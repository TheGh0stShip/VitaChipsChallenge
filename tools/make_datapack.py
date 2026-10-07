#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Vita Chips Challenge data pack from your copy of the game.

Usage:
  python tools/make_datapack.py SOURCE [OUTPUT]

SOURCE is the Microsoft Windows 3.x release of Chip's Challenge, either as
a .zip archive or as a folder (subfolders are searched, names may be
any case) holding CHIPS.EXE, CHIPS.DAT, CHIPS.HLP,
WEP4UTIL.DLL, the .WAV files, and the .MID files. Every file is checked
against the supported release before anything is converted.

The result is OUTPUT/VitaChipsChallenge/data (OUTPUT defaults to
build-vita/datapack). Copy the VitaChipsChallenge folder to ux0:data/ on
the Vita, for example with VitaShell's FTP server.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from extract_reference import FILES  # noqa: E402

COPIED = (
    "CHIPS.DAT", "BLIP2.WAV", "BUMMER.WAV", "CLICK3.WAV", "DITTY1.WAV", "DOOR.WAV",
    "OOF3.WAV", "POP2.WAV", "STRIKE.WAV", "TELEPORT.WAV", "WATER2.WAV",
    "CHIP01.MID", "CHIP02.MID",
)
CONVERTED = (
    "OBJ32_4_RGB.bmp", "OBJ32_1_RGB.bmp", "OBJ32_MASKED.bmp", "BACKGROUND_RGB.bmp",
    "INFOWND_RGB.bmp", "200_RGB.bmp", "CHIPEND_RGB.bmp", "ICON_RGB.bmp", "WEP_666_RGB.bmp",
)


def find_sources(source: Path) -> dict[str, object]:
    """Map each wanted upper-case file name to a zip member or a path.

    Matching ignores case and folder nesting, so a zip holding a CHIPS/
    subfolder or lower-case names works. The first match wins.
    """
    found: dict[str, object] = {}
    if source.is_file():
        try:
            archive = zipfile.ZipFile(source)
        except zipfile.BadZipFile:
            raise SystemExit(f"error: {source} is not a valid .zip archive")
        for member in archive.infolist():
            if member.is_dir():
                continue
            name = member.filename.replace("\\", "/").rsplit("/", 1)[-1].upper()
            if name in FILES and name not in found:
                found[name] = (archive, member)
    elif source.is_dir():
        for path in sorted(source.rglob("*")):
            name = path.name.upper()
            if name in FILES and name not in found and path.is_file():
                found[name] = path
    else:
        raise SystemExit(f"error: {source} does not exist")
    return found


def collect(source: Path, work: Path) -> Path:
    originals = work / "originals"
    originals.mkdir()
    found = find_sources(source)
    missing = sorted(name for name in FILES if name not in found)
    if missing:
        raise SystemExit(
            f"error: these game files were not found in {source}:\n  "
            + "\n  ".join(missing)
            + "\nPoint SOURCE at the Windows 3.x release (zip or folder).")
    mismatched = []
    for name, origin in found.items():
        if isinstance(origin, Path):
            data = origin.read_bytes()
        else:
            archive, member = origin
            data = archive.read(member)
        (originals / name).write_bytes(data)
        if hashlib.sha256(data).hexdigest() != FILES[name]:
            mismatched.append(name)
    if mismatched:
        raise SystemExit(
            "error: these files do not match the supported Windows 3.x release:\n  "
            + "\n  ".join(sorted(mismatched))
            + "\nSome re-releases and patched copies differ and are not supported.")
    return originals


def run(*command: object) -> None:
    result = subprocess.run([str(part) for part in command], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        raise SystemExit(f"error: step failed: {Path(str(command[1])).name}")


def check_pillow() -> None:
    try:
        import PIL  # noqa: F401
    except ImportError:
        raise SystemExit("error: Pillow is required. Install it with:\n"
                         f"  {Path(sys.executable).name} -m pip install Pillow")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path,
                        help="your copy of the game: a .zip archive or a folder")
    parser.add_argument("output", type=Path, nargs="?", default=ROOT / "build-vita" / "datapack",
                        help="output folder (default: build-vita/datapack)")
    args = parser.parse_args()
    check_pillow()
    source = args.source.expanduser().resolve()
    output = args.output.expanduser().resolve()
    data = output / "VitaChipsChallenge" / "data"
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        originals = collect(source, work)
        assets = work / "assets"
        run(sys.executable, ROOT / "tools" / "extract_ne_resources.py", originals / "CHIPS.EXE",
            ROOT / "docs" / "reference-inventory.json", assets)
        run(sys.executable, ROOT / "tools" / "extract_ne_resources.py", originals / "WEP4UTIL.DLL",
            ROOT / "docs" / "wep4util-inventory.json", assets, "--prefix", "WEP_")
        run(sys.executable, ROOT / "tools" / "prepare_runtime_assets.py", assets)
        if data.exists():
            shutil.rmtree(data)
        data.mkdir(parents=True)
        run(sys.executable, ROOT / "tools" / "convert_help.py", originals / "CHIPS.HLP", data / "help")
        for name in COPIED:
            shutil.copyfile(originals / name, data / name)
        for name in CONVERTED:
            shutil.copyfile(assets / name, data / name)
    print(f"Data pack written to {data.parent}")
    print("Copy the VitaChipsChallenge folder to ux0:data/ on your Vita.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
