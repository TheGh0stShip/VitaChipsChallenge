#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build Vita shell art entirely from the original Windows bitmap resources."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def cover(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    scale = max(size[0] / image.width, size[1] / image.height)
    resized = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.NEAREST)
    left = (resized.width - size[0]) // 2
    top = (resized.height - size[1]) // 2
    return resized.crop((left, top, left + size[0], top + size[1]))


def title(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, size: int) -> None:
    try:
        font = ImageFont.truetype("DejaVuSans-Bold.ttf", size)
    except OSError:
        font = ImageFont.load_default()
    draw.text((xy[0] + 3, xy[1] + 3), text, font=font, fill=(0, 0, 0))
    draw.text(xy, text, font=font, fill=(255, 236, 42), stroke_width=1, stroke_fill=(180, 24, 24))


def vita_png(image):
    """LiveArea and bubble images must be 8-bit palette PNGs; the Vita's
    installer rejects truecolor ones with error 0x8010113D."""
    return image.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT,
                                         dither=Image.Dither.FLOYDSTEINBERG)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("bitmaps", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--branding", type=Path,
                        default=Path("assets/branding"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    sheet = Image.open(args.bitmaps / "OBJ32_4.bmp").convert("RGB")
    circuit = Image.open(args.bitmaps / "BACKGROUND.bmp").convert("RGB")
    ending = Image.open(args.bitmaps / "CHIPEND.bmp").convert("RGB")
    logo = Image.open(args.branding / "chips-challenge-logo.png").convert("RGBA")
    retail = Image.open(args.branding / "chips-challenge-retail.png").convert("RGB")

    player_index = 108
    player = sheet.crop(((player_index // 16) * 32, (player_index % 16) * 32,
                         (player_index // 16 + 1) * 32, (player_index % 16 + 1) * 32))
    icon = cover(retail.crop((0, 0, 395, 514)), (128, 128))
    logo_icon = logo.copy()
    logo_icon.thumbnail((120, 58), Image.Resampling.LANCZOS)
    icon.paste(logo_icon, ((128 - logo_icon.width) // 2, 6), logo_icon)
    icon.paste(player.resize((62, 62), Image.Resampling.NEAREST), (33, 62))
    vita_png(icon).save(args.output / "icon0.png", optimize=True)

    background = cover(circuit, (840, 500)).convert("RGB")
    retail_panel = cover(retail, (470, 403))
    background.paste(retail_panel, (350, 70))
    logo_large = logo.copy()
    logo_large.thumbnail((325, 150), Image.Resampling.LANCZOS)
    background.paste(logo_large, (15, 35), logo_large)
    player_large = player.resize((176, 176), Image.Resampling.NEAREST)
    background.paste(player_large, (83, 210))
    title(ImageDraw.Draw(background), (88, 405), "PS VITA", 38)
    vita_png(background).save(args.output / "bg0.png", optimize=True)

    startup = background.resize((280, 158), Image.Resampling.LANCZOS)
    vita_png(startup).save(args.output / "startup.png", optimize=True)
    pic = cover(background, (960, 544))
    vita_png(pic).save(args.output / "pic0.png", optimize=True)

    (args.output / "template.xml").write_text("""<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<livearea style=\"a1\" format-ver=\"01.00\" content-rev=\"1\">
  <livearea-background><image>bg0.png</image></livearea-background>
  <gate><startup-image>startup.png</startup-image></gate>
</livearea>
""", encoding="utf-8")


if __name__ == "__main__":
    main()
