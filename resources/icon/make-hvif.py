#!/usr/bin/env python3
"""Writes the R Television icon as a Haiku vector icon (HVIF).

Same picture as make-icon.py - a parabolic antenna on an isometric patch of
lawn - redrawn as a handful of flat shapes so it fits in a few hundred bytes.
Every shape's outline is a slightly larger dark copy drawn underneath it, which
keeps the file free of stroke transformers. Gradients run left to right in the
icon's own coordinate system (-64..64), so their stops are placed by x.

Regenerate with:  python3 resources/icon/make-hvif.py
Writes RTelevision.hvif next to this file; platforms/haiku/RTelevision.rdef
imports it. Only the standard library is needed.
"""
import math
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "RTelevision.hvif")

# Coordinates below are in the 1024-unit space of make-icon.py; HVIF icons are
# drawn in a 64-unit box.
SCALE = 64.0 / 1024.0
OUTLINE = (40, 32, 24, 255)
EDGE = 0.55           # outline thickness in icon units, applied on each side


# --------------------------------------------------------------- encoding
def coord(v):
    v = max(-128.0, min(192.0, v))
    if -32.0 <= v <= 95.0 and abs(v - round(v)) < 1e-6:
        return bytes([int(round(v)) + 32])
    n = int(round((v + 128.0) * 102.0))
    return bytes([(n >> 8) | 0x80, n & 0xFF])


def gradient_offset(x):
    """Stop offset (0..255) for a horizontal gradient reaching x in icon units."""
    return max(0, min(255, int(round((x + 64.0) / 128.0 * 255.0))))


class Icon:
    def __init__(self):
        self.styles, self.paths, self.shapes = [], [], []

    def solid(self, rgba):
        style = bytes([0x01]) + bytes(rgba)
        if style not in self.styles:          # reuse repeated colours
            self.styles.append(style)
        return self.styles.index(style)

    def linear(self, stops):
        """stops: [(x_in_icon_units, rgba), ...] left to right."""
        out = bytes([0x02, 0x00, 0x00, len(stops)])
        for x, rgba in stops:
            out += bytes([gradient_offset(x)]) + bytes(rgba)
        self.styles.append(out)
        return len(self.styles) - 1

    def polygon(self, pts):
        out = bytes([0x02 | 0x08, len(pts)])          # closed, no curves
        for x, y in pts:
            out += coord(x) + coord(y)
        self.paths.append(out)
        return len(self.paths) - 1

    def curve(self, nodes):
        """nodes: [(point, point_in, point_out), ...], closed."""
        out = bytes([0x02, len(nodes)])
        for p, pin, pout in nodes:
            for x, y in (p, pin, pout):
                out += coord(x) + coord(y)
        self.paths.append(out)
        return len(self.paths) - 1

    def shape(self, style, path):
        self.shapes.append(bytes([0x0A, style, 1, path, 0x00]))

    def write(self, name):
        data = b"ncif" + bytes([len(self.styles)]) + b"".join(self.styles)
        data += bytes([len(self.paths)]) + b"".join(self.paths)
        data += bytes([len(self.shapes)]) + b"".join(self.shapes)
        with open(name, "wb") as f:
            f.write(data)
        return len(data)


# --------------------------------------------------------------- geometry
def s(p):
    return (p[0] * SCALE, p[1] * SCALE)


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def offset_convex(pts, d):
    """Moves each edge of a convex polygon outwards by d (any winding)."""
    n = len(pts)
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    sign = 1.0 if area > 0 else -1.0
    lines = []
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy)
        nx, ny = sign * dy / L, -sign * dx / L        # outward normal
        lines.append((x0 + nx * d, y0 + ny * d, dx, dy))
    out = []
    for i in range(n):
        ax, ay, adx, ady = lines[i - 1]
        bx, by, bdx, bdy = lines[i]
        det = adx * bdy - ady * bdx
        t = ((bx - ax) * bdy - (by - ay) * bdx) / det
        px, py = ax + adx * t, ay + ady * t
        # Sharp corners would throw the mitre far out; cap it like a stroke does.
        vx, vy = px - pts[i][0], py - pts[i][1]
        dist = math.hypot(vx, vy)
        if dist > 1.6 * d:
            px, py = pts[i][0] + vx / dist * 1.6 * d, pts[i][1] + vy / dist * 1.6 * d
        out.append((px, py))
    return out


