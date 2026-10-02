#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Expand original indexed Windows resources for deterministic Vita rendering."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


TRANSPARENT_KEY = (3, 2, 1)
RGB_RESOURCES = ("OBJ32_4", "OBJ32_4E", "BACKGROUND", "INFOWND", "200", "CHIPEND")


def vita_bmp(image: Image.Image) -> Image.Image:
    """Compensate for SDL2 Vita's red/blue interpretation of 24-bit BMP pixels."""
    red, green, blue = image.convert("RGB").split()
    return Image.merge("RGB", (blue, green, red))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("assets", type=Path)
    args = parser.parse_args()
    for name in RGB_RESOURCES:
        source = Image.open(args.assets / f"{name}.bmp")
        vita_bmp(source).save(args.assets / f"{name}_RGB.bmp")

    source = Image.open(args.assets / "OBJ32_4.bmp").convert("RGB")
    mask = Image.open(args.assets / "OBJ32_1.bmp").convert("1")
    masked = vita_bmp(source)
    masked_pixels = masked.load()
    mask_pixels = mask.load()
    for y in range(source.height):
        for x in range(source.width):
            if mask_pixels[x, y] == 0:
                masked_pixels[x, y] = TRANSPARENT_KEY
    masked.save(args.assets / "OBJ32_MASKED.bmp")


if __name__ == "__main__":
    main()
