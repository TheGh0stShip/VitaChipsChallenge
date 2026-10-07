#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Vita VPK.

A personal build packages the game data from your copy of Chip's Challenge;
--release builds the distributable VPK, which reads the data pack instead.

Requires VitaSDK (set VITASDK, default ~/.local/vitasdk), CMake, Ninja and
Pillow. The VPK path is printed when the build finishes.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
VITASDK = Path(os.environ.get("VITASDK", Path.home() / ".local/vitasdk"))


def run(*command: object) -> None:
    result = subprocess.run([str(part) for part in command], cwd=ROOT,
                            env={**os.environ, "VITASDK": str(VITASDK)})
    if result.returncode != 0:
        sys.exit(f"error: command failed ({result.returncode}): {Path(str(command[0])).name}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--release", action="store_true",
                        help="build a VPK without game data that reads the data pack")
    parser.add_argument("--source", type=Path, default=ROOT / "reference/chips_challenge.zip",
                        help="your copy of the game, for a personal build")
    args = parser.parse_args()
    for tool in ("cmake", "ninja"):
        if shutil.which(tool) is None:
            sys.exit(f"error: {tool} was not found on PATH")
    toolchain = VITASDK / "share" / "vita.toolchain.cmake"
    if not toolchain.exists():
        sys.exit(f"error: VitaSDK toolchain not found at {toolchain}; set VITASDK")
    build = ROOT / ("build-vita-release" if args.release else "build-vita")
    if not args.release:
        source = args.source.expanduser()
        if not source.exists():
            sys.exit(f"error: game copy not found at {source}; pass --source (see README.md)")
        run(sys.executable, ROOT / "tools" / "make_datapack.py", source, build / "datapack")
    run("cmake", "-S", ".", "-B", build, "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}",
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
        f"-DVCC_RELEASE={'ON' if args.release else 'OFF'}")
    run("cmake", "--build", build)
    print(build / "VitaChipsChallenge.vpk")


if __name__ == "__main__":
    main()
