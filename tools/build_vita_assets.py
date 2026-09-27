#!/usr/bin/env python3
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("bitmaps", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    sheet = Image.open(args.bitmaps / "OBJ32_4.bmp").convert("RGB")
    circuit = Image.open(args.bitmaps / "BACKGROUND.bmp").convert("RGB")
    ending = Image.open(args.bitmaps / "CHIPEND.bmp").convert("RGB")

    player_index = 108
    player = sheet.crop(((player_index % 13) * 32, (player_index // 13) * 32,
                         (player_index % 13 + 1) * 32, (player_index // 13 + 1) * 32))
    icon = cover(circuit, (128, 128))
    icon.paste(player.resize((96, 96), Image.Resampling.NEAREST), (16, 16))
    icon.save(args.output / "icon0.png", optimize=True)

    background = cover(circuit, (840, 500)).convert("RGB")
    ending_large = ending.resize((430, 430), Image.Resampling.NEAREST)
    background.paste(ending_large, (390, 48))
    title(ImageDraw.Draw(background), (28, 38), "CHIP'S", 70)
    title(ImageDraw.Draw(background), (28, 118), "CHALLENGE", 60)
    title(ImageDraw.Draw(background), (30, 205), "PS VITA", 38)
    background.save(args.output / "bg0.png", optimize=True)

    startup = background.resize((280, 158), Image.Resampling.LANCZOS)
    startup.save(args.output / "startup.png", optimize=True)
    pic = cover(background, (960, 544))
    pic.save(args.output / "pic0.png", optimize=True)

    (args.output / "template.xml").write_text("""<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<livearea style=\"a1\" format-ver=\"01.00\" content-rev=\"1\">
  <livearea-background><image>bg0.png</image></livearea-background>
  <gate><startup-image>startup.png</startup-image></gate>
</livearea>
""", encoding="utf-8")


if __name__ == "__main__":
    main()
