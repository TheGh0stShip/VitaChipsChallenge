#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract original bitmap resources from the inventoried Windows NE file."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def safe_name(value: object) -> str:
    text = str(value)
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in text)


def dib_to_bmp(dib: bytes) -> bytes:
    if len(dib) < 40:
        raise ValueError("bitmap resource is shorter than BITMAPINFOHEADER")
    header_size, width, height, planes, depth = struct.unpack_from("<IiiHH", dib)
    if header_size < 40 or planes != 1 or width <= 0 or height == 0:
        raise ValueError("unsupported bitmap resource header")
    colors_used = struct.unpack_from("<I", dib, 32)[0]
    palette_entries = colors_used or (1 << depth if depth <= 8 else 0)
    pixel_offset = 14 + header_size + palette_entries * 4
    file_size = 14 + len(dib)
    return struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset) + dib


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    executable = args.executable.read_bytes()
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    args.output.mkdir(parents=True, exist_ok=True)
    extracted = 0
    for resource in inventory["resources"]:
        if resource["type"] != 2:
            continue
        offset = int(resource["file_offset"])
        size = int(resource["size"])
        end = offset + size
        if end > len(executable):
            raise ValueError(f"resource {resource['id']} extends beyond executable")
        output = args.output / f"{safe_name(resource['id'])}.bmp"
        output.write_bytes(dib_to_bmp(executable[offset:end]))
        print(output)
        extracted += 1
    if extracted == 0:
        raise ValueError("inventory contains no NE bitmap resources")


if __name__ == "__main__":
    main()
