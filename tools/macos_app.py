#!/usr/bin/env python3
"""Make the macOS app, build/macos/Halo Content Evolved.app (ninja macos_app,
tools/macos_build.py), from the macOS build's files:

    Contents/Info.plist          port/macos/Info.plist, with the build's number
    Contents/MacOS/halo          the executable (its rpath finds ../Frameworks)
    Contents/Frameworks/         libSDL3.0.dylib, libMoltenVK.dylib
    Contents/Resources/          the icon (from ContentEvolved.png), brokers.txt,
                                 the game's data files, SDL3's licence

and sign it ad hoc (no Apple account: the first start is allowed in System
Settings, Privacy & Security). The app keeps its settings and saves in
~/Library/Application Support/Halo Content Evolved, and asks for the game
folder (port/macos/host/host_game_folder_macos.c).

Usage: macos_app.py APP EXECUTABLE PLIST ICON_PNG VERSION
       --frameworks FILE... --resources FILE...
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ICON_SIZES = (16, 32, 128, 256, 512)


def make_icon(png: Path, out: Path) -> None:
    with tempfile.TemporaryDirectory() as folder:
        iconset = Path(folder) / "AppIcon.iconset"
        iconset.mkdir()
        for size in ICON_SIZES:
            for scale, suffix in ((1, ""), (2, "@2x")):
                subprocess.run(["sips", "-z", str(size * scale), str(size * scale), str(png), "--out",
                                str(iconset / f"icon_{size}x{size}{suffix}.png")], check=True,
                               stdout=subprocess.DEVNULL)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(out)], check=True)


def main(arguments) -> None:
    app, executable, plist, icon_png = (Path(argument) for argument in arguments[:4])
    version = arguments[4]
    rest = arguments[5:]
    frameworks = rest[rest.index("--frameworks") + 1:rest.index("--resources")]
    resources = rest[rest.index("--resources") + 1:]
    if app.suffix != ".app" or "build" not in app.parts:
        sys.exit(f"{app}: not an app bundle in the build folder")
    if app.exists():
        shutil.rmtree(app)
    contents = app / "Contents"
    for folder in ("MacOS", "Frameworks", "Resources"):
        (contents / folder).mkdir(parents=True)
    (contents / "Info.plist").write_text(plist.read_text(encoding="utf-8").replace("@VERSION@", version),
                                         encoding="utf-8")
    shutil.copy2(executable, contents / "MacOS" / "halo")
    for name in frameworks:
        shutil.copy2(Path(name).resolve(), contents / "Frameworks" / Path(name).name)
    for name in resources:
        # (SDL's licence and MoltenVK's, each named for its library)
        target = {"LICENSE.txt": "SDL3-LICENSE.txt", "LICENSE": "MoltenVK-LICENSE.txt"}.get(Path(name).name,
                                                                                         Path(name).name)
        shutil.copy2(name, contents / "Resources" / target)
    make_icon(icon_png, contents / "Resources" / "AppIcon.icns")
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main(sys.argv[1:])
