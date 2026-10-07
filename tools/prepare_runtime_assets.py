#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Expand original indexed Windows resources for deterministic Vita rendering."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageChops


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

    def box(tile: int) -> tuple[int, int, int, int]:
        x, y = (tile // 16) * 32, (tile % 16) * 32
        return x, y, x + 32, y + 32

    for tile in range(0x40, 0x70):
        sprite = source.crop(box(tile + 0x30))
        masked.paste(sprite, box(tile), opaque_mask(source.crop(box(tile + 0x60))))
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


def opaque_mask(image: Image.Image) -> Image.Image:
    """Return an L mask that is 255 wherever an RGB pixel is not black."""
    r, g, b = image.split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v else 0)


ICON_RAW_MODES = {1: "P;1", 4: "P;4", 8: "P"}


def icon_rgb(dib: bytes) -> Image.Image:
    """Decode a bottom-up RT_ICON DIB (colour image plus AND mask)."""
    header_size, width, height, _, depth = struct.unpack_from("<IiiHH", dib)
    if depth not in ICON_RAW_MODES:
        raise ValueError(f"unsupported icon depth {depth}")
    height //= 2
    colors = 1 << depth
    palette = bytearray()
    for i in range(colors):
        b, g, r = dib[header_size + 4 * i: header_size + 4 * i + 3]
        palette += bytes((r, g, b))
    xor_offset = header_size + 4 * colors
    xor_stride = ((width * depth + 31) // 32) * 4
    and_offset = xor_offset + xor_stride * height
    and_stride = ((width + 31) // 32) * 4
    size = (width, height)
    image = Image.frombytes("P", size, dib[xor_offset:and_offset], "raw",
                            ICON_RAW_MODES[depth], xor_stride, -1)
    image.putpalette(palette)
    image = image.convert("RGB")
    mask = Image.frombytes("1", size, dib[and_offset:and_offset + and_stride * height],
                           "raw", "1", and_stride, -1)
    image.paste(TRANSPARENT_KEY, (0, 0, width, height), mask)
    return image

if __name__ == "__main__":
    main()
