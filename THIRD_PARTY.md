# Third-party material

The GNU GPLv3 license in `LICENSE` covers the source code and original project
documentation authored for VitaChipsChallenge. It does not grant rights to
third-party trademarks, game data, executable resources, retail artwork, or
screenshots merely because those files are present or referenced here.

## Chip's Challenge material

Chip's Challenge names, characters, artwork, level data, audio, and other game
content remain property of their respective rights holders. The verified
reference archive under `reference/` is ignored by Git and is not distributed
as GPL software. Users must supply their own reference archive for personal
builds.

The historical images under `assets/branding/` retain their original rights.
Their individual sources and purpose are recorded in
`assets/branding/README.md`. Vita shell graphics under `assets/vita/` are
compositions derived from that retail imagery and original Windows resources;
the GPL does not relicense those underlying materials.

## Dependencies and tools

VitaSDK, SDL, Python, Pillow, CMake, Ninja, and Vita3K are separate projects
distributed under their own licenses. Building or testing VitaChipsChallenge
does not change those licenses.

## Fonts

`assets/fonts/LiberationSans-*.ttf` are Liberation Sans 2.1.5 from
<https://github.com/liberationfonts/liberation-fonts>, licensed under the SIL
Open Font License 1.1 (`assets/fonts/LICENSE-LiberationFonts.txt`). They
stand in for the Windows 3.x "MS Sans Serif", Arial, and System fonts named
by the original dialog templates and `DS:0598`, which are not part of the
reference archive. Liberation Sans is metric-compatible with Arial.
