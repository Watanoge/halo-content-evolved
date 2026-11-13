"""Ninja rules for the macOS build (``ninja macos``): Apple silicon only.

macOS on Apple silicon keeps the low 4 GB of every process unmapped
(__PAGEZERO; an executable linked with a smaller one is killed before main),
and the game's data needs 32-bit pointers. So the game runs as the Android
and the 64-bit ARM Linux builds run it (port/android/README.md, "How the port
operates"): an ILP32 AArch64 guest image (arm64_32) inside a 64-bit host. Its
4 GB lie at a base the host chooses, a multiple of 4 GB, and the guest's code
is rewritten to add that base, kept in x21, to every address it uses
(tools/guest_base_register.py; port/macos/README.md, "The guest's memory").

- build/macos/halo: the host (port/macos/host, with the Android host's
  shared files and the Linux desktop host's host_desktop.c), with the guest
  image inside it: the desktop guest with the Vulkan renderer, as the PS5's
  is (tools/ps5_build.py), and HALO_MACOS;
- build/macos/libSDL3.0.dylib: SDL3, beside it (an rpath of
  @executable_path);
- the renderer is the Vulkan backend of the Android host, on MoltenVK
  (Homebrew's molten-vk; build/macos/libMoltenVK.dylib, beside it too).

The sources (musl, SDL3, glslang) are the Android build's
(build/android/third_party) when they are there, else downloaded to
build/macos/third_party. The guest's OpenGL ES headers (for the GL stubs that
the Vulkan image still carries, never called) are the Android NDK's.
"""

import os
import platform
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from .android_build import (GLSLANG_TAG, GLSLANG_URL, MBEDTLS_DIR, MUSL_VERSION, SDL_TAG, TOML_DIR, ZLIB_DEFINES,
                            ZLIB_DIR, ZLIB_SOURCES, _find_ndk, fetch_third_party, generate_guest_image)
from .guest_arch import ARM64_32
from .linux_build import (GAME_DATA_FILES, MINIUPNPC_DEFINES, MINIUPNPC_DIR, compile_launcher, miniupnpc_sources,
                          updater_defines)
from .ninja_syntax import Writer, escape

PORT_DIR = Path("port/macos")
ANDROID_DIR = Path("port/android")
LINUX_DIR = Path("port/linux")
LINUX_HOST_DIR = LINUX_DIR / "host"
BUILD = Path("build/macos")
APP = BUILD / "Halo Content Evolved.app"
ANDROID_THIRD_PARTY = Path("build/android/third_party")
OWN_THIRD_PARTY = BUILD / "third_party"
HOMEBREW = Path("/opt/homebrew")
MOLTENVK = HOMEBREW / "opt" / "molten-vk"
VULKAN_HEADERS = HOMEBREW / "opt" / "vulkan-headers" / "include"
MACOS_MINIMUM = "14.4"

# the Android host's files the macOS host uses as they are (with HALO_MACOS
# where it differs); the rest are port/macos/host's: the entry point, the
# guest's memory, its system calls, the Vulkan driver (MoltenVK), the
# debugging hooks and OpenGL's absence
SHARED_HOST_FILES = ("host_arch.c", "host_common.c", "host_loader.c", "host_thread.c", "host_watch_hash.c",
                     "host_https.c", "host_replay.c", "host_sdl.c")
SHARED_RENDERER_FILES = ("host_vk.c", "host_vk_data.c", "host_vk_draw.c", "host_vk_post.c", "host_vk_present.c",
                         "host_vk_hud.c", "host_vk_profile.c", "host_vk_render.c", "host_vk_shaders.c",
                         "host_vk_texture.c", "host_vk_visibility.c", "host_vk_passes.c")
POSIX_FILES = ("posix_files.c", "posix_net.c", "posix_mcc_preview.c", "posix_mcc_share.c")
TLS_FILES = ("posix_dtls.c", "posix_update.c")


