#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Expand original indexed Windows resources for deterministic Vita rendering."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image


TRANSPARENT_KEY = (3, 2, 1)
RGB_RESOURCES = ("OBJ32_4", "OBJ32_4E", "OBJ32_1", "BACKGROUND", "INFOWND", "200", "CHIPEND")


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

    # The application icon for the About dialog, transparent where the AND
    # mask is set.
    icon = args.assets / "ICON_1.dib"
    if icon.exists():
        icon_rgb(icon.read_bytes()).save(args.assets / "ICON_RGB.bmp")

    # WEP4UTIL banner used by WEPABOUT2.
    banner = args.assets / "WEP_666.bmp"
    if banner.exists():
        vita_bmp(Image.open(banner)).save(args.assets / "WEP_666_RGB.bmp")


def icon_rgb(dib: bytes) -> Image.Image:
    header_size, width, height, _, depth = struct.unpack_from("<IiiHH", dib)
    height //= 2
    colors = 1 << depth
    palette = [dib[header_size + 4 * i: header_size + 4 * i + 3] for i in range(colors)]
    xor_offset = header_size + 4 * colors
    xor_stride = ((width * depth + 31) // 32) * 4
    and_offset = xor_offset + xor_stride * height
    and_stride = ((width + 31) // 32) * 4
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        row = height - 1 - y
        for x in range(width):
            bit = (dib[and_offset + row * and_stride + x // 8] >> (7 - x % 8)) & 1
            if bit:
                pixels[x, y] = TRANSPARENT_KEY
                continue
            value = dib[xor_offset + row * xor_stride + x * depth // 8]
            if depth == 4:
                value = (value >> 4) if x % 2 == 0 else (value & 15)
            b, g, r = palette[value]
            pixels[x, y] = (b, g, r)  # Vita SDL swaps red and blue
    return image


if __name__ == "__main__":
    main()
