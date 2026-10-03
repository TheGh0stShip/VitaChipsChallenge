#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Disassemble NE code segments with relocations resolved.

Listings are derived from the copyrighted reference executable, so write them
under reference/ or a scratch directory, never into tracked files.

Usage: ne_disasm.py CHIPS.EXE docs/reference-inventory.json OUTDIR
"""

from __future__ import annotations

import json
import pathlib
import re
import subprocess
import sys
import tempfile

# Win16 import ordinals referenced by CHIPS.EXE. Names marked "?" are
# unverified and must be confirmed from call context before relying on them.
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
        78: "InflateRect?", 85: "DrawIcon", 87: "DialogBox",
        88: "EndDialog", 91: "GetDlgItem", 92: "SetDlgItemText",
        93: "GetDlgItemText", 95: "GetDlgItemInt",
        101: "SendDlgItemMessage", 102: "AdjustWindowRect",
        104: "MessageBeep", 106: "GetKeyState", 107: "DefWindowProc",
        109: "PeekMessage", 110: "PostMessage", 111: "SendMessage",
        113: "TranslateMessage", 114: "DispatchMessage",
        124: "UpdateWindow", 125: "InvalidateRect", 127: "ValidateRect?",
        133: "GetWindowWord", 134: "SetWindowWord", 150: "LoadMenu",
        154: "CheckMenuItem", 155: "EnableMenuItem", 159: "GetSubMenu",
        160: "DrawMenuBar", 173: "LoadCursor", 174: "LoadIcon",
        175: "LoadBitmap", 177: "LoadAccelerators",
        178: "TranslateAccelerator", 420: "wsprintf", 471: "lstrcmpi",
    },
    "GDI": {
        1: "SetBkColor", 2: "SetBkMode", 9: "SetTextColor", 29: "PatBlt?",
        34: "BitBlt", 35: "StretchBlt?", 45: "SelectObject",
        52: "CreateCompatibleDC", 57: "CreateFontIndirect",
        68: "DeleteDC", 69: "DeleteObject", 80: "GetDeviceCaps",
        82: "GetObject", 87: "GetStockObject", 93: "GetTextMetrics",
        128: "MulDiv", 443: "SetDIBitsToDevice?",
    },
    "WEP4UTIL": {
        2: "FCHKWEPVERS", 4: "WEPABOUT2", 5: "WEPHELP",
        103: "CENTERHWND", 1202: "GRAYDLGPROC",
    },
}


def target_name(target: dict) -> str:
    if target["kind"] == "internal":
        return f"seg{target['segment']}"
    module = target["module"]
    symbol = target["symbol"]
    return f"{module}.{IMPORTS.get(module, {}).get(symbol, f'#{symbol}')}"


def main() -> None:
    exe, inventory_path, out = map(pathlib.Path, sys.argv[1:4])
    data = exe.read_bytes()
    inventory = json.loads(inventory_path.read_text())
    out.mkdir(parents=True, exist_ok=True)
    sites: dict[int, dict[int, dict]] = {}
    for record in inventory["relocations"]:
        for site in record["fixup_sites"]:
            sites.setdefault(record["segment"], {})[site] = record
    line_re = re.compile(r"^([0-9A-F]{8})\s+([0-9A-F]+)\s+(.*)$")
    for segment in inventory["segments"]:
        if segment["kind"] != "code":
            continue
        number = segment["number"]
        code = data[segment["file_offset"]:segment["file_offset"] + segment["file_size"]]
        with tempfile.NamedTemporaryFile() as handle:
            handle.write(code)
            handle.flush()
            listing = subprocess.run(["ndisasm", "-b16", handle.name],
                                     check=True, capture_output=True,
                                     text=True).stdout
        lines = []
        for line in listing.splitlines():
            match = line_re.match(line)
            if not match:
                lines.append(line)
                continue
            start = int(match.group(1), 16)
            length = len(match.group(2)) // 2
            notes = []
            for site in range(start, start + length):
                record = sites.get(number, {}).get(site)
                if not record:
                    continue
                name = target_name(record["target"])
                if record["target"]["kind"] == "internal" and record["source_type"] == 2:
                    # Selector fixup: the instruction's offset word is real.
                    offset = int.from_bytes(code[site - 2:site], "little")
                    name = f"{record['target']['segment']}:{offset:04X}"
                notes.append(name)
            text = f"{number}:{start:04X}  {match.group(3)}"
            if notes:
                text = f"{text:<48} ; -> {', '.join(notes)}"
            lines.append(text)
        (out / f"seg{number}.asm").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