def macos_configure_inputs() -> List[Path]:
    return [Path(__file__), PORT_DIR / "host"]


def native() -> bool:
    return sys.platform == "darwin" and platform.machine().lower() in ("arm64", "aarch64")


def _unavailable(n: Writer, reason: str) -> None:
    message = f"macOS build (macos) unavailable: {reason}"
    print(message, file=sys.stderr)
    n.comment(message)
    n.rule(name="macos_unavailable", command=escape(f"echo {shlex.quote(message)} >&2 && exit 1"),
           description="MACOS UNAVAILABLE")
    n.build(outputs="macos", rule="macos_unavailable")
    n.newline()


def _third_party() -> Path:
    """the folder of musl, SDL3 and glslang: the Android build's when it has
    them all, else this build's own (downloaded)"""
    if all((ANDROID_THIRD_PARTY / name).is_dir() for name in (f"musl-{MUSL_VERSION}", "SDL3", "glslang")):
        return ANDROID_THIRD_PARTY
    fetch_third_party(OWN_THIRD_PARTY)
    glslang = OWN_THIRD_PARTY / "glslang"
    if not glslang.is_dir():
        print(f"Cloning glslang {GLSLANG_TAG}")
        subprocess.run(["git", "clone", "-q", "--depth", "1", "--branch", GLSLANG_TAG, GLSLANG_URL, str(glslang)],
                       check=True)
    return OWN_THIRD_PARTY


def _homebrew_llvm(tool: str) -> str:
    for folder in (HOMEBREW / "opt" / "llvm" / "bin", HOMEBREW / "opt" / "lld" / "bin"):
        if (folder / tool).exists():
            return str(folder / tool)
    raise RuntimeError(f"no {tool} in Homebrew's llvm or lld (brew install llvm lld)")


def generate_macos_build(n: Writer, sln: Any) -> None:
    config_path = LINUX_DIR / "port.json"
    if not config_path.is_file() or not (PORT_DIR / "host").is_dir():
        return
    if not native():
        n.comment("macOS build (macos): only on an Apple silicon Mac")
        return
    try:
        _generate(n, sln, config_path)
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        _unavailable(n, str(error))


