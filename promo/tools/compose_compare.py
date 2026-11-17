#!/usr/bin/env python3
"""Build feature-comparison images from capture_shots.py variant frames.

    split  OUT A.png B.png --labels OFF,ON [--title T] [--note N] [--angle 0.18] [--at 0.5]
        Before/after in one frame: A on the left, B on the right of a slanted cut.
    panels OUT A.png B.png [C.png ...] --labels a,b,c [--crop x0,y0,x1,y1] [--title T] [--note N]
        Side-by-side crops of the same frozen frame, one label per panel.
    card   OUT A.png --title T [--note N]
        A single frame with the FnQL logo and a caption bar.

Titles are feature names; notes are the cvar line players type. Uses the same
fonts and accents as compose_art.py.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compose_art import BRIGHT, HEAD, RED, SUB, WHITE, paste_logo, text  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

BAR = (8, 8, 10)


def caption_bar(im: Image.Image, title: str | None, note: str | None, logo: bool = True) -> Image.Image:
    """Append a branded caption bar under the image."""
    if not title and not note:
        return im
    w = im.width
    bh = max(84, w // 16)
    out = Image.new("RGB", (w, im.height + bh), BAR)
    out.paste(im, (0, 0))
    d = ImageDraw.Draw(out)
    d.rectangle([0, im.height, w, im.height + 4], fill=RED)
    x = int(bh * 0.35)
    if logo:
        lw = int(bh * 1.55)
        _, lh = paste_logo(out, lw, (x, im.height + (bh - int(lw * 712 / 1332)) // 2 + 2), glow=False)
        x += lw + int(bh * 0.3)
    if title:
        text(out, (x, im.height + int(bh * 0.18)), title.upper(), int(bh * 0.36), shadow=False)
    if note:
        text(out, (x + 2, im.height + int(bh * 0.62)), note, int(bh * 0.22), font=SUB,
             fill=(205, 205, 212), shadow=False)
    return out


def label(im: Image.Image, xy, s: str, size: int, anchor="la", fill=WHITE):
    d = ImageDraw.Draw(im, "RGBA")
    from PIL import ImageFont
    f = ImageFont.truetype(SUB, size)
    l, t, r, b = d.textbbox(xy, s, font=f, anchor=anchor)
    pad = size // 3
    d.rectangle([l - pad, t - pad, r + pad, b + pad], fill=(0, 0, 0, 170))
    d.rectangle([l - pad, b + pad - 3, r + pad, b + pad], fill=RED + (255,))
    d.text(xy, s, font=f, fill=fill, anchor=anchor)


def split(a: Image.Image, b: Image.Image, labels, angle: float, at: float = 0.5) -> Image.Image:
    w, h = a.size
    cx = int(w * at)
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    off = int(h * angle / 2)
    d.polygon([(cx + off, 0), (w, 0), (w, h), (cx - off, h)], fill=255)
    out = Image.composite(b, a, mask)
    dd = ImageDraw.Draw(out)
    dd.line([(cx + off, 0), (cx - off, h)], fill=WHITE, width=max(3, w // 480))
    size = max(18, h // 26)
    label(out, (int(w * 0.03), int(h * 0.04)), labels[0], size)
    label(out, (int(w * 0.97), int(h * 0.04)), labels[1], size, anchor="ra")
    return out


def panels(ims, labels, crop, out_w: int) -> Image.Image:
    if crop:
        ims = [im.crop(crop) for im in ims]
    n = len(ims)
    gap = 6
    pw = (out_w - gap * (n - 1)) // n
    ph = round(ims[0].height * pw / ims[0].width)
    out = Image.new("RGB", (out_w, ph), BAR)
    size = max(16, min(36, pw // 14))
    for i, (im, lab) in enumerate(zip(ims, labels)):
        x = i * (pw + gap)
        out.paste(im.resize((pw, ph), Image.LANCZOS), (x, 0))
        label(out, (x + pw // 2, ph - size), lab, size, anchor="ms")
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("mode", choices=("split", "panels", "card"))
    p.add_argument("out", type=Path)
    p.add_argument("images", nargs="+", type=Path)
    p.add_argument("--labels", default="OFF,ON")
    p.add_argument("--title")
    p.add_argument("--note")
    p.add_argument("--crop", help="x0,y0,x1,y1 in source pixels")
    p.add_argument("--angle", type=float, default=0.18, help="slant of the split line")
    p.add_argument("--at", type=float, default=0.5, help="split position as a fraction of the width")
    p.add_argument("--width", type=int, default=1920)
    args = p.parse_args()

    ims = [Image.open(f).convert("RGB") for f in args.images]
    labels = [s.strip() for s in args.labels.split(",")]
    crop = tuple(int(v) for v in args.crop.split(",")) if args.crop else None
    if args.mode == "split":
        if len(ims) != 2:
            p.error("split takes two images")
        a, b = (im.crop(crop) if crop else im for im in ims)
        out = split(a, b, labels, args.angle, args.at)
    elif args.mode == "panels":
        if len(labels) != len(ims):
            p.error("give one label per panel")
        out = panels(ims, labels, crop, args.width)
    else:
        out = ims[0].crop(crop) if crop else ims[0]
    if out.width != args.width and args.mode != "panels":
        out = out.resize((args.width, round(out.height * args.width / out.width)), Image.LANCZOS)
    out = caption_bar(out, args.title, args.note)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    out.save(args.out, quality=90, subsampling=0, optimize=True)
    print(args.out, out.size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
