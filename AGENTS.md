# PS Vita target requirements

The target is little-endian ARMv7-A (Cortex-A9), 32-bit ARM/Thumb, using an
ILP32 ABI. It is not AArch64. Vita objects and libraries must agree on the
floating-point calling convention; inspect ELF attributes rather than inferring
the ABI from NEON or VFP availability.

Development hosts are commonly Linux x86-64 LP64 or Windows x64 LLP64. Disk,
wire, checksum, segmented-address, and token values inherited from the 16-bit
Windows reference must use explicit widths and endian-safe access. Do not pack
all structures globally or store host pointers in reference-format integers.

Host tests are useful but cannot establish correctness on physical Vita
hardware. Release gates must include ARM ELF/ABI checks and a device pass.

# Decompilation provenance

This repository is a clean source reconstruction of the Microsoft Windows 3.x
version of Chip's Challenge. Do not import Tile World, emulator, clone, or
third-party gameplay-engine source. Reference binaries and copyrighted game
data belong under `reference/` and must never be committed. Record executable
addresses and observed behavior for reconstructed routines.

