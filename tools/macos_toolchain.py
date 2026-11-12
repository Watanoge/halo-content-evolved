"""The tools a build on macOS needs that are not where the build looks by
default: Homebrew's LLVM (Apple's clang in /usr/bin has no
-fno-define-target-os-macros, which the arm64_32 guest's musl passes, nor the
Windows targets), lld (Homebrew's llvm no longer has lld-link), the Android
SDK and the JDK Android Studio ships.

Each is found at configure time and written into build.ninja as an absolute
path, so a build does not depend on the PATH of the shell that runs ninja.
That matters because ninja reruns configure.py by itself whenever one of its
inputs changes, from whichever shell ran ninja. Elsewhere than on macOS (or
with nothing found) each returns None, and the builds keep their defaults."""

import os
import sys
from pathlib import Path
from typing import Optional

# Homebrew's prefixes: Apple Silicon's, then Intel's
HOMEBREW_PREFIXES = (Path("/opt/homebrew"), Path("/usr/local"))


def _homebrew_tool(formula: str, name: str) -> Optional[Path]:
    if sys.platform != "darwin":
        return None
    for prefix in HOMEBREW_PREFIXES:
        tool = prefix / "opt" / formula / "bin" / name
        if tool.is_file():
            return tool
    return None


def default_clang() -> str:
    """Homebrew LLVM's clang on macOS, or plain clang (the PATH's)"""
    clang = _homebrew_tool("llvm", "clang")
    return str(clang) if clang else "clang"


def lld_link() -> Optional[Path]:
    """Homebrew's lld-link on macOS (the lld formula's, or an older llvm's)"""
    return _homebrew_tool("lld", "lld-link") or _homebrew_tool("llvm", "lld-link")


def llvm_nm() -> Optional[Path]:
    """Homebrew LLVM's llvm-nm on macOS (Apple's nm reads neither ELF nor the
    LLVM bitcode of link-time optimised objects)"""
    return _homebrew_tool("llvm", "llvm-nm")


def ld_lld() -> Optional[Path]:
    """Homebrew's ld.lld on macOS, the linker of the Linux build (clang
    targeting Linux would otherwise reach Apple's ld)"""
    return _homebrew_tool("lld", "ld.lld") or _homebrew_tool("llvm", "ld.lld")


def wayland_scanner() -> Optional[Path]:
    """a wayland-scanner on macOS, which the Linux build's SDL3 needs for its
    Wayland driver: Homebrew has no macOS bottle of wayland, so it is the one
    built from Wayland's source into ~/.local/bin (scanner.c and
    wayland-util.c, against the system's expat), or Homebrew's if it is
    there. Without one, the Linux build is left out of `ninja all`."""
    if sys.platform != "darwin":
        return None
    for tool in (Path.home() / ".local" / "bin" / "wayland-scanner",
                 *(prefix / "bin" / "wayland-scanner" for prefix in HOMEBREW_PREFIXES)):
        if tool.is_file() and os.access(tool, os.X_OK):
            return tool
    return None


def android_sdk() -> Optional[Path]:
    """ANDROID_HOME or ANDROID_SDK_ROOT, or the SDK Android Studio installs on
    macOS"""
    for variable in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        sdk = os.environ.get(variable)
        if sdk and Path(sdk).is_dir():
            return Path(sdk)
    if sys.platform == "darwin":
        sdk = Path.home() / "Library" / "Android" / "sdk"
        if sdk.is_dir():
            return sdk
    return None


def java_home() -> Optional[Path]:
    """JAVA_HOME, or on macOS the JDK inside Android Studio (the system's
    java may be an old browser plugin's, too old for Gradle)"""
    home = os.environ.get("JAVA_HOME")
    if home and Path(home).is_dir():
        return Path(home)
    if sys.platform == "darwin":
        for studio in (Path("/Applications/Android Studio.app"), Path.home() / "Applications" / "Android Studio.app"):
            jbr = studio / "Contents" / "jbr" / "Contents" / "Home"
            if jbr.is_dir():
                return jbr
    return None
