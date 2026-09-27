#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a personal VPK from the supplied original Windows archive."""

from __future__ import annotations

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
    archive = ROOT / "reference/chips_challenge.zip"
    if not archive.exists():
        sys.exit(f"missing {archive}; see reference/README.md")
    build = ROOT / "build-vita"
    assets = build / "assets"
    vita_assets = ROOT / "assets/vita"
    run(sys.executable, "tools/extract_reference.py", archive, "reference/extracted")
    run(sys.executable, "tools/extract_ne_resources.py", "reference/extracted/CHIPS.EXE",
        "docs/reference-inventory.json", assets)
    run(sys.executable, "tools/build_vita_assets.py", assets, vita_assets,
        "--branding", "assets/branding")
    run("cmake", "-S", ".", "-B", build, "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={VITASDK / 'share/vita.toolchain.cmake'}",
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF")
    run("cmake", "--build", build)
    print(build / "VitaChipsChallenge.vpk")


if __name__ == "__main__":
    main()
