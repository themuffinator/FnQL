#!/usr/bin/env python3
"""Compose FnQL key art and banners from FnQL screenshots.

    python promo/tools/compose_art.py <out-dir> <hero.png> <alt.png> <thumb.png>

hero: action shot for the key art, link preview and Steam guide header.
alt: a calm shot for the wide headers (keep the Quake Live logo out of frame).
thumb: a shot with a player near the centre, for the video thumbnail.
Needs Pillow and the Montserrat and Bahnschrift fonts (set FNQL_FONTS_DIR if
they aren't in the Windows fonts folder).

The 2026-11-17 set used, from capture_shots.py output:
hero fnql-act-hiddenfortress-a, alt fnql-glx-overkill-a,
thumb fnql-hl-thunderstruck-c.
"""
import os
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageEnhance

REPO = Path(__file__).resolve().parents[2]
LOGO = Image.open(REPO / "misc/branding/fnql-logo.png").convert("RGBA")
ICON = Image.open(REPO / "misc/branding/icons/fnql-1024.png").convert("RGBA")
FONTS = Path(os.environ.get("FNQL_FONTS_DIR", r"C:\Windows\Fonts"))
HEAD = str(FONTS / "Montserrat-ExtraBoldItalic.ttf")
SUB = str(FONTS / "bahnschrift.ttf")
RED = (208, 20, 27)
BRIGHT = (255, 59, 48)
WHITE = (255, 255, 255)


def cover(im, w, h, focus=(0.5, 0.5)):
    """Scale-to-cover then crop around a focus point."""
    s = max(w / im.width, h / im.height)
    im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
    x = int((im.width - w) * focus[0])
    y = int((im.height - h) * focus[1])
    return im.crop((x, y, x + w, y + h))


def grade(im, dark=0.0):
    im = ImageEnhance.Contrast(im).enhance(1.06)
    if dark:
        im = ImageEnhance.Brightness(im).enhance(1 - dark)
    return im


def side_fade(w, h, start, end, strength=235, from_left=True):
    """Horizontal black fade, opaque at the chosen edge."""
    g = Image.new("L", (w, 1))
    for x in range(w):
        t = x / w if from_left else 1 - x / w
        if t <= start:
            a = strength
        elif t >= end:
            a = 0
        else:
            a = int(strength * (1 - (t - start) / (end - start)) ** 1.6)
        g.putpixel((x, 0), a)
    return g.resize((w, h))


def bottom_fade(w, h, frac, strength=230):
    g = Image.new("L", (1, h))
    top = int(h * (1 - frac))
    for y in range(h):
        g.putpixel((0, y), 0 if y < top else int(strength * ((y - top) / (h - top)) ** 1.4))
    return g.resize((w, h))


def darken(im, mask):
    black = Image.new("RGB", im.size, (6, 6, 8))
    return Image.composite(black, im, mask)


def stripes(im, x0, width, color=RED, n=3, gap=None, slant=None):
    """Diagonal accent stripes, like the social preview's edge bars."""
    d = ImageDraw.Draw(im, "RGBA")
    h = im.height
    slant = slant if slant is not None else h * 0.35
    gap = gap or width * 1.6
    for i in range(n):
        x = x0 + i * gap
        a = 255 if i == 0 else 150 - i * 30
        d.polygon([(x + slant, 0), (x + slant + width, 0), (x + width, h), (x, h)],
                  fill=color + (a,))


def text(im, xy, s, size, font=HEAD, fill=WHITE, spacing=0, anchor="la", shadow=True):
    d = ImageDraw.Draw(im, "RGBA")
    f = ImageFont.truetype(font, size)
    if spacing:
        x, y = xy
        if anchor[0] == "m":
            total = sum(d.textlength(c, font=f) + spacing for c in s) - spacing
            x -= total / 2
        for c in s:
            if shadow:
                d.text((x + 2, y + 2), c, font=f, fill=(0, 0, 0, 160), anchor="l" + anchor[1])
            d.text((x, y), c, font=f, fill=fill, anchor="l" + anchor[1])
            x += d.textlength(c, font=f) + spacing
        return
    if shadow:
        d.text((xy[0] + 3, xy[1] + 3), s, font=f, fill=(0, 0, 0, 170), anchor=anchor)
    d.text(xy, s, font=f, fill=fill, anchor=anchor)


def paste_logo(im, width, xy, glow=True):
    lg = LOGO.resize((width, round(LOGO.height * width / LOGO.width)), Image.LANCZOS)
    if glow:
        a = lg.getchannel("A").filter(ImageFilter.GaussianBlur(width / 30))
        sh = Image.new("RGBA", lg.size, (0, 0, 0, 0))
        sh.putalpha(a.point(lambda v: int(v * 0.8)))
        im.paste(sh, (xy[0] + 6, xy[1] + 10), sh)
    im.paste(lg, xy, lg)
    return lg.size


