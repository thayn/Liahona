"""
Covenant Path participant medallion (single-colour print, painted after).

Two parts:
  medallion  - the badge. Face up on the bed; NFC pocket opens on the back (bed side).
  cap        - press-fit disc that closes the pocket over a 1" (25 mm) NTAG215 sticker.

Run:  python medallion.py   ->  out/medallion.stl, out/medallion_cap.stl, .step, preview PNGs
All sizes in mm. Change the settings block only.
"""
from pathlib import Path
import math
import numpy as np
from build123d import (Polygon, Circle, Rectangle, Text, Pos, Rot, Plane, Mode, Align,
                       Sketch, Compound, extrude, Vector, offset, mirror, export_stl, export_step)

HERE = Path(__file__).parent
UP = (0, 0, 1)
FONT = str(HERE / "fonts" / "Oswald-Bold.ttf")

S = dict(
    # outline: hexagon with points top and bottom (matches the logo badge)
    W=48.0, H=56.0, SIDE_HALF=14.0, CORNER_R=3.0,
    BASE_T=4.0,            # body thickness
    RELIEF=0.8,            # raised art height (paint these surfaces the light colour)
    RIM_W=1.6,             # outer raised rim
    LINE_GAP=1.0, LINE_W=0.8,   # thin inner border line
    ART_GAP=0.8,           # clearance between the inner line and the artwork
    # lanyard bail above the top point
    BAIL_R=6.0, BAIL_Y=31.5, HOLE_D=5.2,
    # NFC pocket (back, open on the bed side) and cap
    TAG_D=25.4,            # 1" NTAG215 sticker
    POCKET_D=28.4, POCKET_DEPTH=1.8, POCKET_Y=-1.0,
    CAP_D=28.2, CAP_T=1.6, CAP_STAR=0.6,
    NOTCH_R=1.5,           # pry notch at the pocket edge
    # back text, engraved into the bed side
    BACK_TEXT_H=3.6, BACK_ENGRAVE=0.6,
    TOP_TEXT="COVENANT", BOTTOM_TEXT="PATH",
    BACK_TOP="MAKE PROMISES", BACK_BOTTOM="KEEP PROMISES",
)


# ---------- 2D helpers ----------
def hex_outline():
    w, h, s = S["W"] / 2, S["H"] / 2, S["SIDE_HALF"]
    pts = [(0, h), (w, s), (w, -s), (0, -h), (-w, -s), (-w, s)]
    poly = Polygon(*pts, align=None)
    r = S["CORNER_R"]
    return offset(offset(poly, -r), r)          # round the corners


def shrink(face, d):
    return offset(face, -d)


def text_fit(txt, cap_h, center_y, max_w=None, font=FONT):
    """Text with its cap height = cap_h, centred on x=0 and center_y."""
    t = Text(txt, font_size=10, font_path=font, align=(Align.CENTER, Align.CENTER))
    bb = t.bounding_box()
    k = cap_h / (bb.max.Y - bb.min.Y)
    if max_w and (bb.max.X - bb.min.X) * k > max_w:
        k = max_w / (bb.max.X - bb.min.X)
    t = Text(txt, font_size=10 * k, font_path=font, align=(Align.CENTER, Align.CENTER))
    bb = t.bounding_box()
    return Pos(-(bb.min.X + bb.max.X) / 2, center_y - (bb.min.Y + bb.max.Y) / 2) * t


def thick_polyline(pts, w):
    """Union of rectangles + round joints along a polyline (a raised line)."""
    out = None
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        L = math.hypot(x1 - x0, y1 - y0)
        a = math.degrees(math.atan2(y1 - y0, x1 - x0))
        seg = Pos((x0 + x1) / 2, (y0 + y1) / 2) * Rot(0, 0, a) * Rectangle(L, w)
        out = seg if out is None else out + seg
    for x, y in pts:
        out += Pos(x, y) * Circle(w / 2)
    return out


def catmull(pts, n=24):
    P = np.array(pts, float)
    P = np.vstack([P[0], P, P[-1]])
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for t in np.linspace(0, 1, n, endpoint=False):
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                              + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(P[-2])
    return np.array(out)


