#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Vita Chips Challenge data pack from your copy of the game.

Usage:
  python3 tools/make_datapack.py SOURCE [OUTPUT]

SOURCE is the Microsoft Windows 3.x release of Chip's Challenge, either as
a .zip archive or as a folder holding CHIPS.EXE, CHIPS.DAT, CHIPS.HLP,
WEP4UTIL.DLL, the .WAV files, and the .MID files. Every file is checked
against the supported release before anything is converted.

The result is OUTPUT/VitaChipsChallenge/data (OUTPUT defaults to
build-vita/datapack). Copy the VitaChipsChallenge folder to ux0:data/ on
the Vita, for example with VitaShell's FTP server.
"""

from __future__ import annotations

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


def collect(source: Path, work: Path) -> Path:
    originals = work / "originals"
    originals.mkdir()
    if source.is_file():
        with zipfile.ZipFile(source) as archive:
            for member in archive.infolist():
                name = Path(member.filename).name.upper()
                if name in FILES and not member.is_dir():
                    (originals / name).write_bytes(archive.read(member))
    else:
        for path in source.iterdir():
            if path.name.upper() in FILES and path.is_file():
                shutil.copyfile(path, originals / path.name.upper())
    missing = [name for name in FILES if not (originals / name).exists()]
    if missing:
        raise SystemExit("missing game files: " + ", ".join(sorted(missing)))
    for name, expected in FILES.items():
        if hashlib.sha256((originals / name).read_bytes()).hexdigest() != expected:
            raise SystemExit(f"{name} does not match the supported Windows release")
    return originals


def run(*command: object) -> None:
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else ROOT / "build-vita/datapack"
    data = output / "VitaChipsChallenge" / "data"
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        originals = collect(source, work)
        assets = work / "assets"
        run(sys.executable, "tools/extract_ne_resources.py", originals / "CHIPS.EXE",
            "docs/reference-inventory.json", assets)
        run(sys.executable, "tools/extract_ne_resources.py", originals / "WEP4UTIL.DLL",
            "docs/wep4util-inventory.json", assets, "--prefix", "WEP_")
        run(sys.executable, "tools/prepare_runtime_assets.py", assets)
        if data.exists():
            shutil.rmtree(data)
        data.mkdir(parents=True)
        run(sys.executable, "tools/convert_help.py", originals / "CHIPS.HLP", data / "help")
        for name in COPIED:
            shutil.copyfile(originals / name, data / name)
        for name in CONVERTED:
            shutil.copyfile(assets / name, data / name)
    print(f"Data pack written to {data.parent}")
    print("Copy the VitaChipsChallenge folder to ux0:data/ on your Vita.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
