#!/usr/bin/env python3
"""Regenerate the icon and System Settings thumbnails in assets/.

Draws a still of the starfield look (dark #020405 background, white stars with
short tails streaking away from the centre). Requires Pillow.
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets"
BG = (2, 4, 5)


def render(width, height, count, seed=7, ss=4, boost=1.0, tail=1.0):
    rnd = random.Random(seed)
    img = Image.new("RGB", (width * ss, height * ss), BG)
    draw = ImageDraw.Draw(img, "RGBA")
    for _ in range(count):
        z = 0.08 + rnd.random() * 0.92
        x = (rnd.random() - 0.5) * z
        y = (rnd.random() - 0.5) * z
        prox = 1 - z
        hx = width * (0.5 + x / z) * ss
        hy = height * (0.5 + y / z) * ss
        radius = (0.35 + prox * prox * 1.65) * ss * boost
        alpha = int(255 * (0.3 + prox * 0.7))
        old = z + 0.4 * 0.045 * 8 * tail
        tx = width * (0.5 + x / old) * ss
        ty = height * (0.5 + y / old) * ss
        length = math.hypot(hx - tx, hy - ty) or 1
        cap = 18 * tail * ss * boost
        k = min(1, cap / length)
        tx, ty = hx - (hx - tx) * k, hy - (hy - ty) * k
        for seg in range(4):
            a, b = seg / 4, (seg + 1) / 4
            draw.line(
                [(tx + (hx - tx) * a, ty + (hy - ty) * a), (tx + (hx - tx) * b, ty + (hy - ty) * b)],
                fill=(255, 255, 255, int(alpha * 0.32)),
                width=max(1, int(radius * 2 * b)),
            )
        draw.ellipse([hx - radius, hy - radius, hx + radius, hy + radius], fill=(255, 255, 255, alpha))
    return img.resize((width, height), Image.LANCZOS)


def main():
    OUT.mkdir(exist_ok=True)
    render(90, 58, 140, boost=0.6).save(OUT / "thumbnail.png")
    render(180, 116, 220, boost=1.2).save(OUT / "thumbnail@2x.png")
    render(1280, 800, 640, tail=1.5).save(OUT / "preview.png")

    icon = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    mask = Image.new("L", (256, 256), 0)
    ImageDraw.Draw(mask).rounded_rectangle([8, 8, 248, 248], radius=48, fill=255)
    icon.paste(render(256, 256, 120, seed=11, boost=3, tail=1.5), (0, 0), mask)
    icon.save(OUT / "starfield.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    icon.save(OUT / "icon.png")


if __name__ == "__main__":
    main()
