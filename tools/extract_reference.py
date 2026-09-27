#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate and safely extract the supported Windows reference archive."""

from __future__ import annotations

import hashlib
import pathlib
import sys
import zipfile

ARCHIVE_SHA256 = "ffbb83dc4ca5cc9e8cbf78271b44a42ea4e7db2f4f8d1953383390acf94e7ddf"
FILES = {
    "BLIP2.WAV": "579d70f895fe18c35b37e484cf4ea1266882e6427c7d374db7f1fc8386cddf24",
    "BUMMER.WAV": "df88038798290a13a7934edf2a77d9147ed432f46d15208b0a644aafa0ab48e0",
    "CHIP01.MID": "4ea9de6ca3e63f3534de5672adc1e7eec05dbcbd59859d5f729e546e109b93d2",
    "CHIP02.MID": "775cb2a52624cc256a6047228ab4d6c6b7a588cf3067fd8af89f5a286e2eed25",
    "CHIPS.DAT": "b0a68f21642447385512e0ed9386a4a13a4f7e9a38f909da772104d8e6b5c1fb",
    "CHIPS.EXE": "8e26acd67cf120bd5b512de4b4e78b80aca1579413cd04f3b2b68909a375866c",
    "CHIPS.HLP": "be66ae3cfe738530960d82097df2c9fdf63d2d5263a8cb8173d2e65995a6fb42",
    "CLICK3.WAV": "096f7524b8c5fee09ff5d83d3bb61d58a61288fcf6cf105ba1a1e44d80758b03",
    "DITTY1.WAV": "fdbf2dd8a05665d3c0eb99e66e9775a65bca0dbc154443691e097dd339ff55ff",
    "DOOR.WAV": "51f2b354c00dcf61edb9d645f76f1867b3da14fdb1f606b2269830178521a1bd",
    "OOF3.WAV": "b65e20a521c3a9f1160d1e12c49cd16e81adf97ed1796c8d3093be286073b0eb",
    "POP2.WAV": "42dc9902afa86887749ee0b3bd32140515aaaa56823a879ae989d418a232bcc1",
    "STRIKE.WAV": "3fb206417137e47256c094ab3d3a88c4a0c2eed15ed4b9b480230c2bc18914f2",
    "TELEPORT.WAV": "57d4070a9048c4594cff581c8429fc56d3c5f956e4e1eef3e9ecb56d3418fcd8",
    "WATER2.WAV": "7f38317a459413697539973386dc53bbc6936b07145d5a7aca9f0aa40cc1f433",
    "WEP4UTIL.DLL": "40a405813946caac1aa2ec3e31fa570de1c418f8db43182d614e0bac4e1978cb",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"usage: {sys.argv[0]} ARCHIVE [OUTPUT_DIR]", file=sys.stderr)
        return 2
    archive = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2]) if len(sys.argv) == 3 else archive.parent / "extracted"
    archive_data = archive.read_bytes()
    if digest(archive_data) != ARCHIVE_SHA256:
        raise SystemExit("unsupported chips_challenge.zip fingerprint")

    with zipfile.ZipFile(archive) as source:
        names = source.namelist()
        if len(names) != len(FILES) or set(names) != set(FILES):
            raise SystemExit("archive member list does not match the supported reference")
        output.mkdir(parents=True, exist_ok=True)
        for name in sorted(names):
            if pathlib.PurePosixPath(name).name != name:
                raise SystemExit(f"unsafe archive member: {name}")
            data = source.read(name)
            if digest(data) != FILES[name]:
                raise SystemExit(f"fingerprint mismatch: {name}")
            (output / name).write_bytes(data)
            print(f"{FILES[name]}  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
