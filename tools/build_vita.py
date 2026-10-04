#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Vita VPK.

A personal build packages the game data from your copy of Chip's Challenge;
--release builds the distributable VPK, which reads the data pack instead.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
VITASDK = Path(os.environ.get("VITASDK", Path.home() / ".local/vitasdk"))


def run(*command: object) -> None:
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True,
                   env={**os.environ, "VITASDK": str(VITASDK)})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", action="store_true",
                        help="build a VPK without game data that reads the data pack")
    parser.add_argument("--source", type=Path, default=ROOT / "reference/chips_challenge.zip",
                        help="your copy of the game, for a personal build")
    args = parser.parse_args()
    build = ROOT / ("build-vita-release" if args.release else "build-vita")
    if not args.release:
        if not args.source.exists():
            sys.exit(f"missing {args.source}; see README.md")
        run(sys.executable, "tools/make_datapack.py", args.source, build / "datapack")
    run("cmake", "-S", ".", "-B", build, "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={VITASDK / 'share/vita.toolchain.cmake'}",
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
        f"-DVCC_RELEASE={'ON' if args.release else 'OFF'}")
    run("cmake", "--build", build)
    print(build / "VitaChipsChallenge.vpk")


if __name__ == "__main__":
    main()