def tapered_band(center_pts, w0, w1, n=10):
    """A band along a smooth curve whose width tapers from w0 to w1 (union of short pieces)."""
    c = catmull(center_pts, n=n)
    ws = np.linspace(w0, w1, len(c))
    out = None
    for i in range(len(c) - 1):
        (x0, y0), (x1, y1) = c[i], c[i + 1]
        L = math.hypot(x1 - x0, y1 - y0)
        a = math.degrees(math.atan2(y1 - y0, x1 - x0))
        w = (ws[i] + ws[i + 1]) / 2
        seg = Pos((x0 + x1) / 2, (y0 + y1) / 2) * Rot(0, 0, a) * Rectangle(L, w)
        seg += Pos(x1, y1) * Circle(ws[i + 1] / 2)
        out = seg if out is None else out + seg
    return out + Pos(*c[0]) * Circle(w0 / 2)


def pine(x, y, h, w):
    """Three-tier pine silhouette, base at (x, y)."""
    trunk_h = h * 0.12
    tiers = []
    for i, (top, bot, half) in enumerate([(1.0, 0.55, 0.32), (0.75, 0.32, 0.42), (0.52, 0.12, 0.5)]):
        tiers.append(Polygon((x, y + h * top), (x + w * half, y + h * bot), (x - w * half, y + h * bot),
                             align=None))
    out = tiers[0] + tiers[1] + tiers[2]
    out += Pos(x, y + trunk_h / 2) * Rectangle(w * 0.16, trunk_h + 0.2)
    return out


