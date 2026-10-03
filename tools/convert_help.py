#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert the supplied WinHelp 3.0 CHIPS.HLP into the viewer's format.

The output is derived from the user's reference copy at build time and is
never committed. It contains:

  help.txt   topics, paragraphs, formatting runs, links, and keywords
  hbmN.bmp   the help file's bitmaps

help.txt is line oriented, one record per line, fields separated by tabs:

  N  font  attributes  half-points  face  red  green  blue
  T  number  title
  P  align  left  first  above  below  tab
  Q                     paragraph end (formatting continues)
  F  font
  X  text
  L                     line break
  A                     tab
  J  topic              jump hotspot start (green, solid underline)
  U  topic              popup hotspot start (green, dotted underline)
  E                     hotspot end
  I  align  bitmap      picture (c in line, l left, r right)
  K  keyword  topic
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

BLOCK = 2048
BLOCK_HEADER = 12


def read_files(data: bytes) -> dict[str, bytes]:
    directory = struct.unpack_from("<I", data, 4)[0]
    tree = directory + 9
    page_size = struct.unpack_from("<H", data, tree + 4)[0]
    pages = struct.unpack_from("<H", data, tree + 30)[0]
    files = {}
    for page in range(pages):
        start = tree + 38 + page * page_size
        count = struct.unpack_from("<H", data, start + 2)[0]
        cursor = start + 8
        for _ in range(count):
            end = data.index(b"\0", cursor)
            name = data[cursor:end].decode("latin1")
            offset = struct.unpack_from("<I", data, end + 1)[0]
            cursor = end + 5
            used = struct.unpack_from("<i", data, offset + 4)[0]
            files[name] = data[offset + 9: offset + 9 + used]
    return files


def btree_leaves(tree: bytes):
    page_size = struct.unpack_from("<H", tree, 4)[0]
    levels = struct.unpack_from("<H", tree, 32)[0]
    pages = struct.unpack_from("<H", tree, 30)[0]
    for page in range(pages):
        start = 38 + page * page_size
        if start + 8 > len(tree):
            break
        yield tree[start: start + page_size], levels


def cs_long(b: bytes, i: int):
    w = b[i] | (b[i + 1] << 8)
    if w & 1:
        return (struct.unpack_from("<I", b, i)[0] >> 1) - 0x40000000, i + 4
    return (w >> 1) - 0x4000, i + 2


def cu_long(b: bytes, i: int):
    w = b[i] | (b[i + 1] << 8)
    if w & 1:
        return struct.unpack_from("<I", b, i)[0] >> 1, i + 4
    return w >> 1, i + 2


def cu_short(b: bytes, i: int):
    if b[i] & 1:
        return (b[i] | (b[i + 1] << 8)) >> 1, i + 2
    return b[i] >> 1, i + 1


def cs_short(b: bytes, i: int):
    if b[i] & 1:
        return ((b[i] | (b[i + 1] << 8)) >> 1) - 0x4000, i + 2
    return (b[i] >> 1) - 0x40, i + 1