KAPPA = 0.5522847498


def ellipse(cx, cy, rx, ry, degrees):
    """A rotated ellipse as four Bezier nodes (point, in, out)."""
    r = math.radians(degrees)
    c, sn = math.cos(r), math.sin(r)

    def rot(x, y):
        return (cx + x * c - y * sn, cy + x * sn + y * c)

    nodes = []
    for ux, uy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
        px, py = ux * rx, uy * ry
        tx, ty = -uy * rx * KAPPA, ux * ry * KAPPA      # tangent direction
        nodes.append((rot(px, py), rot(px - tx, py - ty), rot(px + tx, py + ty)))
    return nodes


def ellipse_points(cx, cy, rx, ry, degrees, n=72):
    r = math.radians(degrees)
    c, sn = math.cos(r), math.sin(r)
    return [(cx + rx * math.cos(t) * c - ry * math.sin(t) * sn,
             cy + rx * math.cos(t) * sn + ry * math.sin(t) * c)
            for t in (2 * math.pi * i / n for i in range(n))]


# --------------------------------------------------------------- drawing
def outlined_polygon(icon, pts, style):
    icon.shape(icon.solid(OUTLINE), icon.polygon(offset_convex(pts, EDGE)))
    icon.shape(style, icon.polygon(pts))


def outlined_ellipse(icon, cx, cy, rx, ry, deg, style):
    icon.shape(icon.solid(OUTLINE), icon.curve(ellipse(cx, cy, rx + EDGE, ry + EDGE, deg)))
    icon.shape(style, icon.curve(ellipse(cx, cy, rx, ry, deg)))


