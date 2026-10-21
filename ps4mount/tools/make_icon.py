#!/usr/bin/env python3
"""Draws assets/icon0.png (512x512) for the PS4Mount Media tab shortcut."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

S = 512
img = Image.new('RGB', (S, S))
d = ImageDraw.Draw(img)
for y in range(S):
    t = y / (S - 1)
    d.line([(0, y), (S, y)], fill=(int(18 + 10 * t), int(24 + 20 * t), int(40 + 70 * t)))
bold = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
# USB drive outline
d.rounded_rectangle([196, 70, 316, 120], radius=10, outline=(150, 190, 255), width=8)
d.rounded_rectangle([156, 112, 356, 250], radius=24, fill=(61, 123, 253))
def center(text, size, y, fill):
    font = ImageFont.truetype(bold, size)
    w = d.textlength(text, font=font)
    d.text(((S - w) / 2, y), text, font=font, fill=fill)
center('PS4', 150, 260, (255, 255, 255))
center('MOUNT', 60, 420, (170, 200, 255))
out = Path(__file__).resolve().parent.parent / 'assets' / 'icon0.png'
out.parent.mkdir(exist_ok=True)
img.save(out, optimize=True)
