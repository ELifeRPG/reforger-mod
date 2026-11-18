"""Draws the cashier's stamp on the bank papers' on-screen view (256 x 256, transparent), in the style of the cash band's.

The same violet round stamp as on the 10 000 Ŧ band (assets/models/cash/make_band.py), without a bank name: every bank's
papers carry it, so the ring says what it vouches for instead. Tilted and unevenly inked like a real impression.
Run: python3 -I make_paper_stamp.py [output.png]
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SIZE = 256
FONTS = "/usr/share/fonts/noto/"
STAMP = (112, 58, 128)
TEXT = "BANQUE · CAISSE · VÉRIFIÉ · "
RADIUS = 108
TILT = -12


def font(name, size):
    return ImageFont.truetype(FONTS + name, size)


def centred(d, xy, text, f, fill):
    l, t, r, b = d.textbbox((0, 0), text, font=f)
    d.text((xy[0] - (r - l) / 2 - l, xy[1] - (b - t) / 2 - t), text, font=f, fill=fill)


def impression(size):
    """Ink coverage: mostly full, with paler patches and a lighter edge on one side, as a stamp pressed unevenly."""
    mask = Image.new("L", (size, size), 0)
    px = mask.load()
    seed = 11
    for y in range(size):
        for x in range(size):
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            grain = (seed >> 8) % 45
            # Pressed harder towards the lower left.
            slope = 40 * ((x - y) / size)
            px[x, y] = max(0, min(255, 255 - grain - int(slope)))

    return mask.filter(ImageFilter.GaussianBlur(1.2))


def stamp():
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    c = SIZE / 2
    r = RADIUS
    d.ellipse([c - r, c - r, c + r, c + r], outline=(*STAMP, 255), width=7)
    d.ellipse([c - r + 34, c - r + 34, c + r - 34, c + r - 34], outline=(*STAMP, 255), width=3)

    f = font("NotoSans-Bold.ttf", 21)
    for i, ch in enumerate(TEXT):
        a = 2 * math.pi * i / len(TEXT)
        glyph = Image.new("RGBA", (40, 40), (0, 0, 0, 0))
        centred(ImageDraw.Draw(glyph), (20, 21), ch, f, (*STAMP, 255))
        glyph = glyph.rotate(-math.degrees(a), resample=Image.BICUBIC)
        gx = int(c + (r - 19) * math.sin(a) - 20)
        gy = int(c - (r - 19) * math.cos(a) - 20)
        layer.paste(glyph, (gx, gy), glyph)

    centred(d, (c, c), "Ŧ", font("NotoSerif-Bold.ttf", 60), (*STAMP, 255))

    # Uneven ink: the impression's coverage scales the alpha.
    alpha = layer.getchannel("A")
    coverage = impression(SIZE)
    alpha = Image.composite(coverage, Image.new("L", (SIZE, SIZE), 0), alpha)
    layer.putalpha(alpha)
    return layer.rotate(TILT, resample=Image.BICUBIC)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "ELIFE_PaperStamp.png"
    stamp().save(out)
    print("saved", out)


if __name__ == "__main__":
    main()
