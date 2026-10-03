#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Expand original indexed Windows resources for deterministic Vita rendering."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image


TRANSPARENT_KEY = (1, 2, 3)
RGB_RESOURCES = ("OBJ32_4", "OBJ32_4E", "OBJ32_1", "BACKGROUND", "INFOWND", "200", "CHIPEND")


def vita_bmp(image: Image.Image) -> Image.Image:
    """Expand to 24-bit RGB with the original palette colours unchanged.

    An earlier build swapped red and blue to suit Vita3K; PS Vita hardware
    shows SDL's BMP colours correctly, so no swap is applied."""
    return image.convert("RGB")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("assets", type=Path)
    args = parser.parse_args()
    for name in RGB_RESOURCES:
        source = Image.open(args.assets / f"{name}.bmp")
        vita_bmp(source).save(args.assets / f"{name}_RGB.bmp")

    # Creature, Chip, and item tiles 0x40-0x6F over non-floor ground are
    # composited as 2:00C4 does: the colours come from column tile+0x30
    # (the sprite on white) and the opaque area from tile+0x60 (white mask).
    source = Image.open(args.assets / "OBJ32_4.bmp").convert("RGB")
    masked = Image.new("RGB", source.size, TRANSPARENT_KEY)
    source_pixels = source.load()
    masked_pixels = masked.load()

    def origin(tile: int) -> tuple[int, int]:
        return (tile // 16) * 32, (tile % 16) * 32

    for tile in range(0x40, 0x70):
        tx, ty = origin(tile)
        cx, cy = origin(tile + 0x30)
        mx, my = origin(tile + 0x60)
        for y in range(32):
            for x in range(32):
                if source_pixels[mx + x, my + y] != (0, 0, 0):
                    masked_pixels[tx + x, ty + y] = source_pixels[cx + x, cy + y]
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
            pixels[x, y] = (r, g, b)
    return image


if __name__ == "__main__":
    main()
