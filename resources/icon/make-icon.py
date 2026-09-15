#!/usr/bin/env python3
"""Draws the R Television app icon in the BeOS style: a parabolic antenna
standing on a patch of lawn.

BeOS icons are small isometric objects rather than pictures on a plate: a dark
outline, light falling from the top left, one gradient per surface, and a
shadow cast onto whatever the object stands on. Here the lawn is an isometric
tile with a soil edge, and the dish opens towards the upper right with its feed
arm rising from the lower rim to the LNB.

Regenerate with:  python3 resources/icon/make-icon.py
Requires Pillow and numpy (and iconutil for the .icns, macOS only). The
generated PNGs and AppIcon.icns are committed so a build never needs them.
"""
import math
import os
import shutil
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

S = 1024                      # master size; every coordinate below is in these units
K = 4                         # supersampling factor, for smooth edges after downscaling
W = S * K
OUT = os.path.dirname(os.path.abspath(__file__))

OUTLINE = (40, 32, 24, 255)
STROKE = 18


def scaled(points):
    return [(x * K, y * K) for x, y in points]


def polygon_mask(points):
    mask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(mask).polygon(scaled(points), fill=255)
    return mask


def fill(canvas, mask, c0, c1=None, p0=None, p1=None):
    """Fills a mask with a flat colour, or a linear gradient from c0 at p0 to c1 at p1."""
    box = mask.getbbox()
    if not box:
        return
    x0, y0, x1, y1 = box
    if c1 is None:
        layer = Image.new("RGBA", (x1 - x0, y1 - y0), c0)
    else:
        ys, xs = np.mgrid[y0:y1, x0:x1].astype(np.float32)
        ax, ay, bx, by = p0[0] * K, p0[1] * K, p1[0] * K, p1[1] * K
        dx, dy = bx - ax, by - ay
        t = np.clip(((xs - ax) * dx + (ys - ay) * dy) / (dx * dx + dy * dy), 0, 1)[..., None]
        a, b = np.array(c0, np.float32), np.array(c1, np.float32)
        layer = Image.fromarray((a + (b - a) * t).astype(np.uint8), "RGBA")
    canvas.paste(layer, (x0, y0), mask.crop(box))


def outline(canvas, points, width=STROKE, closed=True):
    pts = scaled(points)
    if closed:
        pts = pts + pts[:2]
    ImageDraw.Draw(canvas).line(pts, fill=OUTLINE, width=width * K, joint="curve")


def shape(canvas, points, c0, c1=None, p0=None, p1=None):
    fill(canvas, polygon_mask(points), c0, c1, p0, p1)
    outline(canvas, points)


def ellipse(cx, cy, rx, ry, degrees, n=240):
    r = math.radians(degrees)
    c, s = math.cos(r), math.sin(r)
    out = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        out.append((cx + x * c - y * s, cy + x * s + y * c))
    return out


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


# ------------------------------------------------------------------ the lawn
TOP, RIGHT, BOTTOM, LEFT = (512, 468), (938, 678), (512, 888), (86, 678)
DEPTH = 84


def grass_edge(a, b, drop):
    """The lower edge of the turf hanging over a side face, cut into blades."""
    pts, n = [], 18
    for i in range(n + 1):
        x, y = lerp(a, b, i / n)
        pts.append((x, y + (drop + 16 if i % 2 else drop)))
    return pts


def draw_lawn(img):
    # Side faces: soil, with the turf's blades hanging over the top of each.
    left_face = [LEFT, BOTTOM, (BOTTOM[0], BOTTOM[1] + DEPTH), (LEFT[0], LEFT[1] + DEPTH)]
    right_face = [BOTTOM, RIGHT, (RIGHT[0], RIGHT[1] + DEPTH), (BOTTOM[0], BOTTOM[1] + DEPTH)]
    fill(img, polygon_mask(left_face), (150, 100, 52, 255), (104, 66, 32, 255), LEFT, BOTTOM)
    fill(img, polygon_mask(right_face), (98, 62, 30, 255), (70, 44, 22, 255), BOTTOM, RIGHT)

    left_turf = [LEFT, BOTTOM] + list(reversed(grass_edge(LEFT, BOTTOM, 30)))
    right_turf = [BOTTOM, RIGHT] + list(reversed(grass_edge(BOTTOM, RIGHT, 30)))
    fill(img, polygon_mask(left_turf), (96, 168, 48, 255))
    fill(img, polygon_mask(right_turf), (64, 128, 36, 255))
    outline(img, left_face)
    outline(img, right_face)

    top = [TOP, RIGHT, BOTTOM, LEFT]
    shape(img, top, (156, 222, 88, 255), (82, 160, 44, 255), (300, 520), (720, 860))

    # A few tufts so the tile reads as grass rather than a green slab.
    d = ImageDraw.Draw(img)
    for x, y in ((236, 684), (330, 790), (690, 802), (792, 652), (596, 548), (400, 588)):
        for dx, h in ((-14, 34), (0, 48), (14, 32)):
            d.line(scaled([(x + dx * 0.4, y), (x + dx, y - h)]), fill=(58, 124, 32, 255),
                   width=9 * K, joint="curve")


def draw_shadow(img):
    """The antenna's shadow, thrown to the lower right onto the lawn only."""
    shadow = Image.new("L", (W, W), 0)
    ImageDraw.Draw(shadow).polygon(scaled(ellipse(610, 738, 190, 54, 26)), fill=76)
    shadow = shadow.filter(ImageFilter.GaussianBlur(14 * K))
    lawn = polygon_mask([TOP, RIGHT, BOTTOM, LEFT])
    alpha = Image.fromarray(np.minimum(np.asarray(shadow), np.asarray(lawn)))
    layer = Image.new("RGBA", (W, W), (20, 40, 10, 0))
    layer.putalpha(alpha)
    img.alpha_composite(layer)


