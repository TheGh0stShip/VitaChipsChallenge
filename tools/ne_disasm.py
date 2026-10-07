#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Disassemble NE code segments with relocations resolved.

Listings are derived from the copyrighted reference executable, so write them
under reference/ or a scratch directory, never into tracked files.

Usage: ne_disasm.py CHIPS.EXE docs/reference-inventory.json OUTDIR

Each code segment is written to OUTDIR/segN.asm. Lines are prefixed with
"segment:offset", relocated operands are annotated "; -> target", a blank
separator follows every RETF/RET, and offsets reached by a near call or by an
internal far-call fixup get a "segN_XXXX:" label.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
from typing import Any

# Win16 import ordinals referenced by CHIPS.EXE (Windows 3.1 export tables).
# Unknown ordinals are printed as "#N".
IMPORTS = {
    "KERNEL": {
        1: "FatalExit", 3: "GetVersion", 5: "LocalAlloc", 6: "LocalReAlloc",
        7: "LocalFree", 10: "LocalSize", 15: "GlobalAlloc",
        16: "GlobalReAlloc", 17: "GlobalFree", 18: "GlobalLock",
        19: "GlobalUnlock", 20: "GlobalSize", 23: "LockSegment",
        24: "UnlockSegment", 30: "WaitEvent", 49: "GetModuleFileName",
        50: "GetProcAddress", 51: "MakeProcInstance",
        52: "FreeProcInstance", 60: "FindResource", 61: "LoadResource",
        62: "LockResource", 63: "FreeResource", 74: "OpenFile",
        81: "_lclose", 82: "_lread", 84: "_llseek", 88: "lstrcpy",
        90: "lstrlen", 91: "InitTask", 95: "LoadLibrary",
        96: "FreeLibrary", 102: "DOS3Call", 107: "SetErrorMode",
        127: "GetPrivateProfileInt", 128: "GetPrivateProfileString",
        129: "WritePrivateProfileString", 131: "GetDOSEnvironment",
        134: "GetWindowsDirectory", 137: "FatalAppExit",
        178: "__WINFLAGS",
    },
    "USER": {
        1: "MessageBox", 5: "InitApp", 6: "PostQuitMessage", 10: "SetTimer",
        12: "KillTimer", 15: "GetCurrentTime", 18: "SetCapture",
        19: "ReleaseCapture", 22: "SetFocus", 31: "IsIconic",
        33: "GetClientRect", 34: "EnableWindow", 37: "SetWindowText",
        39: "BeginPaint", 40: "EndPaint", 41: "CreateWindow",
        42: "ShowWindow", 53: "DestroyWindow", 57: "RegisterClass",
        61: "ScrollWindow", 66: "GetDC", 68: "ReleaseDC", 69: "SetCursor",
        78: "InflateRect", 84: "DrawIcon", 85: "DrawText", 87: "DialogBox",
        88: "EndDialog", 91: "GetDlgItem", 92: "SetDlgItemText",
        93: "GetDlgItemText", 95: "GetDlgItemInt",
        101: "SendDlgItemMessage", 102: "AdjustWindowRect",
        104: "MessageBeep", 106: "GetKeyState", 107: "DefWindowProc",
        109: "PeekMessage", 110: "PostMessage", 111: "SendMessage",
        113: "TranslateMessage", 114: "DispatchMessage",
        124: "UpdateWindow", 125: "InvalidateRect", 127: "ValidateRect",
        133: "GetWindowWord", 134: "SetWindowWord", 150: "LoadMenu",
        154: "CheckMenuItem", 155: "EnableMenuItem", 159: "GetSubMenu",
        160: "DrawMenuBar", 173: "LoadCursor", 174: "LoadIcon",
        175: "LoadBitmap", 177: "LoadAccelerators",
        178: "TranslateAccelerator", 420: "wsprintf", 471: "lstrcmpi",
        483: "SystemParametersInfo",
    },
    "GDI": {
        1: "SetBkColor", 2: "SetBkMode", 9: "SetTextColor", 29: "PatBlt",
        34: "BitBlt", 35: "StretchBlt", 45: "SelectObject",
        52: "CreateCompatibleDC", 57: "CreateFontIndirect",
        68: "DeleteDC", 69: "DeleteObject", 80: "GetDeviceCaps",
        82: "GetObject", 87: "GetStockObject", 93: "GetTextMetrics",
        128: "MulDiv", 443: "SetDIBitsToDevice",
    },
    "WEP4UTIL": {
        2: "FCHKWEPVERS", 4: "WEPABOUT2", 5: "WEPHELP",
        103: "CENTERHWND", 1202: "GRAYDLGPROC",
    },
}