class Phrases:
    def __init__(self, table: bytes):
        count = struct.unpack_from("<H", table, 0)[0]
        offsets = struct.unpack_from(f"<{count + 1}H", table, 4)
        self.items = [table[4 + offsets[i]: 4 + offsets[i + 1]] for i in range(count)]

    def expand(self, text: bytes) -> bytes:
        out = bytearray()
        i = 0
        while i < len(text):
            c = text[i]
            if 0 < c < 16 and i + 1 < len(text):
                index = (c - 1) * 256 + text[i + 1]
                out += self.items[index // 2]
                if index & 1:
                    out += b" "
                i += 2
            else:
                out.append(c)
                i += 1
        return bytes(out)


def topic_stream(topic: bytes) -> bytes:
    return b"".join(topic[o + BLOCK_HEADER: o + BLOCK] for o in range(0, len(topic), BLOCK))


def absolute(position: int) -> int:
    per = BLOCK - BLOCK_HEADER
    return (position // per) * BLOCK + BLOCK_HEADER + position % per


def clean(text: bytes) -> str:
    return text.decode("cp1252", errors="replace").replace("\t", " ").replace("\n", " ")


def paragraph(ld1: bytes, text: bytes, out: list[str]) -> None:
    i = 0
    _, i = cs_long(ld1, i)
    i += 2
    i += 2  # paragraph id
    bits = struct.unpack_from("<H", ld1, i)[0]
    i += 2
    values = {"above": 0, "below": 0, "lines": 0, "left": 0, "right": 0, "first": 0}
    if bits & 1:
        _, i = cs_long(ld1, i)
    for bit, name in ((2, "above"), (4, "below"), (8, "lines"), (16, "left"), (32, "right"),
                      (64, "first")):
        if bits & bit:
            values[name], i = cs_short(ld1, i)
    if bits & 0x100:
        i += 3
    tab = 0
    if bits & 0x200:
        count, i = cs_short(ld1, i)
        for k in range(count):
            stop, i = cu_short(ld1, i)
            if stop & 0x4000:
                _, i = cu_short(ld1, i)
            if k == 0:
                tab = stop & 0x3FFF
    align = "r" if bits & 0x400 else ("c" if bits & 0x800 else "l")
    out.append(f"P\t{align}\t{values['left']}\t{values['first']}\t{values['above']}"
               f"\t{values['below']}\t{tab}")
    strings = text.split(b"\0")
    index = 0

    def emit_text() -> None:
        nonlocal index
        if index < len(strings) and strings[index]:
            out.append("X\t" + clean(strings[index]))
        index += 1

    emit_text()
    while i < len(ld1):
        c = ld1[i]
        i += 1
        if c == 0xFF:
            break
        if c == 0x80:
            out.append(f"F\t{struct.unpack_from('<H', ld1, i)[0]}")
            i += 2
        elif c == 0x81:
            out.append("L")
        elif c == 0x82:
            out.append("Q")
        elif c == 0x83:
            out.append("A")
        elif c in (0x86, 0x87, 0x88):
            kind = ld1[i]
            i += 1
            _, i = cs_long(ld1, i)
            if kind == 0x22:
                _, i = cu_short(ld1, i)
            number = struct.unpack_from("<H", ld1, i + 2)[0]
            i += 4
            out.append(f"I\t{ {0x86: 'c', 0x87: 'l', 0x88: 'r'}[c] }\thbm{number}.bmp")
        elif c == 0x89:
            out.append("E")
        elif c in (0xE0, 0xE1):
            topic = struct.unpack_from("<I", ld1, i)[0]
            i += 4
            out.append(("U" if c == 0xE0 else "J") + f"\t{topic}")
        elif c == 0x8B:
            out.append("X\t\xa0")
        elif c == 0x8C:
            out.append("X\t-")
        else:
            raise ValueError(f"unknown help command 0x{c:02x}")
        emit_text()


def rle(data: bytes, size: int) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data) and len(out) < size:
        n = data[i]
        i += 1
        if n & 0x80:
            out += data[i: i + (n & 0x7F)]
            i += n & 0x7F
        else:
            out += bytes([data[i]]) * n
            i += 1
    return bytes(out[:size]).ljust(size, b"\0")


def lz77(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        flags = data[i]
        i += 1
        for bit in range(8):
            if i >= len(data):
                break
            if flags & (1 << bit):
                word = data[i] | (data[i + 1] << 8)
                i += 2
                length = (word >> 12) + 3
                distance = (word & 0x0FFF) + 1
                for _ in range(length):
                    out.append(out[-distance])
            else:
                out.append(data[i])
                i += 1
    return bytes(out)


def picture(data: bytes) -> bytes | None:
    magic, count = struct.unpack_from("<HH", data, 0)
    if magic not in (0x506C, 0x706C) or count < 1:
        return None
    start = struct.unpack_from("<I", data, 4)[0]
    kind, packing = data[start], data[start + 1]
    if kind not in (5, 6):
        return None
    i = start + 2
    _, i = cu_long(data, i)
    _, i = cu_long(data, i)
    _, i = cu_short(data, i)
    bits, i = cu_short(data, i)
    width, i = cu_long(data, i)
    height, i = cu_long(data, i)
    colors, i = cu_long(data, i)
    _, i = cu_long(data, i)
    compressed, i = cu_long(data, i)
    _, i = cu_long(data, i)
    offset = struct.unpack_from("<I", data, i)[0]
    i += 8
    if kind == 6:
        palette_count = colors or (1 << bits if bits <= 8 else 0)
        palette = data[i: i + 4 * palette_count]
    else:
        palette_count = 2 if bits == 1 else 0
        palette = b"\0\0\0\0\xff\xff\xff\0" if bits == 1 else b""
    stride = ((width * bits + 31) // 32) * 4
    raw = data[start + offset: start + offset + compressed]
    if packing in (2, 3):
        raw = lz77(raw)
    if packing in (1, 3):
        raw = rle(raw, stride * height)
    raw = raw[: stride * height].ljust(stride * height, b"\0")
    header = struct.pack("<IiiHHIIiiII", 40, width, height, 1, bits, 0, len(raw), 3780, 3780,
                         palette_count, 0)
    body = header + palette + raw
    return struct.pack("<2sIHHI", b"BM", 14 + len(body), 0, 0, 14 + 40 + len(palette)) + body


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("help", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    files = read_files(args.help.read_bytes())
    phrases = Phrases(files["|Phrases"])
    stream = topic_stream(files["|TOPIC"])
    out: list[str] = []

    fonts = files["|FONT"]
    names, descriptors, names_offset, descriptors_offset = struct.unpack_from("<4H", fonts, 0)
    face_size = (descriptors_offset - names_offset) // max(names, 1)
    faces = [fonts[names_offset + k * face_size: names_offset + (k + 1) * face_size]
             .split(b"\0")[0].decode("latin1") for k in range(names)]
    for k in range(descriptors):
        attr, half, _, face, r, g, b = struct.unpack_from("<BBBH3B", fonts,
                                                          descriptors_offset + 11 * k)
        out.append(f"N\t{k}\t{attr}\t{half}\t{faces[face] if face < len(faces) else ''}"
                   f"\t{r}\t{g}\t{b}")

    tomap = files["|TOMAP"]
    topic_offsets = list(struct.unpack_from(f"<{len(tomap) // 4}I", tomap, 0))
    number_of = {offset: number for number, offset in enumerate(topic_offsets)}

    position = struct.unpack_from("<i", files["|TOPIC"], 4)[0] - BLOCK_HEADER
    while position + 21 <= len(stream):
        size, _, _, _, length1, kind = struct.unpack_from("<iiiiiB", stream, position)
        if size <= 0:
            break
        ld1 = stream[position + 21: position + length1]
        ld2 = phrases.expand(stream[position + length1: position + size])
        if kind == 2:
            number = number_of.get(absolute(position), -1)
            out.append(f"T\t{number}\t{clean(ld2.split(b'\\0')[0])}")
        elif kind == 1:
            paragraph(ld1, ld2, out)
        position += size

    keywords = files["|KWDATA"]
    for page, levels in btree_leaves(files["|KWBTREE"]):
        count = struct.unpack_from("<H", page, 2)[0]
        cursor = 8
        for _ in range(count):
            end = page.index(b"\0", cursor)
            word = page[cursor:end].decode("cp1252")
            hits, data_offset = struct.unpack_from("<HI", page, end + 1)
            cursor = end + 7
            for k in range(hits):
                topic = struct.unpack_from("<I", keywords, data_offset + 4 * k)[0]
                out.append(f"K\t{word}\t{number_of.get(topic, -1)}")

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "help.txt").write_text("\n".join(out) + "\n", encoding="cp1252",
                                           errors="replace")
    for name, data in files.items():
        if name.startswith("bm"):
            bmp = picture(data)
            if bmp:
                (args.output / f"h{name}.bmp").write_bytes(bmp)


if __name__ == "__main__":
    main()
