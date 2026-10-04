# Local reference files

Place the user-owned `chips_challenge.zip` here. It is intentionally ignored by
Git. The supported reference archive has SHA-256:

```text
ffbb83dc4ca5cc9e8cbf78271b44a42ea4e7db2f4f8d1953383390acf94e7ddf
```

For a personal build, run `python3 tools/build_vita.py`. For analysis:

```sh
python3 tools/extract_reference.py reference/chips_challenge.zip
python3 tools/ne_disasm.py reference/extracted/CHIPS.EXE \
  docs/reference-inventory.json reference/disasm
```

The Windows executable fingerprint is:

```text
8e26acd67cf120bd5b512de4b4e78b80aca1579413cd04f3b2b68909a375866c  CHIPS.EXE
```

Reference files are inputs for analysis and personal builds. They are not part
of the public source distribution.