def _generate(n: Writer, sln: Any, config_path: Path) -> None:
    import json

    if not (MOLTENVK / "lib" / "libMoltenVK.dylib").is_file() or not (VULKAN_HEADERS / "vulkan" / "vulkan.h").is_file():
        raise RuntimeError("no MoltenVK or Vulkan headers (brew install molten-vk vulkan-headers)")
    ndk = _find_ndk()
    if not ndk:
        raise RuntimeError("no Android NDK for the guest's OpenGL ES headers (set ANDROID_NDK_HOME)")
    prebuilt = sorted((ndk / "toolchains" / "llvm" / "prebuilt").iterdir())
    gl_headers = prebuilt[0] / "sysroot" / "usr" / "include"
    # (the runtime library the image links with: compiler-rt's AArch64
    # builtins, an ELF archive, which the NDK's clang names; Homebrew's names
    # its Darwin one)
    builtins_cc = str(prebuilt[0] / "bin" / "aarch64-linux-android28-clang")
    guest_cc = getattr(sln, "macos_guest_cc", None) or _homebrew_llvm("clang")
    ar, ld, objdump = _homebrew_llvm("llvm-ar"), _homebrew_llvm("ld.lld"), _homebrew_llvm("llvm-objdump")
    third_party = _third_party()
    sdl_dir = third_party / "SDL3"
    glslang_dir = third_party / "glslang"
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))
    python = "$python"
    host_cc = "/usr/bin/clang"

    n.comment("macOS build (ninja macos); see port/macos/README.md")
    n.variable("macos_guest_cc", guest_cc)

    # ---------- the guest image: the desktop guest with the Vulkan renderer, on a base register

    release = bool(getattr(sln, "port_release", False))
    guest = generate_guest_image(
        n, sln, config, arch=ARM64_32, prefix="macos", label="MACOS", build=BUILD, third_party=third_party,
        guest_cc=guest_cc, gl_headers=gl_headers, ar=ar, ld=ld, objdump=objdump, builtins_cc=builtins_cc,
        environment="gnu", platform_defines=["-DHALO_GLES=1", "-DHALO_MACOS=1"],
        extra_runtime=[LINUX_HOST_DIR / "guest_desktop.c"],
        extra_imports=[LINUX_HOST_DIR / "host_imports.list", ANDROID_DIR / "host_imports_vk.list"],
        updater_cflags=updater_defines(release), vulkan=True, base_register=True,
        extra_sources=[Path("port/third_party/qrcodegen/qrcodegen.c")])

    # ---------- SDL3, for Cocoa, Metal and CoreAudio

    sdl_build = BUILD / "sdl3-build"
    libsdl = sdl_build / "libSDL3.0.dylib"
    n.rule(
        name="macos_sdl3",
        command=(f"cmake -S {sdl_dir} -B {sdl_build} -G Ninja -DCMAKE_BUILD_TYPE=Release "
                 f"-DCMAKE_C_COMPILER={host_cc} -DCMAKE_OSX_ARCHITECTURES=arm64 "
                 f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_MINIMUM} "
                 "-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF "
                 "-DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF "
                 f"> {BUILD}/sdl3-configure.log && ninja -C {sdl_build} > {BUILD}/sdl3-build.log"),
        description=f"MACOS SDL3 {SDL_TAG}",
        pool="console",
        restat=True,
    )
    n.build(outputs=libsdl, rule="macos_sdl3", implicit=[sdl_dir / "CMakeLists.txt"])

    # ---------- glslang, static, as the PS5's (host_vk_shaders.c)

    glslang_build = BUILD / "glslang-build"
    glslang_libraries = [glslang_build / "glslang" / "libglslang.a",
                         glslang_build / "glslang" / "libglslang-default-resource-limits.a"]
    n.rule(
        name="macos_glslang",
        command=(f"cmake -S {glslang_dir} -B {glslang_build} -G Ninja -DCMAKE_BUILD_TYPE=Release "
                 f"-DCMAKE_C_COMPILER={host_cc} -DCMAKE_CXX_COMPILER=/usr/bin/clang++ "
                 f"-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_MINIMUM} "
                 "-DENABLE_OPT=OFF -DENABLE_HLSL=OFF -DENABLE_GLSLANG_BINARIES=OFF -DENABLE_GLSLANG_JS=OFF "
                 "-DGLSLANG_TESTS=OFF -DBUILD_EXTERNAL=OFF -DBUILD_SHARED_LIBS=OFF "
                 f"> {BUILD}/glslang-configure.log && "
                 f"ninja -C {glslang_build} glslang glslang-default-resource-limits > {BUILD}/glslang-build.log"),
        description=f"MACOS GLSLANG {GLSLANG_TAG}",
        pool="console",
        restat=True,
    )
    n.build(outputs=glslang_libraries, rule="macos_glslang", implicit=[glslang_dir / "CMakeLists.txt"])

    # ---------- the host

    host_obj_dir = BUILD / "host" / "obj"
    n.rule(
        name="macos_host_cc",
        command=f"{compile_launcher(sln)}{host_cc} -MMD -MF $out.d $cflags -c $in -o $out",
        description="MACOS HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    # (the weak stand-ins for imports no host file answers yet: each ends the
    # run naming itself, tools/ps5_build.py's)
    import_stubs = BUILD / "host" / "gen" / "host_import_stubs.c"
    n.rule(name="macos_import_stubs", command=f"{python} -m tools.ps5_build stubs $in $out",
           description="MACOS IMPORT STUBS $out")
    n.build(outputs=import_stubs, rule="macos_import_stubs", inputs=guest["host_table_c"],
            implicit=[Path("tools/ps5_build.py")])
    host_cflags = " ".join([
        "-O2", "-g", "-fno-omit-frame-pointer", "-Wall", "-Wno-unused-function", "-Wno-deprecated-declarations",
        "-arch arm64", f"-mmacosx-version-min={MACOS_MINIMUM}",
        "-DHALO_MACOS=1", "-DHALO_GUEST_BASE=1", "-D_DARWIN_C_SOURCE",
        f"-I{PORT_DIR}/host", f"-I{ANDROID_DIR}/include", f"-I{ANDROID_DIR}/host", f"-I{ANDROID_DIR}/guest",
        f"-I{LINUX_DIR}/src", f"-I{TOML_DIR}", f"-I{ZLIB_DIR}", f"-I{glslang_dir}",
        f"-isystem {sdl_dir}/include", f"-isystem {VULKAN_HEADERS}",
        # (the guest's system call numbers and its libc's layouts, which the
        # host translates: port/android/guest/libc/arch)
        f"-I{ANDROID_DIR}/guest/libc/arch/generic",
        shlex.quote(f'-DHOST_VK_GLSLANG_TAG="{GLSLANG_TAG}"'),
    ])
    host_sources: List[Path] = [
        *sorted((PORT_DIR / "host").glob("*.c")),
        *(ANDROID_DIR / "host" / name for name in SHARED_HOST_FILES + SHARED_RENDERER_FILES),
        LINUX_HOST_DIR / "host_desktop.c",
        *(LINUX_DIR / "src" / name for name in POSIX_FILES),
        TOML_DIR / "tomlc17.c",
    ]
    host_objects: List[Path] = []
    for source in host_sources:
        obj = host_obj_dir / (source.parent.name + "_" + source.name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=source, variables={"cflags": host_cflags})
        host_objects.append(obj)
    for source in sorted((PORT_DIR / "host").glob("*.S")):
        obj = host_obj_dir / (source.name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=source, variables={"cflags": f"-arch arm64 -mmacosx-version-min={MACOS_MINIMUM}"})
        host_objects.append(obj)
    miniupnpc_cflags = " ".join([host_cflags, f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}",
                                 *MINIUPNPC_DEFINES])
    for source in [LINUX_DIR / "src" / "posix_upnp.c", *miniupnpc_sources()]:
        obj = host_obj_dir / ("miniupnpc_" + source.name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=source,
                variables={"cflags": miniupnpc_cflags + (" -w" if source.name != "posix_upnp.c" else "")})
        host_objects.append(obj)
    mbedtls_cflags = " ".join([host_cflags, f"-I{MBEDTLS_DIR / 'include'}", f"-I{MBEDTLS_DIR / 'library'}",
                               "-DMBEDTLS_THREADING_C", "-DMBEDTLS_THREADING_PTHREAD"])
    for source in [*(LINUX_DIR / "src" / name for name in TLS_FILES), *sorted((MBEDTLS_DIR / "library").glob("*.c"))]:
        obj = host_obj_dir / ("mbedtls_" + source.name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=source,
                variables={"cflags": mbedtls_cflags + (" -w" if source.name not in TLS_FILES else "")})
        host_objects.append(obj)
    for name in ZLIB_SOURCES:
        obj = host_obj_dir / ("zlib_" + name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=ZLIB_DIR / name,
                variables={"cflags": " ".join([host_cflags, *ZLIB_DEFINES, "-w"])})
        host_objects.append(obj)
    for generated in (guest["host_table_c"], import_stubs):
        obj = host_obj_dir / (generated.name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=generated, variables={"cflags": host_cflags})
        host_objects.append(obj)
    # the guest image, inside the executable (host_main_macos.c)
    image_obj = host_obj_dir / "guest_image.o"
    n.rule(name="macos_image", command=f"{host_cc} -arch arm64 -mmacosx-version-min={MACOS_MINIMUM} -DGUEST_IMAGE='\"$image\"' -c $in -o $out",
           description="MACOS IMAGE $out")
    n.build(outputs=image_obj, rule="macos_image", inputs=PORT_DIR / "guest_image.S",
            implicit=[guest["vk_image"], guest["vk_checked"]], variables={"image": str(guest["vk_image"])})
    host_objects.append(image_obj)

    output = BUILD / "halo"
    n.rule(
        name="macos_host_link",
        command=(f"/usr/bin/clang++ -arch arm64 -mmacosx-version-min={MACOS_MINIMUM} -o $out @$out.rsp "
                 f"-L{sdl_build} -lSDL3.0 -Wl,-rpath,@executable_path -Wl,-rpath,@executable_path/../Frameworks "
                 "-framework Foundation -framework AppKit -framework QuartzCore -framework Metal "
                 "-framework IOKit -framework Security -framework CoreGraphics"),
        description="MACOS LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(outputs=output, rule="macos_host_link", inputs=host_objects + glslang_libraries, implicit=[libsdl])

    # ---------- beside it: SDL, MoltenVK, the brokers and the game's data files

    n.rule(name="macos_copy", command="cp -L $in $out", description="MACOS STAGE $out")
    staged = [BUILD / "libSDL3.0.dylib", BUILD / "libMoltenVK.dylib", BUILD / "brokers.txt",
              BUILD / "SDL3-LICENSE.txt"]
    n.build(outputs=staged[0], rule="macos_copy", inputs=libsdl)
    n.build(outputs=staged[1], rule="macos_copy", inputs=MOLTENVK / "lib" / "libMoltenVK.dylib")
    n.build(outputs=staged[2], rule="macos_copy", inputs=Path("port/assets/network/brokers.txt"))
    n.build(outputs=staged[3], rule="macos_copy", inputs=sdl_dir / "LICENSE.txt")
    for source in GAME_DATA_FILES:
        n.build(outputs=BUILD / source.name, rule="macos_copy", inputs=source)
        staged.append(BUILD / source.name)
    n.build(outputs="macos", rule="phony", inputs=[output, *staged])

    # ---------- the app (tools/macos_app.py), signed ad hoc, and a zip of it

    version = os.environ.get("HALO_BUILD_NUMBER", "")
    version = version if version.isdigit() else "0"
    plist = APP / "Contents" / "Info.plist"
    frameworks = [libsdl, MOLTENVK / "lib" / "libMoltenVK.dylib"]
    # (the brokers, the master servers' list and the game's data files, which
    # the game reads beside it: SDL_GetBasePath, the app's Resources)
    resources = [Path("port/assets/network/brokers.txt"), Path("port/assets/network/masters.txt"), *GAME_DATA_FILES,
                 sdl_dir / "LICENSE.txt", MOLTENVK / "LICENSE"]
    n.rule(
        name="macos_app",
        command=(f"{python} tools/macos_app.py {shlex.quote(str(APP))} {output} {PORT_DIR / 'Info.plist'} "
                 f"ContentEvolved.png {version} --frameworks {' '.join(map(str, frameworks))} "
                 f"--resources {' '.join(map(str, resources))}"),
        description="MACOS APP",
    )
    n.build(outputs=plist, rule="macos_app",
            inputs=[output, *frameworks, PORT_DIR / "Info.plist", Path("ContentEvolved.png"), *resources],
            implicit=[Path("tools/macos_app.py")])
    n.build(outputs="macos_app", rule="phony", inputs=plist)
    archive = BUILD / "halo-macos-arm64.zip"
    n.rule(name="macos_zip", command=f"ditto -c -k --keepParent {shlex.quote(str(APP))} $out.partial && mv $out.partial $out",
           description="MACOS ZIP $out")
    n.build(outputs=archive, rule="macos_zip", inputs=plist)
    n.build(outputs="macos_zip", rule="phony", inputs=archive)
    n.newline()