def build():
    icon = Icon()

    # -- lawn: an isometric tile with soil sides
    TOP, RIGHT, BOTTOM, LEFT = s((512, 468)), s((938, 678)), s((512, 888)), s((86, 678))
    depth = 84 * SCALE
    down = lambda p: (p[0], p[1] + depth)
    left_face = [LEFT, BOTTOM, down(BOTTOM), down(LEFT)]
    right_face = [BOTTOM, RIGHT, down(RIGHT), down(BOTTOM)]
    outlined_polygon(icon, left_face, icon.solid((132, 86, 44, 255)))
    outlined_polygon(icon, right_face, icon.solid((84, 52, 26, 255)))
    # turf hanging over the soil
    hang = 30 * SCALE
    icon.shape(icon.solid((96, 168, 48, 255)),
               icon.polygon([LEFT, BOTTOM, (BOTTOM[0], BOTTOM[1] + hang), (LEFT[0], LEFT[1] + hang)]))
    icon.shape(icon.solid((64, 128, 36, 255)),
               icon.polygon([BOTTOM, RIGHT, (RIGHT[0], RIGHT[1] + hang), (BOTTOM[0], BOTTOM[1] + hang)]))
    top = [TOP, RIGHT, BOTTOM, LEFT]
    outlined_polygon(icon, top, icon.linear([(LEFT[0], (156, 222, 88, 255)),
                                              (RIGHT[0], (82, 160, 44, 255))]))
    # a few tufts
    tuft = icon.solid((58, 124, 32, 255))
    for x, y in ((236, 684), (330, 790), (690, 802), (792, 652), (596, 548), (400, 588)):
        for dx, h in ((-14, 34), (0, 48), (14, 32)):
            a, b = s((x + dx * 0.4, y)), s((x + dx, y - h))
            w = 4.5 * SCALE
            icon.shape(tuft, icon.polygon([(a[0] - w, a[1]), (a[0] + w, a[1]), (b[0] + w, b[1]), (b[0] - w, b[1])]))

    # -- shadow thrown onto the lawn
    icon.shape(icon.solid((20, 40, 10, 70)), icon.curve(ellipse(*s((610, 738)), 190 * SCALE, 54 * SCALE, 26)))

    # -- mast footing
    fx, fy = s((452, 700))
    hw, hh, dep = 74 * SCALE, 37 * SCALE, 26 * SCALE
    f_top, f_right, f_bottom, f_left = (fx, fy - hh), (fx + hw, fy), (fx, fy + hh), (fx - hw, fy)
    outlined_polygon(icon, [f_left, f_bottom, (f_bottom[0], f_bottom[1] + dep), (f_left[0], f_left[1] + dep)],
                     icon.solid((186, 184, 176, 255)))
    outlined_polygon(icon, [f_bottom, f_right, (f_right[0], f_right[1] + dep), (f_bottom[0], f_bottom[1] + dep)],
                     icon.solid((138, 136, 130, 255)))
    outlined_polygon(icon, [f_top, f_right, f_bottom, f_left], icon.solid((226, 224, 216, 255)))

    # -- mast
    mx, mtop, mfoot, mw = 452 * SCALE, 392 * SCALE, 700 * SCALE, 24 * SCALE
    mast = [(mx - mw, mtop), (mx + mw, mtop), (mx + mw, mfoot), (mx - mw, mfoot)]
    outlined_polygon(icon, mast, icon.linear([(mx - mw, (240, 242, 246, 255)),
                                               (mx + mw, (118, 122, 134, 255))]))

    # -- dish: back shell, then the face
    cx, cy = s((408, 330))
    tilt = 42
    outlined_ellipse(icon, cx - 30 * SCALE, cy + 30 * SCALE, 262 * SCALE, 150 * SCALE, tilt,
                     icon.linear([(cx - 16, (150, 158, 172, 255)), (cx + 8, (92, 98, 112, 255))]))
    outlined_ellipse(icon, cx, cy, 250 * SCALE, 140 * SCALE, tilt,
                     icon.linear([(cx - 12, (255, 255, 255, 255)), (cx + 14, (184, 194, 210, 255))]))
    # inner ring for depth
    icon.shape(icon.solid((150, 162, 184, 90)),
               icon.curve(ellipse(cx + 8 * SCALE, cy - 8 * SCALE, 212 * SCALE, 110 * SCALE, tilt)))
    icon.shape(icon.linear([(cx - 12, (255, 255, 255, 255)), (cx + 14, (184, 194, 210, 255))]),
               icon.curve(ellipse(cx + 8 * SCALE, cy - 8 * SCALE, 200 * SCALE, 100 * SCALE, tilt)))

    # -- feed arm from the lowest rim point up to the LNB
    face_pts = ellipse_points(cx, cy, 250 * SCALE, 140 * SCALE, tilt)
    root = max(face_pts, key=lambda p: p[1] - 0.35 * p[0])
    lnb = (cx + 176 * SCALE, cy - 118 * SCALE)
    ax, ay = lnb[0] - root[0], lnb[1] - root[1]
    L = math.hypot(ax, ay)
    nx, ny = -ay / L, ax / L
    half = 10 * SCALE
    arm = [(root[0] + nx * half, root[1] + ny * half), (lnb[0] + nx * half, lnb[1] + ny * half),
           (lnb[0] - nx * half, lnb[1] - ny * half), (root[0] - nx * half, root[1] - ny * half)]
    outlined_polygon(icon, arm, icon.solid((214, 218, 226, 255)))

    # -- LNB: a short capped cylinder pointing back into the bowl
    ang = math.atan2(cy - lnb[1], cx - lnb[0])
    ux, uy = math.cos(ang), math.sin(ang)
    vx, vy = -uy, ux
    length, bh = 92 * SCALE, 34 * SCALE
    tail = (lnb[0] - ux * length * 0.55, lnb[1] - uy * length * 0.55)
    head = (lnb[0] + ux * length * 0.45, lnb[1] + uy * length * 0.45)
    body = [(tail[0] + vx * bh, tail[1] + vy * bh), (head[0] + vx * bh, head[1] + vy * bh),
            (head[0] - vx * bh, head[1] - vy * bh), (tail[0] - vx * bh, tail[1] - vy * bh)]
    outlined_polygon(icon, body, icon.solid((96, 102, 116, 255)))
    mouth = (head[0] - ux * 10 * SCALE, head[1] - uy * 10 * SCALE)
    icon.shape(icon.solid((206, 212, 224, 255)),
               icon.curve(ellipse(mouth[0], mouth[1], 14 * SCALE, bh - 6 * SCALE, math.degrees(ang))))

    return icon


def main():
    icon = build()
    n = icon.write(OUT)
    print(f"wrote {OUT} ({n} bytes, {len(icon.styles)} styles, {len(icon.paths)} paths, {len(icon.shapes)} shapes)")


if __name__ == "__main__":
    main()
