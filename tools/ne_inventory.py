#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Produce a deterministic inventory of a 16-bit Windows NE executable."""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
import sys


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def counted(data: bytes, offset: int) -> tuple[str, int]:
    length = data[offset]
    end = offset + 1 + length
    return data[offset + 1 : end].decode("cp1252", errors="replace"), end


def name_table(data: bytes, offset: int) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    while offset < len(data) and data[offset]:
        name, next_offset = counted(data, offset)
        ordinal = u16(data, next_offset)
        result.append({"name": name, "ordinal": ordinal})
        offset = next_offset + 2
    return result


def resource_name(data: bytes, table: int, value: int) -> str | int:
    if value & 0x8000:
        return value & 0x7FFF
    name, _ = counted(data, table + value)
    return name


def entry_table(data: bytes, offset: int, size: int) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    cursor = offset
    end = offset + size
    ordinal = 1
    while cursor < end:
        count = data[cursor]
        if count == 0:
            break
        segment = data[cursor + 1]
        cursor += 2
        if segment == 0:
            ordinal += count
            continue
        for _ in range(count):
            flags = data[cursor]
            if segment == 0xFF:
                target_segment = data[cursor + 3]
                target_offset = u16(data, cursor + 4)
                cursor += 6
                movable = True
            else:
                target_segment = segment
                target_offset = u16(data, cursor + 1)
                cursor += 3
                movable = False
            entries.append(
                {
                    "ordinal": ordinal,
                    "segment": target_segment,
                    "offset": target_offset,
                    "flags": f"0x{flags:02x}",
                    "movable": movable,
                }
            )
            ordinal += 1
    return entries


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT.EXE OUTPUT.json", file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    data = source.read_bytes()
    if data[:2] != b"MZ":
        raise SystemExit("missing MZ header")
    ne = u32(data, 0x3C)
    if data[ne : ne + 2] != b"NE":
        raise SystemExit("missing NE header")

    entry_offset = ne + u16(data, ne + 0x04)
    entry_size = u16(data, ne + 0x06)
    segment_count = u16(data, ne + 0x1C)
    module_count = u16(data, ne + 0x1E)
    alignment = u16(data, ne + 0x32)
    segment_table = ne + u16(data, ne + 0x22)
    resource_table = ne + u16(data, ne + 0x24)
    resident_table = ne + u16(data, ne + 0x26)
    module_table = ne + u16(data, ne + 0x28)
    imported_table = ne + u16(data, ne + 0x2A)

    modules = []
    for index in range(module_count):
        name_offset = u16(data, module_table + index * 2)
        module, _ = counted(data, imported_table + name_offset)
        modules.append(module)

    segments = []
    relocations = []
    for index in range(segment_count):
        entry = segment_table + index * 8
        sector, length, flags, minimum = struct.unpack_from("<HHHH", data, entry)
        segments.append(
            {
                "number": index + 1,
                "file_offset": sector << alignment,
                "file_size": length or 0x10000,
                "minimum_allocation": minimum or 0x10000,
                "flags": f"0x{flags:04x}",
                "kind": "data" if flags & 1 else "code",
                "has_relocations": bool(flags & 0x0100),
            }
        )
        if flags & 0x0100:
            relocation_cursor = (sector << alignment) + (length or 0x10000)
            relocation_count = u16(data, relocation_cursor)
            relocation_cursor += 2
            for _ in range(relocation_count):
                source_type, target_flags, source_offset, target1, target2 = struct.unpack_from(
                    "<BBHHH", data, relocation_cursor
                )
                record: dict[str, object] = {
                    "segment": index + 1,
                    "source_offset": source_offset,
                    "source_type": source_type,
                    "flags": f"0x{target_flags:02x}",
                }
                sites = []
                site = source_offset
                while site != 0xFFFF and site not in sites:
                    if site + 2 > (length or 0x10000):
                        break
                    sites.append(site)
                    site = u16(data, (sector << alignment) + site)
                record["fixup_sites"] = sites
                target_kind = target_flags & 3
                if target_kind == 0:
                    record["target"] = {
                        "kind": "internal",
                        "segment": target1,
                        "offset_or_ordinal": target2,
                    }
                elif target_kind in (1, 2):
                    module = modules[target1 - 1] if 0 < target1 <= len(modules) else target1
                    if target_kind == 1:
                        symbol: str | int = target2
                    else:
                        symbol, _ = counted(data, imported_table + target2)
                    record["target"] = {
                        "kind": "import_ordinal" if target_kind == 1 else "import_name",
                        "module": module,
                        "symbol": symbol,
                    }
                else:
                    record["target"] = {"kind": "os_fixup", "value": target1, "extra": target2}
                relocations.append(record)
                relocation_cursor += 8

    resource_shift = u16(data, resource_table)
    cursor = resource_table + 2
    resources = []
    while (type_value := u16(data, cursor)) != 0:
        count = u16(data, cursor + 2)
        cursor += 8
        type_name = resource_name(data, resource_table, type_value)
        for _ in range(count):
            offset, length, flags, ident, handle, usage = struct.unpack_from(
                "<HHHHHH", data, cursor
            )
            resources.append(
                {
                    "type": type_name,
                    "id": resource_name(data, resource_table, ident),
                    "file_offset": offset << resource_shift,
                    "size": length << resource_shift,
                    "flags": f"0x{flags:04x}",
                    "handle": handle,
                    "usage": usage,
                }
            )
            cursor += 12

    csip = u32(data, ne + 0x14)
    sssp = u32(data, ne + 0x18)
    result = {
        "file": source.name,
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "ne_offset": ne,
        "linker_version": f"{data[ne + 2]}.{data[ne + 3]}",
        "target_os": data[ne + 0x36],
        "expected_windows_version": f"{data[ne + 0x3F]}.{data[ne + 0x3E]}",
        "entry": {"segment": csip >> 16, "offset": csip & 0xFFFF},
        "initial_stack": {"segment": sssp >> 16, "offset": sssp & 0xFFFF},
        "automatic_data_segment": u16(data, ne + 0x0E),
        "heap_size": u16(data, ne + 0x10),
        "stack_size": u16(data, ne + 0x12),
        "segment_alignment_shift": alignment,
        "segments": segments,
        "entries": entry_table(data, entry_offset, entry_size),
        "relocations": relocations,
        "resources": resources,
        "import_modules": modules,
        "resident_names": name_table(data, resident_table),
        "nonresident_names": name_table(data, u32(data, ne + 0x2C)),
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"wrote {destination}")
    print(f"{len(segments)} segments, {len(resources)} resources, {len(modules)} modules")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