def star(cx, cy, r_long, r_short, points=8, r_in_frac=0.18):
    pts = []
    n = points * 2
    for i in range(n):
        a = math.pi / 2 + i * math.pi / points
        if i % 2 == 0:
            r = r_long if (i // 2) % 2 == 0 else r_short
        else:
            r = r_long * r_in_frac + 0.6
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return Polygon(*pts, align=None)


# ---------- front artwork ----------
def front_art():
    hexf = hex_outline()
    field = shrink(hexf, S["RIM_W"] + S["LINE_GAP"] + S["LINE_W"] + S["ART_GAP"])

    # rim and the thin inner line
    rim = hexf - shrink(hexf, S["RIM_W"])
    a = S["RIM_W"] + S["LINE_GAP"]
    line = shrink(hexf, a) - shrink(hexf, a + S["LINE_W"])

    # sun with a temple inside
    sx, sy, sr = 0.0, 13.8, 5.8
    sun = Pos(sx, sy) * Circle(sr) - Pos(sx, sy) * Circle(sr - 0.8)
    temple = (Pos(0, 10.7) * Rectangle(6.0, 1.0)
              + Pos(0, 13.4) * Rectangle(1.6, 4.4)
              + Polygon((-0.8, 15.6), (0.8, 15.6), (0, 18.8), align=None))
    for s in (-1, 1):
        temple += Pos(s * 2.2, 12.5) * Rectangle(1.2, 2.6)
        temple += Polygon((s * 2.2 - 0.6, 13.8), (s * 2.2 + 0.6, 13.8), (s * 2.2, 15.6), align=None)

    temple = Pos(0, -0.7) * temple

    # mountains: ridge line + snow caps
    ridge_pts = [(-21, 2), (-14, 8.5), (-10, 6), (-5, 11.5), (0, 8.6),
                 (5, 11.5), (10, 6), (14, 8.5), (21, 2)]
    ridge = thick_polyline(ridge_pts, 0.9)
    snow = None
    for px, py in [(-5, 11.5), (5, 11.5)]:
        cap = Polygon((px, py), (px + 2.3, py - 2.4), (px + 0.9, py - 1.7), (px, py - 2.6),
                      (px - 0.9, py - 1.7), (px - 2.3, py - 2.4), align=None)
        snow = cap if snow is None else snow + cap

    # sky below the ridge (used to keep rays out of the mountains)
    below = Polygon(*([(-30, 2), *ridge_pts, (30, 2), (30, -30), (-30, -30)]), align=None)

    # sun rays
    rays = None
    for deg in range(15, 166, 15):
        a = math.radians(deg)
        p0 = (sx + 7.0 * math.cos(a), sy + 7.0 * math.sin(a))
        p1 = (sx + 9.0 * math.cos(a), sy + 9.0 * math.sin(a))
        ray = thick_polyline([p0, p1], 0.8)
        rays = ray if rays is None else rays + ray
    rays = rays - offset(below, 0.8)

    # trees in front of the mountains
    trees = (pine(-16.5, -1.6, 10.0, 6.0) + pine(-11.6, 0.2, 7.0, 4.2)
             + pine(16.5, -1.6, 10.0, 6.0) + pine(11.6, 0.2, 7.0, 4.2))
    ridge = ridge - offset(trees, 0.8)

    # the winding path up to the temple
    path = tapered_band([(1.0, -1.3), (-3.8, 1.2), (3.0, 4.0), (-0.8, 6.8), (0.0, 8.6)], 4.0, 0.9)
    ridge = ridge - offset(Pos(0, 8.6) * Circle(1.4), 0)

    # words
    t1 = text_fit(S["TOP_TEXT"], 6.6, -7.4, max_w=36.5)
    t2 = text_fit(S["BOTTOM_TEXT"], 6.6, -16.4, max_w=22.0)

    art = sun + temple + ridge + snow + rays + trees + path
    art = art & field
    # drop slivers too small to print or paint cleanly
    art = Sketch() + [f for f in art.faces() if f.area >= 1.5]
    words = t1 + t2
    return hexf, rim + line, art, words, field


def build():
    hexf, border, art, words, field = front_art()
    T, R = S["BASE_T"], S["RELIEF"]

    body_outline = hexf + Pos(0, S["BAIL_Y"]) * Circle(S["BAIL_R"])
    body_outline = body_outline - Pos(0, S["BAIL_Y"] + 0.4) * Circle(S["HOLE_D"] / 2)
    body = extrude(body_outline, T, dir=UP)

    raised = border + art + words
    body += Pos(0, 0, T) * extrude(raised, R, dir=UP)

    # NFC pocket on the back (bed side) + pry notch
    py = S["POCKET_Y"]
    pocket = Pos(0, py) * Circle(S["POCKET_D"] / 2)
    pocket += Pos(0, py - S["POCKET_D"] / 2 + 0.4) * Circle(S["NOTCH_R"])
    body -= extrude(pocket, S["POCKET_DEPTH"], dir=UP)

    # back text, mirrored so it reads correctly from the back
    bt = (text_fit(S["BACK_TOP"], S["BACK_TEXT_H"], 18.0, max_w=24)
          + text_fit(S["BACK_BOTTOM"], S["BACK_TEXT_H"], -20.0, max_w=21))
    bt = mirror(bt, about=Plane.YZ)
    body -= extrude(bt, S["BACK_ENGRAVE"], dir=UP)

    # cap: disc with a raised compass star (star faces out, on the back)
    cap = extrude(Circle(S["CAP_D"] / 2), S["CAP_T"], dir=UP)
    cap += Pos(0, 0, S["CAP_T"]) * extrude(star(0, 0, 10.5, 6.5) + (Circle(9.8) - Circle(9.0)),
                                            S["CAP_STAR"], dir=UP)
    return body, cap, (hexf, border, art, words, field)


if __name__ == "__main__":
    out = HERE / "out"
    out.mkdir(exist_ok=True)
    body, cap, _ = build()
    export_stl(body, str(out / "medallion.stl"), tolerance=0.02, angular_tolerance=0.2)
    export_stl(cap, str(out / "medallion_cap.stl"), tolerance=0.02, angular_tolerance=0.2)
    export_step(Compound([body, Pos(40, 0, 0) * cap]), str(out / "medallion.step"))
    bb = body.bounding_box()
    print("medallion bbox", round(bb.size.X, 2), round(bb.size.Y, 2), round(bb.size.Z, 2),
          "volume cm3", round(body.volume / 1000, 2), "solids", len(body.solids()))
    print("cap volume cm3", round(cap.volume / 1000, 2), "solids", len(cap.solids()))