def keyart(bg, out, w=1920, h=1080, focus=(0.6, 0.5)):
    im = grade(cover(bg, w, h, focus))
    im = darken(im, side_fade(w, h, 0.18, 0.62, 225))
    im = darken(im, bottom_fade(w, h, 0.35, 160))
    stripes(im, w - 260, 34, n=3, slant=-h * 0.22)
    s = w / 1920
    lw, lh = paste_logo(im, int(760 * s), (int(110 * s), int(250 * s)))
    y = int(250 * s) + lh + int(40 * s)
    text(im, (int(122 * s), y), "THE QUAKE LIVE YOU OWN.", int(62 * s), fill=WHITE)
    text(im, (int(122 * s), y + int(78 * s)), "REBUILT ENGINE.", int(62 * s), fill=BRIGHT)
    text(im, (int(126 * s), y + int(178 * s)),
         "GLX · VULKAN · HDR · RAW INPUT · HRTF · OPEN SOURCE", int(26 * s), font=SUB,
         fill=(225, 225, 230), spacing=int(4 * s))
    text(im, (int(126 * s), h - int(70 * s)), "github.com/themuffinator/FnQL", int(28 * s),
         font=SUB, fill=(200, 200, 205), spacing=int(2 * s))
    text(im, (w - int(40 * s), h - int(30 * s)),
         "Unofficial. Not affiliated with id Software, Bethesda or Valve. Requires Quake Live on Steam.",
         int(15 * s), font=SUB, fill=(160, 160, 165), anchor="rs", shadow=False)
    im.save(out, quality=92, subsampling=0)


def banner(bg, out, w, h, focus=(0.5, 0.5), logo_frac=0.42, tagline=True, small=False):
    im = grade(cover(bg, w, h, focus))
    im = darken(im, side_fade(w, h, 0.10, 0.70, 215))
    stripes(im, w - int(h * 0.55), max(8, int(h * 0.05)), n=3, slant=-h * 0.3)
    lw_target = int(w * logo_frac)
    lw, lh = paste_logo(im, lw_target, (int(w * 0.05), int((h - lw_target * LOGO.height / LOGO.width) / 2 - (h * 0.08 if tagline else 0))))
    if tagline:
        y = int((h + lh) / 2 - h * 0.08) + int(h * 0.05)
        size = max(14, int(h * (0.075 if small else 0.065)))
        text(im, (int(w * 0.05) + 8, y), "MODERN ENGINE · RETAIL RULES", size, font=SUB,
             fill=(230, 230, 235), spacing=max(1, size // 6))
    im.save(out, quality=92, subsampling=0)


def thumb(bg, out, w=1280, h=720):
    im = grade(cover(bg, w, h, (0.35, 0.5)))
    im = darken(im, bottom_fade(w, h, 0.38, 235))
    im = darken(im, side_fade(w, h, 0.0, 0.45, 150))
    stripes(im, w - 150, 26, n=2, slant=-h * 0.25)
    paste_logo(im, 430, (44, 40))
    text(im, (48, h - 172), "QUAKE LIVE, REBUILT", 78)
    text(im, (52, h - 66), "FREE · OPEN SOURCE · JOINS RETAIL SERVERS", 28, font=SUB,
         fill=BRIGHT, spacing=3)
    im.save(out, quality=92, subsampling=0)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    for font in (HEAD, SUB):
        if not Path(font).is_file():
            sys.exit(f"missing font {font}; set FNQL_FONTS_DIR")
    out = Path(sys.argv[1])
    hero, alt, th = (Image.open(p).convert("RGB") for p in sys.argv[2:5])
    (out / "keyart").mkdir(parents=True, exist_ok=True)
    (out / "banners").mkdir(parents=True, exist_ok=True)
    keyart(hero, out / "keyart" / "fnql-keyart-1920x1080.jpg")
    keyart(hero, out / "keyart" / "fnql-keyart-1280x720.jpg", 1280, 720)
    banner(alt, out / "banners" / "fnql-moddb-header-1200x300.jpg", 1200, 300, (0.5, 0.45), 0.36)
    banner(alt, out / "banners" / "fnql-x-header-1500x500.jpg", 1500, 500, (0.5, 0.5), 0.36)
    banner(hero, out / "banners" / "fnql-og-1200x630.jpg", 1200, 630, (0.6, 0.5), 0.5)
    banner(alt, out / "banners" / "fnql-discord-banner-960x540.jpg", 960, 540, (0.5, 0.5), 0.55, small=True)
    banner(hero, out / "banners" / "fnql-steam-guide-1024x512.jpg", 1024, 512, (0.6, 0.5), 0.5)
    thumb(th, out / "banners" / "fnql-youtube-thumb-1280x720.jpg")


if __name__ == "__main__":
    main()