# --------------------------------------------------------------- the antenna
MAST_X, MAST_TOP, MAST_FOOT = 452, 392, 700
DISH = (408, 330)


def draw_mast(img):
    # A small concrete footing, itself an isometric block.
    fx, fy, hw, hh, dep = MAST_X, MAST_FOOT, 74, 37, 26
    f_top, f_right, f_bottom, f_left = (fx, fy - hh), (fx + hw, fy), (fx, fy + hh), (fx - hw, fy)
    shape(img, [f_left, f_bottom, (f_bottom[0], f_bottom[1] + dep), (f_left[0], f_left[1] + dep)],
          (186, 184, 176, 255))
    shape(img, [f_bottom, f_right, (f_right[0], f_right[1] + dep), (f_bottom[0], f_bottom[1] + dep)],
          (138, 136, 130, 255))
    shape(img, [f_top, f_right, f_bottom, f_left], (226, 224, 216, 255))

    mast = [(MAST_X - 24, MAST_TOP), (MAST_X + 24, MAST_TOP),
            (MAST_X + 24, MAST_FOOT), (MAST_X - 24, MAST_FOOT)]
    shape(img, mast, (240, 242, 246, 255), (118, 122, 134, 255),
          (MAST_X - 24, 0), (MAST_X + 24, 0))


def draw_dish(img):
    cx, cy = DISH
    tilt = 42                     # major axis runs upper left to lower right
    # The shell's back edge shows as a crescent on the lower left, away from
    # where the dish is pointing.
    back = ellipse(cx - 30, cy + 30, 262, 150, tilt)
    shape(img, back, (150, 158, 172, 255), (92, 98, 112, 255), (cx - 200, cy - 60), (cx + 60, cy + 200))

    face = ellipse(cx, cy, 250, 140, tilt)
    fill(img, polygon_mask(face), (255, 255, 255, 255), (184, 194, 210, 255),
         (cx - 160, cy - 160), (cx + 170, cy + 150))
    # A soft inner ring gives the bowl some depth.
    ring = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ImageDraw.Draw(ring).line(scaled(ellipse(cx + 8, cy - 8, 212, 110, tilt) + [ellipse(cx + 8, cy - 8, 212, 110, tilt)[0]]),
                              fill=(150, 162, 184, 110), width=10 * K, joint="curve")
    img.alpha_composite(ring.filter(ImageFilter.GaussianBlur(4 * K)))
    outline(img, face)
    return face


def draw_feed(img, face):
    cx, cy = DISH
    lnb = (cx + 176, cy - 118)
    # The arm starts at the lowest point of the rim, as on an offset dish.
    root = max(face, key=lambda p: p[1] - 0.35 * p[0])
    d = ImageDraw.Draw(img)
    d.line(scaled([root, lnb]), fill=OUTLINE, width=(20 + STROKE) * K)
    d.line(scaled([root, lnb]), fill=(214, 218, 226, 255), width=20 * K)

    # The LNB: a short capped cylinder pointing back into the bowl.
    ang = math.atan2(cy - lnb[1], cx - lnb[0])
    ux, uy = math.cos(ang), math.sin(ang)
    vx, vy = -uy, ux
    length, half = 92, 34
    tail = (lnb[0] - ux * length * 0.55, lnb[1] - uy * length * 0.55)
    head = (lnb[0] + ux * length * 0.45, lnb[1] + uy * length * 0.45)
    body = [(tail[0] + vx * half, tail[1] + vy * half), (head[0] + vx * half, head[1] + vy * half),
            (head[0] - vx * half, head[1] - vy * half), (tail[0] - vx * half, tail[1] - vy * half)]
    shape(img, body, (120, 126, 140, 255), (54, 58, 68, 255),
          (lnb[0] + vx * half, lnb[1] + vy * half), (lnb[0] - vx * half, lnb[1] - vy * half))
    # The feed horn's opening, facing the bowl: a lighter end, no outline of its own.
    mouth = ellipse(head[0] - ux * 10, head[1] - uy * 10, 14, half - 6, math.degrees(ang))
    fill(img, polygon_mask(mouth), (206, 212, 224, 255))


def draw_icon():
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    draw_lawn(img)
    draw_shadow(img)
    draw_mast(img)
    face = draw_dish(img)
    draw_feed(img, face)
    return img.resize((S, S), Image.LANCZOS)


def write_icns(master):
    if not shutil.which("iconutil"):
        print("iconutil not found; AppIcon.icns left as it was")
        return
    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, "AppIcon.iconset")
        os.mkdir(iconset)
        for px in (16, 32, 128, 256, 512):
            master.resize((px, px), Image.LANCZOS).save(os.path.join(iconset, f"icon_{px}x{px}.png"))
            master.resize((px * 2, px * 2), Image.LANCZOS).save(
                os.path.join(iconset, f"icon_{px}x{px}@2x.png"))
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o",
                        os.path.join(OUT, "AppIcon.icns")], check=True)


def main():
    master = draw_icon()
    for px in (16, 32, 64, 128, 256, 512, 1024):
        master.resize((px, px), Image.LANCZOS).save(os.path.join(OUT, f"icon_{px}.png"))
    master.save(os.path.join(OUT, "icon.png"))
    write_icns(master)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