Record = dict[str, Any]
LINE_RE = re.compile(r"^([0-9A-F]{8})\s+([0-9A-F]+)\s+(.*)$")
NEAR_CALL_RE = re.compile(r"^call (?:near )?0x([0-9a-f]+)$")
RETURN_RE = re.compile(r"^(?:retf|ret)\b")


def target_name(target: Record) -> str:
    if target["kind"] == "internal":
        return f"seg{target['segment']}"
    module = target["module"]
    symbol = target["symbol"]
    return f"{module}.{IMPORTS.get(module, {}).get(symbol, f'#{symbol}')}"


def disassemble(code: bytes, ndisasm: str) -> str:
    with tempfile.NamedTemporaryFile() as handle:
        handle.write(code)
        handle.flush()
        return subprocess.run([ndisasm, "-b16", handle.name], check=True,
                              capture_output=True, text=True).stdout


def far_targets(inventory: Record, data: bytes) -> dict[int, set[int]]:
    """Collect segment:offset entry points referenced by internal fixups."""
    result: dict[int, set[int]] = {}
    segments = {s["number"]: s for s in inventory["segments"]}
    for record in inventory["relocations"]:
        target = record["target"]
        if target["kind"] != "internal":
            continue
        if record["source_type"] == 2:
            seg = segments[record["segment"]]
            for site in record["fixup_sites"]:
                at = seg["file_offset"] + site
                offset = int.from_bytes(data[at - 2:at], "little")
                result.setdefault(target["segment"], set()).add(offset)
        elif "offset" in target:
            result.setdefault(target["segment"], set()).add(target["offset"])
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Disassemble NE code segments with relocations resolved.")
    parser.add_argument("exe", type=pathlib.Path, help="NE executable")
    parser.add_argument("inventory", type=pathlib.Path,
                        help="JSON produced by ne_inventory.py")
    parser.add_argument("outdir", type=pathlib.Path,
                        help="output directory (keep outside tracked files)")
    parser.add_argument("--ndisasm", default="ndisasm",
                        help="ndisasm executable (default: %(default)s)")
    args = parser.parse_args(argv)
    ndisasm = shutil.which(args.ndisasm)
    if ndisasm is None:
        parser.exit(1, f"error: '{args.ndisasm}' not found; install NASM "
                       "(e.g. apt install nasm) or pass --ndisasm PATH\n")
    for path in (args.exe, args.inventory):
        if not path.is_file():
            parser.error(f"file not found: {path}")
    data = args.exe.read_bytes()
    inventory: Record = json.loads(args.inventory.read_text())
    out: pathlib.Path = args.outdir
    out.mkdir(parents=True, exist_ok=True)
    sites: dict[int, dict[int, Record]] = {}
    for record in inventory["relocations"]:
        for site in record["fixup_sites"]:
            sites.setdefault(record["segment"], {})[site] = record
    entries = far_targets(inventory, data)
    for segment in inventory["segments"]:
        if segment["kind"] != "code":
            continue
        number = segment["number"]
        code = data[segment["file_offset"]:segment["file_offset"] + segment["file_size"]]
        parsed: list[tuple[int, int, str] | str] = []
        labels = set(entries.get(number, set()))
        for line in disassemble(code, ndisasm).splitlines():
            match = LINE_RE.match(line)
            if not match:
                parsed.append(line)
                continue
            start = int(match.group(1), 16)
            parsed.append((start, len(match.group(2)) // 2, match.group(3)))
            call = NEAR_CALL_RE.match(match.group(3))
            if call and start not in sites.get(number, {}):
                labels.add(int(call.group(1), 16))
        lines: list[str] = []
        for item in parsed:
            if isinstance(item, str):
                lines.append(item)
                continue
            start, length, text = item
            if start in labels:
                lines.append(f"seg{number}_{start:04X}:")
            notes = []
            for site in range(start, start + length):
                record = sites.get(number, {}).get(site)
                if not record:
                    continue
                name = target_name(record["target"])
                if record["target"]["kind"] == "internal" and record["source_type"] == 2:
                    # Selector fixup: the instruction's offset word is real.
                    offset = int.from_bytes(code[site - 2:site], "little")
                    name = f"seg{record['target']['segment']}_{offset:04X}"
                notes.append(name)
            call = NEAR_CALL_RE.match(text)
            if call and not notes:
                notes.append(f"seg{number}_{int(call.group(1), 16):04X}")
            out_line = f"{number}:{start:04X}  {text}"
            if notes:
                out_line = f"{out_line:<48} ; -> {', '.join(notes)}"
            lines.append(out_line)
            if RETURN_RE.match(text):
                lines.append("")
        (out / f"seg{number}.asm").write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
