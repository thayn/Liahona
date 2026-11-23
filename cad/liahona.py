"""Liahona ball v2 — 175 mm, concept ornament, multi-colour bodies.

Coordinates: ball centre at the origin, +Z up (display faces up), hinge at -Y
(back), front at +Y. Angles (deg) CCW from +X about +Z. All mm.
Every dimension lives in SETTINGS.

Each part function returns {colour: solid}. Colours: gold, tan, clear.
Run:  python3 liahona.py   -> out/<part>__<colour>.stl, out/<part>.step, out/<part>.3mf
"""
import math
from pathlib import Path
from build123d import (Box, Cylinder, Cone, Sphere, Pos, Rot, Axis, Plane, Vector, Wire, Face,
                       Shell, Solid, Compound, extrude, revolve, export_step, export_stl)
from shapely.geometry import Polygon as SPoly, Point as SPoint, LineString
from shapely.ops import unary_union
from shapely import affinity
import hebrew

# =============================================================================
# SETTINGS
# =============================================================================
# Sizes: the 175 mm ball (4" 4C screen, 126 mm glass) is the reference design. Every
# ball-shaped dimension below is written for 175 mm and scaled by BALL_D / 175.
# Bought parts (switches, battery, boards, rod) keep their real sizes.
SIZES = {
    "160": dict(BALL_D=160.0, DISP_D=115.0,    # ESP32-P4-WIFI6-Touch-LCD-3.4C (default)
                NFC_SEAT=False,                # no room for the reader at the front: it stays on the base cap
                AFTER=dict(GPS_RC=63.0, GPS_ZC=26.0, GPS_TILT=65.0)),   # GPS fits the smaller shoulder
    "175": dict(BALL_D=175.0, DISP_D=126.0),   # ESP32-P4-WIFI6-Touch-LCD-4C
}
SIZE = __import__("os").environ.get("LIAHONA_SIZE", "160")

S = dict(
    # ball ---------------------------------------------------------------
    R=87.5,                  # 175 mm ball (outer surface of raised ornament)
    RELIEF=1.2,              # ornament height above the base surface
    T=2.4,                   # min wall under the ornament
    FIT=0.5, SLIDE=0.3, MIN_CLR=0.4,

    # split planes (z) -------------------------------------------------------
    Z_LID=52.0,              # lid seam = bezel top
    Z_LIDCAP=62.7,           # lid ring / lid cap (inner overhang 48 deg)
    Z_BEZEL=38.0,            # bezel bottom / upper shell top
    Z_SEAM=-24.0,            # upper / lower shell (bayonet)
    Z_BASECAP=-66.5,         # lower shell / base cap (outer overhang 49.5 deg)
    Z_FOOT=-85.5,            # flat foot, ~37 mm across
    Z_SPINDLE_SEAT=85.5,     # flat on the lid for the spindle

    # display ----------------------------------------------------------------
    DISP_D=126.0, DISP_T=6.0,          # DISP_D comes from SIZES
    GASKET=0.5, LIP_T=2.4, LIP_OVER=2.0,   # glass covered 2 mm all round
    BOARD_W=84.6, BOARD_H=65.4, BOARD_DEPTH=9.0,   # 3.4C back board, measured 84.56 x 65.41
    CONN_OVERHANG=5.0,
    STANDOFF_X=75.0, STANDOFF_Y=75.0,   # 3.4C back-plate holes, measured 75 x 75, 4 mm holes
    STANDOFF_H=9.0,          # top part of each chassis post (behind the board)
    STANDOFF_D=5.5,          # ... its diameter, slim to clear the board's corner notches
    PEG_D=3.2, PEG_L=3.0,    # printed peg into each 4 mm back-plate hole (glue it)

    # bezel bayonet (upper-shell spigot = display seat) -------------------------
    SPIGOT_SEAT=1.6,         # glass rests 1.6 mm deep on the spigot top
    LUG_H=2.5, LUG_DEPTH=1.4, LUG_W_DEG=14.0,
    LUG_ANGLES=(90.0, 210.0, 330.0), TWIST_DEG=20.0,
    SPIGOT_LOW=28.0,         # display spigot runs down to here, then a 45 deg web
    SEAM_COLLAR_R_IN=76.5, SEAM_COLLAR_H=6.0, ORING_CORD=1.5,

    # band + letters ------------------------------------------------------------
    BAND_Z=(-8.0, 8.0),      # tan band (recessed to the base surface)
    BAND_TEXT="נֵר לְרַגְלִי דְבָרֶךָ",
    BAND_FONT="/usr/share/fonts/truetype/freefont/FreeSerifBold.ttf",
    LETTER_H=8.0,            # plain-letter height
    TEXT_ARC_DEG=87.0,       # one repetition spans this arc (concept)
    TEXT_REPEATS=(90.0, 210.0, 330.0),
    WORD_GAP=1.9,            # word gap, in letter steps
    DIAMOND=(3.2, 5.0),      # lit diamond separator (w, h)
    FRAME_W=2.5,             # raised gold frame strips along band edges

    # LED ring (part of the upper shell, behind the band) -------------------
    LED_GAP=9.0,             # letters' inner face -> LED face
    LED_STRIP_W=2.7, LED_STRIP_T=1.6,

    # ornament ---------------------------------------------------------------------
    TRI_UPPER=(11.5, 35.0),  # z range of the upper triangle row
    TRI_LOWER=(-29.0, -48.0),
    TRI_N=12,                # up-triangles per row (plus 12 down)
    STRAP_W=2.2,             # straps between panels (tan accent)
    PANEL_INSET=0.6,         # panel bevel step (prints as ~45 deg)
    RIVET_R=1.5, RIVET_H=1.0,
    PETALS=12, PETAL_LAT=(-52.0, -77.0),
    LID_LEAVES=8, LID_LEAF_LAT=(47.0, 76.0),

    # chassis / components -----------------------------------------------------------
    DECK_T=3.0, DECK_R_IN=26.0, DECK_R_OUT=58.0, POST_OD=6.6, DECK_GAP=20.0,
    BATT=(69.0, 37.0, 18.0), POWERBOOST=(36.2, 22.9, 9.0), LIS3MDL=(25.6, 17.8, 3.0),
    LORA=(16.0, 22.0, 3.0), SHIFTER=(30.0, 20.0, 8.0),
    COMPASS_XY=(46.0, 0.0), COMPASS_RISE=10.0,
    GPS_BOARD=(25.4, 17.8, 1.6), GPS_PATCH=(10.0, 10.0, 4.7), GPS_ANGLE=20.0, GPS_ZC=29.0, GPS_RC=70.0, GPS_TILT=55.0,
    NFC=(42.7, 40.4, 4.0),
    INSERT_D=4.0, INSERT_L=5.0, M3_CLR=3.4,

    # USB-C at the bottom (Adafruit 6069 behind a USB-C-shaped slot) -------------
    USB_SLOT=(12.6, 7.0),    # plug overmolds fit through
    USB_SKIN=1.6,            # floor in front of the 6069 nose
    USB_BORE=12.2, USB_GRIP=5.0, USB_NUT_D=20.0,

    # lid / hinge / switches --------------------------------------------------------------
    HINGE_DROP=3.4, KNUCKLE_R=3.4, HINGE_PROUD=0.8,
    PIN_D=2.0, PIN_HOLE_FREE=2.2, PIN_HOLE_PRESS=2.05,
    PIN_ENTRY_SIDE=1,        # +1: pin goes in from the +X side (right, seen from the front)
    KNUCKLE_CENTER_W=12.0, KNUCKLE_GAP=0.4, KNUCKLE_SIDE_W=10.0,
    LID_OPEN_DEG=110.0,
    # lid catch (front) + locating pins: stop the lid sliding sideways on a loose hinge
    CATCH_ANGLE=90.0,        # front, opposite the hinge
    CATCH_W=10.0,            # tongue width (along the rim)
    CATCH_T=1.2,             # tongue thickness (radial): thin enough to flex
    CATCH_L=8.0,             # tongue length below the lid seam
    CATCH_HOOK=0.5,          # hook depth (how far it reaches under the ledge)
    CATCH_PRELOAD=0.1,       # the ledge sits this much low, so the hook pulls the lid down
    LOCPIN_ANGLES=(20.0, 160.0),
    LOCPIN_D=(3.2, 2.2),     # tapered pin: base, tip
    LOCPIN_L=3.0,
    LOCPIN_CLR=0.15,
    # medallion seat: the NFC reader sits behind a round well on the lower front
    NFC_SEAT=True,    NFC_SEAT_THETA=90.0,     # front (opposite the hinge, under the lid catch)
    NFC_SEAT_LAT=-36.0,      # below the band, clear of the seam wedge and the base cap
    NFC_BOARD_D=None,        # reader's antenna face from the ball centre (None: as far out as fits)
    NFC_FLOOR_T=1.6,         # well floor between the medallion and the antenna
    NFC_FLOOR_D=30.0,        # flat floor diameter
    NFC_WALL_DEG=60.0,       # well wall angle to the floor
    NFC_TAPE=0.8,            # foam tape (or hot glue) between the reader and the floor
    NFC_GUIDE_CLR=0.3,
    LID_SW=(12.8, 5.8, 6.5), LID_SW_ANGLE=292.0,     # measured micro switch (L, W, body H)
    LID_SW_CLR=0.25,         # clearance each side: the switch drops in from the top
    LID_SW_LEVER_OVER=1.0,   # the lever runs about 1 mm past the body lengthwise (measured)
    LID_SW_FREE=3.83,        # lever tip above the body top, free (measured)
    LID_SW_CLICK=2.04,       # ... where it clicks (measured)
    LID_SW_OVERTRAVEL=0.5,   # lid pushes the lever this far past the click

    PLUNGER_D=2.0, PLUNGER_COLLAR=4.0, PLUNGER_COLLAR_H=1.2,
    PWR_SW_BODY=(9.1, 3.6, 3.5),     # measured 9.11 x 3.58 x 3.5
    PWR_SW_ACT=(1.44, 1.78),         # slider width, height above the body (5.28 - 3.5), measured
    PWR_SW_TRAVEL=3.71,              # measured slider range
    PWR_SW_RECESS=0.3,               # slider top sits this far below the bezel top
    PWR_SW_GIVE=0.45,                # clearance each side, body and slot (0.2 was too tight)
    PWR_SW_ANGLE=45.0,       # under the lid, front-right (clear of the bayonet lugs)
    SPINDLE_ROD_D=3.0, SPINDLE_H=76.0, SPINDLE_WALL=1.2,
    SPINDLE_PRINTED_MIN_R=3.3, SPINDLE_DOWEL_DEPTH=10.0,   # one-piece printed spindle
)

# scale the ball-shaped settings ---------------------------------------------------
S.update({_k: _v for _k, _v in SIZES[SIZE].items() if _k != "AFTER"})
K = S["BALL_D"] / 175.0
for _k in ("R", "Z_LID", "Z_LIDCAP", "Z_BEZEL", "Z_SEAM", "Z_BASECAP", "Z_FOOT", "Z_SPINDLE_SEAT",
           "SPIGOT_LOW", "SEAM_COLLAR_R_IN", "GPS_ZC", "GPS_RC", "SPINDLE_H", "LETTER_H"):
    S[_k] = S[_k] * K
for _k in ("BAND_Z", "TRI_UPPER", "TRI_LOWER", "COMPASS_XY"):
    S[_k] = tuple(v * K for v in S[_k])
S["SPIGOT_R_IN"] = S["DISP_D"] / 2 - S["SPIGOT_SEAT"]
S["LIP_R_IN"] = S["DISP_D"] / 2 - S["LIP_OVER"]
S["DECK_R_OUT"] = min(S["DECK_R_OUT"], 58.0 * K)
# small ball: keep a full rivet strip between the upper triangle row and the bezel joint
if K < 1:
    S["TRI_UPPER"] = (S["TRI_UPPER"][0], min(S["TRI_UPPER"][1], S["Z_BEZEL"] - 3.6))
RIVET_LIFT = 0.3 if K >= 1 else 0.0

# LIAHONA_ORNAMENT=plates: the "curious workmanship" shells (engraved plates instead of leaves)
ORNAMENT = __import__("os").environ.get("LIAHONA_ORNAMENT", "leaves")
OUT_DIR = __import__("pathlib").Path(__file__).parent / ("out" if SIZE == "160" else f"out/{SIZE}mm")
if ORNAMENT != "leaves":
    OUT_DIR = OUT_DIR / ORNAMENT
S.update(SIZES[SIZE].get("AFTER", {}))


def bs(v):
    """A ball-shaped length written for the 175 mm ball."""
    return v * K


# derived ----------------------------------------------------------------------
R, T, REL = S["R"], S["T"], S["RELIEF"]
R0 = R - REL                       # base surface
RI = R0 - T                        # inner surface
Z_LID = S["Z_LID"]
Z_LIP_UNDER = Z_LID - S["LIP_T"]
Z_GLASS = Z_LIP_UNDER - S["GASKET"]
Z_PLATE = Z_GLASS - S["DISP_T"]
Z_BOARD = Z_PLATE - S["BOARD_DEPTH"]
R_POCKET = S["DISP_D"] / 2 + S["FIT"]
SPIGOT_R_OUT = S["SPIGOT_R_IN"] + T
BEZ_COLLAR_R_IN = SPIGOT_R_OUT + S["SLIDE"]
Z_STANDOFF_END = Z_PLATE - S["STANDOFF_H"]
Z_AX = Z_LID - S["HINGE_DROP"]
AX_Y = -(math.sqrt(R**2 - Z_AX**2) - S["KNUCKLE_R"] + S["HINGE_PROUD"])
BIG = 400.0


def sph_r(z, r=R):
    return math.sqrt(max(r * r - z * z, 0.0))


# =============================================================================
# helpers
# =============================================================================
def zslab(z0, z1):
    return Pos(0, 0, (z0 + z1) / 2) * Box(BIG, BIG, z1 - z0)


def cyl(r, z0, z1):
    return Pos(0, 0, (z0 + z1) / 2) * Cylinder(r, z1 - z0)


def ring(r0, r1, z0, z1):
    return cyl(r1, z0, z1) - cyl(r0, z0 - 1, z1 + 1)


def rev(pts, arc=360.0, start=0.0):
    w = Wire.make_polygon([Vector(r, 0, z) for r, z in pts], close=True)
    return Rot(0, 0, start) * revolve(Face(w), Axis.Z, revolution_arc=arc)


def shell(z0, z1, ro, ri):
    return (Sphere(ro) - Sphere(ri)) & zslab(z0, z1)


def union(parts):
    parts = [p for p in parts if p is not None]
    out = parts[0]
    for p in parts[1:]:
        out = out + p
    return out


def frame(theta, lat):
    th, la = math.radians(theta), math.radians(lat)
    n = Vector(math.cos(la) * math.cos(th), math.cos(la) * math.sin(th), math.sin(la))
    e = Vector(-math.sin(th), math.cos(th), 0)
    nn = Vector(-math.sin(la) * math.cos(th), -math.sin(la) * math.sin(th), math.cos(la))
    return n, e, nn


def gnomonic(theta, lat, d_theta, d_lat):
    """Tangent-plane (mm at radius R0) coords of direction (theta+dt, lat+dl) seen
    from the tangent point (theta, lat)."""
    n, e, nn = frame(theta, lat)
    d, _, _ = frame(theta + d_theta, lat + d_lat)
    k = R0 / d.dot(n)
    p = d * k
    return (p.dot(e), p.dot(nn))


def cone_patch(poly, theta, lat, r_lo, r_hi):
    """Solid = radial cone over a 2D polygon drawn on the tangent plane at
    (theta, lat) [mm at radius R0], clipped to r_lo..r_hi. Holes supported."""
    if poly is None or poly.is_empty:
        return None
    poly = poly.simplify(0.04)
    if poly.geom_type == "MultiPolygon":
        return union([cone_patch(g, theta, lat, r_lo, r_hi) for g in poly.geoms if g.area > 0.05])
    n, e, nn = frame(theta, lat)
    D = r_hi + 4.0
    k = D / R0

    def cone(coords):
        pts = [n * D + e * (u * k) + nn * (v * k) for u, v in coords]
        o = Vector(0, 0, 0)
        faces = [Face(Wire.make_polygon(pts, close=True))]
        for i in range(len(pts)):
            faces.append(Face(Wire.make_polygon([o, pts[(i + 1) % len(pts)], pts[i]], close=True)))
        return Solid(Shell(faces))

    ext = list(poly.exterior.coords)[:-1]
    s = cone(ext)
    for h in poly.interiors:
        s = s - cone(list(h.coords)[:-1])
    return s & (Sphere(r_hi) - Sphere(r_lo))


def relief(poly, theta, lat, r_top=R, bevel=True):
    """Raised ornament from the base surface to r_top with a stepped bevel."""
    if poly is None or poly.is_empty:
        return None
    lo = cone_patch(poly, theta, lat, R0 - 0.3, R0 + (r_top - R0) / 2)
    hi_poly = poly.buffer(-S["PANEL_INSET"], join_style=2) if bevel else poly
    hi = cone_patch(hi_poly, theta, lat, R0 + (r_top - R0) / 2 - 0.01, r_top)
    return union([lo, hi])


def raised_band(z0, z1):
    """Raised zone from the base surface to R between z0 and z1."""
    return shell(z0, z1, R, R0 - 0.3)


def rivet(theta, lat):
    """Faceted dome (revolved polygon) so OCCT never fuses sphere-to-sphere."""
    n, e, _ = frame(theta, lat)
    rr, h = S["RIVET_R"], S["RIVET_H"]
    sink = 0.6
    rs = (rr * rr + h * h) / (2 * h)               # sphere radius through the dome
    pts = [(0.0, -sink), (rr, -sink)]
    for i in range(9):
        a = math.acos((rs - h) / rs) * (1 - i / 8)
        pts.append((rs * math.sin(a), rs * math.cos(a) - (rs - h)))
    pts[-1] = (0.35, h)            # small flat top: no apex vertex in the mesh
    pts.append((0.0, h))
    w = Wire.make_polygon([Vector(r, 0, z) for r, z in pts], close=True)
    dome = revolve(Face(w), Axis.Z)
    return Plane(origin=n * R, x_dir=e, z_dir=n) * dome


def radial_cyl(theta, lat, r, d0, d1):
    n, e, _ = frame(theta, lat)
    pl = Plane(origin=n * d0, x_dir=e, z_dir=n)
    return pl * Pos(0, 0, (d1 - d0) / 2) * Cylinder(r, d1 - d0)


def lat_of(z, r=R0):
    return math.degrees(math.asin(max(-1, min(1, z / r))))


# =============================================================================
# ornament builders
# =============================================================================
def leaf_shape(length, width, groove=0.7):
    """Pointed leaf (vesica) with a midrib groove, centred at 0, pointing +v."""
    rr = (width ** 2 / 4 + (length / 2) ** 2) / width
    c1 = SPoint(rr - width / 2, 0).buffer(rr, 32)
    c2 = SPoint(-(rr - width / 2), 0).buffer(rr, 32)
    leaf = c1.intersection(c2)
    rib = LineString([(0, -length / 2 + 1.5), (0, length / 2 - 2.0)]).buffer(groove / 2)
    return leaf, rib


def sprig(scale=1.0):
    """Small engraved sprig (concept panel motif): leaf outline + stem."""
    leaf, rib = leaf_shape(6.0 * scale, 2.8 * scale)
    outline = leaf.difference(leaf.buffer(-0.6))
    stem = LineString([(0, -3.0 * scale), (0.8 * scale, -5.0 * scale)]).buffer(0.35)
    curls = [SPoint(s * 1.8 * scale, -1.6 * scale).buffer(0.9 * scale).exterior.buffer(0.3)
             for s in (-1, 1)]
    return unary_union([outline, rib, stem] + curls)



# ---- "curious workmanship": continuous engraved bands -------------------------------------
def _bez(p0, p1, p2, p3, n=14):
    out = []
    for i in range(n + 1):
        t = i / n
        a, b, c, d = (1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def glyph_invented(i, h=4.4, w=0.65):
    """A flowing, hieratic-style invented character (deterministic per i), about h tall.
    Returns (shapely geometry centred on 0,0, width)."""
    import random
    r = random.Random(5011 + i * 104729)
    W = h * 0.55
    P = lambda x, y: (x * W, y * h)
    strokes = []
    kind = r.choice(["stem", "stem", "loop", "s", "arch", "hook", "cross", "zig"])
    jit = lambda: r.uniform(-0.08, 0.08)
    if kind == "stem":
        x = r.choice([0.3, 0.5, 0.7])
        strokes.append(_bez(P(x + jit(), 1), P(x + 0.15, 0.65), P(x - 0.15, 0.35), P(x + jit(), 0)))
    elif kind == "loop":
        strokes.append(_bez(P(0.2, 0.0), P(0.9, 0.5), P(0.9, 1.05), P(0.45, 0.95)))
        strokes.append(_bez(P(0.45, 0.95), P(0.05, 0.85), P(0.3, 0.45), P(0.85, 0.3)))
    elif kind == "s":
        strokes.append(_bez(P(0.85, 0.95), P(-0.1, 1.0), P(1.1, 0.0), P(0.15, 0.05)))
    elif kind == "arch":
        strokes.append(_bez(P(0.1, 0.0), P(0.0, 1.15), P(1.0, 1.15), P(0.9, 0.0)))
    elif kind == "hook":
        strokes.append(_bez(P(0.5, 1.0), P(0.5, 0.4), P(0.5, 0.0), P(0.05, 0.15)))
        strokes.append([P(0.15, 0.75), P(0.85, 0.75)])
    elif kind == "cross":
        strokes.append([P(0.5, 1.0), P(0.5, 0.0)])
        strokes.append(_bez(P(0.05, 0.55), P(0.35, 0.75), P(0.65, 0.35), P(0.95, 0.55)))
    else:
        strokes.append([P(0.1, 0.9), P(0.85, 0.75), P(0.15, 0.3), P(0.9, 0.1)])
    for _ in range(r.choice([0, 1, 1, 2])):
        t = r.random()
        if t < 0.3:
            strokes.append([P(r.uniform(0.1, 0.4), r.choice([1.0, 0.0])), P(r.uniform(0.6, 0.9), r.choice([1.0, 0.0]))])
        elif t < 0.55:
            strokes.append(("dot", P(r.choice([0.15, 0.85]), r.choice([0.2, 0.55, 0.95]))))
        elif t < 0.8:
            cx, cy = r.choice([0.2, 0.8]), r.choice([0.25, 0.75])
            strokes.append(_bez(P(cx, cy + 0.12), P(cx + 0.2, cy + 0.15), P(cx + 0.2, cy - 0.15), P(cx - 0.05, cy - 0.1), 8))
        else:
            strokes.append([P(r.uniform(0.6, 0.9), 0.6), P(r.uniform(0.75, 1.0), 0.35)])
    geoms = []
    for st in strokes:
        if st[0] == "dot":
            geoms.append(SPoint(*st[1]).buffer(w * 0.6, 8))
        else:
            geoms.append(LineString(st).buffer(w / 2, 6))
    return affinity.translate(unary_union(geoms), -W / 2, -h / 2), W


def band_glyph(i, h):
    """The i-th character for the bands: traced ones from caractors.json if present
    (list of {"w":..., "strokes":[[[x,y],...],...]} in a unit-height box), else invented."""
    import json
    f = Path(__file__).parent / "caractors.json"
    if f.exists():
        chars = band_glyph.cache if hasattr(band_glyph, "cache") else json.loads(f.read_text())
        band_glyph.cache = chars
        c = chars[i % len(chars)]
        g = unary_union([LineString([(x * h, y * h) for x, y in st]).buffer(0.325, 6) if len(st) > 1
                         else SPoint(st[0][0] * h, st[0][1] * h).buffer(0.4, 8) for st in c["strokes"]])
        W = c["w"] * h
        return affinity.translate(g, -W / 2, -h / 2), W
    return glyph_invented(i, h)


def _groove(z, w=0.6, depth=0.55):
    return (Sphere(R + 1) - Sphere(R - depth)) & zslab(z - w / 2, z + w / 2)


def engraved_band(z_lo, z_hi, seed=0):
    """A continuous raised band with a double engraved border and rows of characters
    packed edge to edge all the way round. Returns (band, cutters, beads)."""
    z_lo, z_hi = min(z_lo, z_hi), max(z_lo, z_hi)
    band = raised_band(z_lo, z_hi)
    cutters = [_groove(z) for z in (z_lo + 1.0, z_lo + 2.1, z_hi - 2.1, z_hi - 1.0)]
    h = 4.4 * K
    free = (z_hi - 2.9) - (z_lo + 2.9)
    nrows = max(1, min(3, int((free + 1.2) // (h + 1.2))))
    zc = (z_lo + z_hi) / 2
    rows_z = [zc + (k - (nrows - 1) / 2) * (h + 1.4) for k in range(nrows)][::-1]
    seg = 15.0
    for ri, zr in enumerate(rows_z):
        lr = lat_of(zr)
        circ = 2 * math.pi * R0 * math.cos(math.radians(lr))
        per_seg = {}
        x, i = 0.6, 0
        while True:
            g, W = band_glyph(seed * 10007 + ri * 1009 + i, h)
            if x + W > circ - 0.9:
                break
            th = (x + W / 2) / circ * 360.0
            k = int(th // seg)
            tc = k * seg + seg / 2
            gx = gnomonic(tc, lr, th - tc, 0)[0]
            per_seg.setdefault(k, []).append(affinity.translate(g, gx, 0))
            x += W + 0.85
            i += 1
        for k, gs in per_seg.items():
            cutters.append(cone_patch(unary_union(gs), k * seg + seg / 2, lr, R - 0.55, R + 1))
    beads = []
    for zb_ in (z_lo - 0.2, z_hi + 0.2):
        lb = lat_of(zb_, R)
        n = int(2 * math.pi * R * math.cos(math.radians(lb)) / 3.2)
        beads += [bead(j * 360.0 / n, lb) for j in range(n)]
    return band, [c for c in cutters if c is not None], beads


def bead(theta, lat, rr=0.95, h=0.75):
    n, e, _ = frame(theta, lat)
    pts = [(0.0, -0.6), (rr, -0.6)]
    rs = (rr * rr + h * h) / (2 * h)
    for i in range(6):
        a = math.acos((rs - h) / rs) * (1 - i / 5)
        pts.append((rs * math.sin(a), rs * math.cos(a) - (rs - h)))
    pts[-1] = (0.3, h)
    pts.append((0.0, h))
    dome = revolve(Face(Wire.make_polygon([Vector(r_, 0, z_) for r_, z_ in pts], close=True)), Axis.Z)
    return Plane(origin=n * (R0 + 0.2), x_dir=e, z_dir=n) * dome


def plate_edge_grooves(z0, z1, pitch=1.4):
    """Fine horizontal lines: the ball reads as a stack of thin plates."""
    out = []
    z = min(z0, z1) + pitch / 2
    while z < max(z0, z1) - pitch / 3:
        out.append(_groove(z, 0.45, 0.4))
        z += pitch
    return out


def binding_rings():
    """Three D-rings on the back (over the hinge side), like the rings through the plates."""
    tz0, tz1 = S["TRI_UPPER"]
    lc = lat_of((tz0 + tz1) / 2)
    hgt = (tz1 - tz0) - 3.0
    out = []
    for th in (255.0, 270.0, 285.0):
        w_ = 11.0 * K
        d = SPoly([(-w_ / 2, -hgt / 2), (w_ / 2, -hgt / 2), (w_ / 2, hgt / 2 - w_ / 2)] +
                  [(w_ / 2 * math.cos(math.radians(a)), hgt / 2 - w_ / 2 + w_ / 2 * math.sin(math.radians(a)))
                   for a in range(0, 181, 15)] + [(-w_ / 2, hgt / 2 - w_ / 2)])
        ring_ = d.buffer(1.1).difference(d.buffer(-1.1))
        out.append(cone_patch(ring_, th, lc, R - 0.6, R + 0.7))
    return out


def ornament_row(z_lo, z_hi, n, phase=0.0, seed=0):
    """(raised gold, tan straps or None, engraving cutters, extra gold e.g. beads)."""
    if ORNAMENT == "engraved":
        band, cut, beads = engraved_band(z_lo, z_hi, seed)
        return band, None, cut, beads
    panels, straps, cut = triangle_row(z_lo, z_hi, n, phase)
    return panels, straps, cut, []


def triangle_row(z_lo, z_hi, n, phase=0.0, motif=True):
    """Alternating up/down triangles between z_lo..z_hi (straps between).
    Returns (panels_gold, straps_tan, engraving_cutters)."""
    la0, la1 = lat_of(z_lo), lat_of(z_hi)
    if la0 > la1:
        la0, la1 = la1, la0
    step = 360.0 / n
    panels, cutters, feet = [], [], []
    for i in range(2 * n):
        up = i % 2 == 0
        tc = phase + i * step / 2
        if up:
            corners = [(tc - step / 2, la0), (tc + step / 2, la0), (tc, la1)]
        else:
            corners = [(tc - step / 2, la1), (tc + step / 2, la1), (tc, la0)]
        lc = (la0 + la1) / 2
        pts = [gnomonic(tc, lc, a - tc, b - lc) for a, b in corners]
        tri = SPoly(pts).buffer(-S["STRAP_W"] / 2, join_style=2)
        panels.append(relief(tri, tc, lc, R - 0.4))
        feet.append(cone_patch(tri, tc, lc, R0 - 0.5, R + 1.0))
        if motif:
            cen = tri.centroid
            s = min(1.0, math.sqrt(tri.area) / 9.0)
            m = affinity.translate(sprig(s), cen.x, cen.y)
            if not up:
                m = affinity.rotate(m, 180, origin=(cen.x, cen.y))
            cutters.append(cone_patch(m, tc, lc, R - 0.9, R + 1))
    # straps: the whole row raised to R, minus the panels' footprint
    zone = shell(min(z_lo, z_hi), max(z_lo, z_hi), R, R0 - 0.3)
    straps = zone.cut(*feet)
    return union(panels), straps, [c for c in cutters if c is not None]


# =============================================================================
# Hebrew band
# =============================================================================
def band_layout():
    """[(geom (mm, x centred), theta, z_base)] for every cluster and diamond."""
    cl, width = hebrew.clusters(S["BAND_TEXT"], S["BAND_FONT"], S["LETTER_H"])
    xs = [x for _, x, _ in cl]
    steps = [xs[i + 1] - xs[i] for i in range(len(xs) - 1)]
    med = sorted(steps)[len(steps) // 2]
    # positions in "steps": 1 per letter, WORD_GAP at word breaks
    pos = [0.0]
    for st in steps:
        pos.append(pos[-1] + (S["WORD_GAP"] if st > 1.4 * med else 1.0))
    span = pos[-1]
    allg = [g for g, _, _ in cl]
    ymin = min(g.bounds[1] for g in allg)
    ymax = max(g.bounds[3] for g in allg)
    z0, z1 = S["BAND_Z"]
    z_base = (z0 + z1) / 2 - (ymin + ymax) / 2
    deg_per_step = S["TEXT_ARC_DEG"] / span
    out = []
    for a0 in S["TEXT_REPEATS"]:
        for (g, x, _), p in zip(cl, pos):
            th = a0 + (p - span / 2) * deg_per_step
            out.append((affinity.translate(g, -x, 0), th, z_base))
        # diamond half-way to the next repeat (visually to the left = smaller theta)
        dw, dh = S["DIAMOND"]
        dia = SPoly([(0, -dh / 2), (dw / 2, 0), (0, dh / 2), (-dw / 2, 0)])
        th_d = a0 - S["TEXT_ARC_DEG"] / 2 - (360 / len(S["TEXT_REPEATS"]) - S["TEXT_ARC_DEG"]) / 2
        out.append((affinity.translate(dia, 0, (z0 + z1) / 2 - z_base), th_d, z_base))
    return out, z_base


def letters_solid(r_lo, r_hi, grow=0.0):
    lay, zb = band_layout()
    parts = []
    for g, th, z_base in lay:
        g = g.buffer(grow, join_style=2) if grow else g
        # tangent plane at the equator: v coordinate = z (scaled to R0)
        gg = affinity.translate(g, 0, z_base)
        parts.append(cone_patch(gg, th, 0.0, r_lo, r_hi))
    return union(parts)


# =============================================================================
# bayonet (same scheme as v1)
# =============================================================================
def bayonet_lugs(r_base, z_lo):
    d, h = S["LUG_DEPTH"], S["LUG_H"]
    prof = [(r_base - 0.5, z_lo - d - 0.5), (r_base + d, z_lo), (r_base + d, z_lo + h), (r_base - 0.5, z_lo + h)]
    return union([rev(prof, S["LUG_W_DEG"], a - S["LUG_W_DEG"] / 2) for a in S["LUG_ANGLES"]])


def bayonet_slots(r_base, z_lo, z_open):
    c, d, h, w = S["SLIDE"], S["LUG_DEPTH"], S["LUG_H"], S["LUG_W_DEG"]
    r0, r1 = r_base - 1.0, r_base + d + c
    z0, z1 = z_lo - d - 0.5 - c, z_lo + h + c
    out = []
    for a in S["LUG_ANGLES"]:
        a_in = a - S["TWIST_DEG"]
        wd = w + 3.0
        out.append(rev([(r0, z_open - 1), (r1, z_open - 1), (r1, z1), (r0, z1)], wd, a_in - wd / 2))
        out.append(rev([(r0, z0), (r1, z0), (r1, z1), (r0, z1)], S["TWIST_DEG"] + wd, a_in - wd / 2))
    return union(out)


# =============================================================================
# hinge
# =============================================================================
def lid_knuckles(clr=0.0):
    kr, cw, g, sw = S["KNUCKLE_R"], S["KNUCKLE_CENTER_W"], S["KNUCKLE_GAP"], S["KNUCKLE_SIDE_W"]
    parts = []
    for sgn in (1, -1):
        x0 = cw / 2 + g - clr
        x1 = x0 + sw + 2 * clr
        xm, xl = sgn * (x0 + x1) / 2, x1 - x0
        parts.append(Pos(xm, AX_Y, Z_AX) * Rot(0, 90, 0) * Cylinder(kr + clr, xl))
        # web up into the lid (kept inside the ball outline)
        parts.append(Pos(xm, AX_Y + 0.5, Z_AX + 2.5) * Box(xl, 4.0 + kr + 2 * clr, 5.0 + 2 * clr))
    return union(parts)


def lid_knuckle_sweep(clr):
    out = []
    for i in range(int(S["LID_OPEN_DEG"] / 5) + 1):
        a = i * 5.0
        out.append(Pos(0, AX_Y, Z_AX) * Rot(a, 0, 0) * Pos(0, -AX_Y, -Z_AX) * lid_knuckles(clr))
    return union(out)


def bezel_knuckle_zone(clr):
    """Lid-side clearance around the bezel's centre knuckle, swept."""
    kr, cw = S["KNUCKLE_R"] + clr, S["KNUCKLE_CENTER_W"] + 2 * min(clr, 0.35)
    k = Pos(0, AX_Y, Z_AX) * Rot(0, 90, 0) * Cylinder(kr, cw)
    out = []
    for i in range(int(S["LID_OPEN_DEG"] / 5) + 1):
        out.append(Pos(0, AX_Y, Z_AX) * Rot(-i * 5.0, 0, 0) * Pos(0, -AX_Y, -Z_AX) * k)
    return union(out)


# =============================================================================
# components / placement helpers
# =============================================================================
def lid_switch_pose():
    """Micro-switch body centre (local frame: x along tangent, y inward, z up).

    The switch drops in from the bezel top, lever up, and rests on a ledge. Its body
    top sits just low enough that the closed lid pushes the lever LID_SW_OVERTRAVEL
    past the click; with the lid open the lever stands about 2.3 mm proud."""
    sl, sw, sh = S["LID_SW"]
    th = S["LID_SW_ANGLE"]
    # inner side 0.5 mm into the display pocket (clean cut; the glass edge sits 0.5 mm further in)
    r = R_POCKET - 0.5 + sw / 2 + S["LID_SW_CLR"]
    z_top = Z_LID - (S["LID_SW_CLICK"] - S["LID_SW_OVERTRAVEL"])
    return th, r, z_top - sh / 2


def lid_switch_plane():
    th, r, zc = lid_switch_pose()
    t = math.radians(th)
    return Plane(origin=(r * math.cos(t), r * math.sin(t), zc), x_dir=(-math.sin(t), math.cos(t), 0),
                 z_dir=(0, 0, 1))


def pwr_switch_plane():
    th = S["PWR_SW_ANGLE"]
    t = math.radians(th)
    r = R_POCKET + T + 2.0           # just outside the display pocket wall
    bh = S["PWR_SW_BODY"][2]
    z_top = Z_LID - S["PWR_SW_ACT"][1] - S["PWR_SW_RECESS"]   # slider just below the bezel top
    return Plane(origin=(r * math.cos(t), r * math.sin(t), z_top - bh / 2),
                 x_dir=(-math.sin(t), math.cos(t), 0), z_dir=(0, 0, 1))


def gps_plane():
    """Back face of the GPS board in the shoulder cavity, tilted GPS_TILT from
    horizontal; z_dir = patch direction (up and out)."""
    th = math.radians(S["GPS_ANGLE"])
    tl = math.radians(S["GPS_TILT"])
    rc, zc = S["GPS_RC"], S["GPS_ZC"]
    radial = Vector(math.cos(th), math.sin(th), 0)
    n = radial * math.sin(tl) + Vector(0, 0, 1) * math.cos(tl)
    t = Vector(-math.sin(th), math.cos(th), 0)
    return Plane(origin=radial * rc + Vector(0, 0, zc), x_dir=t, z_dir=n)


def chassis_z():
    z1 = Z_BOARD - S["DECK_GAP"]
    return z1 - S["DECK_T"], z1


# =============================================================================
# PARTS — each returns {colour: solid}
# =============================================================================
def upper_shell():
    """Seam (-24) to the bezel joint (38). Raised triangle row with tan straps,
    tan band with clear letters through the wall, gold frames and rivets.
    Inside: seam collar, LED ring on a 45 deg web, display spigot + shoulder
    web, GPS seat. Print seam-down (multi-colour)."""
    zs, zb = S["Z_SEAM"], S["Z_BEZEL"]
    bz0, bz1 = S["BAND_Z"]
    fw = S["FRAME_W"]
    tz0, tz1 = S["TRI_UPPER"]
    gold = shell(zs, zb, R0, RI)
    raised = [raised_band(tz1, zb), raised_band(bz1 + fw, tz0), raised_band(zs, bz0 - fw)]
    gold = gold + (union(raised) & zslab(zs, zb))
    panels, strap_zone, engr, beads = ornament_row(tz0, tz1, S["TRI_N"], seed=1)
    tan_straps = strap_zone - Sphere(R0) if strap_zone is not None else None
    gold = gold + panels
    if engr:
        gold = gold.cut(*engr)
    if beads:
        gold = gold.fuse(*beads)
    if ORNAMENT == "engraved":
        gold = gold.fuse(*binding_rings())
    else:
        for i in range(24):
            gold = gold + rivet(i * 15.0 + 7.5, lat_of((tz1 + zb) / 2 + RIVET_LIFT, R))
    # band: gold removed, tan field at the base surface, clear letters through
    gold = gold - (zslab(bz0, bz1) & (Sphere(R + 3) - Sphere(RI - 0.02)))
    gold = gold + raised_band(bz1, bz1 + fw) + raised_band(bz0 - fw, bz0)
    clear = letters_solid(RI - 0.02, R0) & zslab(bz0, bz1)
    tan = shell(bz0, bz1, R0, RI - 0.01) - clear
    if tan_straps is not None:
        tan = tan + tan_straps

    # ---- interior --------------------------------------------------------
    zc1 = zs + S["SEAM_COLLAR_H"]
    collar = (Sphere(RI + 0.01) & zslab(zs, zc1)) - cyl(S["SEAM_COLLAR_R_IN"], zs - 1, zc1 + 1)
    collar = collar - bayonet_slots(S["SEAM_COLLAR_R_IN"] - S["SLIDE"], zs + 2.0, zs)
    # LED ring: cylinder on a 45 deg web rising from the wall above the collar
    led_face = RI - S["LED_GAP"]
    rc_out = led_face - S["LED_STRIP_T"]
    rc_in = rc_out - T
    zw = zc1 + 1.0
    rw = sph_r(zw, RI) + 1.0
    k = T * math.sqrt(2)
    web = rev([(rw, zw), (rw, zw + k), (rc_in, zw + (rw - rc_in) + k), (rc_in, zw + (rw - rc_in))]) & Sphere(RI + 0.01)
    z_led = (bz0 + bz1) / 2
    led_cyl = ring(rc_in, rc_out, zw + (rw - rc_in) - 0.01, z_led + 6.0)
    sw = S["LED_STRIP_W"]
    lips = rev([(rc_out - 0.01, z_led + sw / 2 + 0.2), (rc_out + 0.8, z_led + sw / 2 + 1.0),
                (rc_out + 0.8, z_led + sw / 2 + 1.6), (rc_out - 0.01, z_led + sw / 2 + 1.6)])
    lips = lips + rev([(rc_out - 0.01, z_led - sw / 2 - 1.0), (rc_out + 0.8, z_led - sw / 2 - 0.2),
                       (rc_out - 0.01, z_led - sw / 2 - 0.2)])
    # display spigot: vertical from SPIGOT_LOW to the plate, then a 45 deg web to the wall
    ri_s, ro_s = S["SPIGOT_R_IN"], SPIGOT_R_OUT
    zl = S["SPIGOT_LOW"]
    spigot = ring(ri_s, ro_s, zl - 0.01, Z_PLATE)
    # where the chassis posts come close to the spigot (small screens), groove the
    # spigot and its web so the module still drops in from the top
    sx, sy = S["STANDOFF_X"] / 2, S["STANDOFF_Y"] / 2
    r_post = math.hypot(sx, sy) + S["POST_OD"] / 2 + 0.8
    grooves = []
    if r_post > ri_s:
        z_lo = chassis_z()[0] - 1
        for x in (-sx, sx):
            for y in (-sy, sy):
                a = math.degrees(math.atan2(y, x))
                w = math.degrees((S["POST_OD"] + 2.0) / ri_s)
                grooves.append(rev([(ri_s - 1, z_lo), (r_post, z_lo), (r_post, Z_PLATE + 1),
                                    (ri_s - 1, Z_PLATE + 1)], w, a - w / 2))
    rb = RI + 1.0
    sweb = rev([(ri_s, zl), (ri_s, zl + k), (rb, zl + k - (rb - ri_s)), (rb, zl - (rb - ri_s))]) & Sphere(RI + 0.01)
    lugs = bayonet_lugs(ro_s, zb + 2.0) - cyl(ri_s, zb, Z_PLATE + 1)
    # GPS bracket: solid under the tilted seat, standing on the web
    th = S["GPS_ANGLE"]
    gb = S["GPS_BOARD"]
    seat = gps_plane()
    blk = seat * Pos(0, 0, -20) * Box(gb[0] + 2.0, gb[1] + 2.0, 40)
    blk = blk & (Sphere(RI + 0.01) - cyl(ro_s - 0.01, -100, 100)) & zslab(zl - 30, zb - 0.6)
    blk = blk & rev([(ri_s, zl), (R + 5, zl - (R + 5 - ri_s)), (R + 5, 100), (ri_s, 100)])
    interior = collar + web + led_cyl + lips + spigot + sweb + lugs + blk
    if grooves:
        interior = interior - union(grooves)
    # wire holes: shoulder web (switches, GPS), LED web (strip leads)
    for a in (S["LID_SW_ANGLE"], S["PWR_SW_ANGLE"], th - 16.0):
        t = math.radians(a)
        rr = ro_s + 6.0
        interior = interior - Pos(rr * math.cos(t), rr * math.sin(t), zl - 6.0) * Cylinder(3.0, 14)
    t = math.radians(150.0)
    rr = (rc_in + rw) / 2
    interior = interior - Pos(rr * math.cos(t), rr * math.sin(t), zw + (rw - rr) + 1.5) * Cylinder(2.5, 10)
    # wire gap in the spigot top for the switch leads (display rests elsewhere)
    interior = interior - rev([(ri_s - 1, Z_PLATE - 3.0), (ro_s + 2, Z_PLATE - 3.0), (ro_s + 2, Z_PLATE + 1),
                               (ri_s - 1, Z_PLATE + 1)], 10.0, S["LID_SW_ANGLE"] - 5.0)
    gold = gold + interior
    return {"gold": gold, "tan": tan, "clear": clear}



# ---- lid catch + locating pins ------------------------------------------------------------
def _catch_geo():
    """Radii/heights of the front catch tongue (shared by lid and bezel)."""
    r_in = sph_r(Z_LID, RI) + 0.35            # tongue inner face, just inside the lid wall
    r_out = r_in + S["CATCH_T"]
    zt = Z_LID - S["CATCH_L"]                 # tongue tip
    h = S["CATCH_HOOK"]
    z_hook_top = zt + h + 0.6                 # top of the hook's flat
    return r_in, r_out, zt, h, z_hook_top


def _at(angle):
    return Rot(0, 0, angle - 90.0)            # local +Y = radial at `angle`, local X = along the rim


def _yz_prism(pts, width):
    """Polygon in the (radial Y, Z) plane, extruded `width` along X (centred)."""
    w = Wire.make_polygon([Vector(-width / 2, y, z) for y, z in pts], close=True)
    return extrude(Face(w), width, dir=(1, 0, 0))


def lid_catch():
    """Tongue + hook hanging under the lid at the front. It flexes outward as the lid
    closes, then the hook drops under a ledge in the bezel."""
    r_in, r_out, zt, h, zh = _catch_geo()
    w = S["CATCH_W"]
    tongue = Pos(0, (r_in + r_out) / 2, (zt + Z_LID) / 2 + 0.01) * Box(w, r_out - r_in, Z_LID - zt + 0.02)
    hook = _yz_prism([(r_in + 0.01, zt), (r_in - h, zt + h), (r_in - h, zh), (r_in + 0.01, zh + 0.6)], w)
    return _at(S["CATCH_ANGLE"]) * (tongue + hook)


def bezel_catch_cut():
    r_in, r_out, zt, h, zh = _catch_geo()
    w = S["CATCH_W"] + 0.6
    slot = Pos(0, (r_in - 0.05 + r_out + h + 0.15) / 2, (zt - 0.6 + Z_LID + 1) / 2) * \
        Box(w, r_out + h + 0.15 - (r_in - 0.05), Z_LID + 1 - (zt - 0.6))
    z_ledge = zh + 0.3 - S["CATCH_PRELOAD"]
    recess = Pos(0, (r_in - h - 0.25 + r_in) / 2, (zt - 0.6 + z_ledge) / 2) * Box(w, h + 0.25, z_ledge - (zt - 0.6))
    return _at(S["CATCH_ANGLE"]) * (slot + recess)


def _pin_r():
    return (sph_r(Z_LID, RI) + sph_r(Z_LID, R)) / 2 - 0.7      # middle of the lid wall, a bit inward


def lid_pins():
    d0, d1 = S["LOCPIN_D"]
    L_ = S["LOCPIN_L"]
    out = []
    for a in S["LOCPIN_ANGLES"]:
        out.append(_at(a) * Pos(0, _pin_r(), Z_LID - L_ / 2 + 0.01) * Cone(d1 / 2, d0 / 2, L_ + 0.02))
    return union(out)


def bezel_pin_sockets():
    d0, d1 = S["LOCPIN_D"]
    L_, c = S["LOCPIN_L"] + 0.5, S["LOCPIN_CLR"]
    out = []
    for a in S["LOCPIN_ANGLES"]:
        # socket a little deeper than the pin, open at the seam (extends 0.5 above it)
        out.append(_at(a) * Pos(0, _pin_r(), Z_LID - L_ / 2 + 0.25) * Cone(d1 / 2 + c, d0 / 2 + c + 0.05, L_ + 0.5))
    return union(out)


def lid_lift_notch():
    """Fingertip notch in the lid's outer bottom edge, at the catch."""
    return _at(S["CATCH_ANGLE"]) * Pos(0, sph_r(Z_LID, R), Z_LID + 0.8) * Box(14.0, 3.0, 1.6)


def bezel_ring():
    zb = S["Z_BEZEL"]
    gold = Sphere(R) & zslab(zb, Z_LID)
    gold = gold - cyl(BEZ_COLLAR_R_IN, zb - 1, Z_PLATE)
    gold = gold - cyl(R_POCKET, Z_PLATE - 0.01, Z_LIP_UNDER)
    gold = gold - cyl(S["LIP_R_IN"], Z_LIP_UNDER - 0.01, Z_LID + 1)
    # hollow the ring (cavity open at the bottom; prints flipped as an open cup)
    cav_in = SPIGOT_R_OUT + S["LUG_DEPTH"] + S["SLIDE"] + T + 0.2
    cav = (Sphere(R - T) & zslab(zb - 1, Z_LID - T)) - cyl(cav_in, zb - 2, Z_LID)
    cav = cav - cyl(R_POCKET + T, Z_PLATE - 0.5, Z_LID)          # keep wall round the display pocket
    # keep solid where the hinge and switches live
    keep = []
    for a, w in ((270.0, 44.0), (S["LID_SW_ANGLE"], 24.0), (S["PWR_SW_ANGLE"], 18.0)):
        keep.append(rev([(0, zb - 2), (R + 5, zb - 2), (R + 5, Z_LID + 1), (0, Z_LID + 1)], w, a - w / 2))
    cav = cav - union(keep)
    gold = gold - cav
    # rivets round the rim
    if ORNAMENT == "engraved":
        gold = gold.cut(*plate_edge_grooves(zb + 0.8, Z_LID - 0.8))      # stacked plate edges
    else:
        for i in range(24):
            if abs(((i * 15.0 - 270.0 + 180) % 360) - 180) > 25:
                gold = gold + rivet(i * 15.0, lat_of((zb + Z_LID) / 2, R))
    # bayonet slots
    gold = gold - bayonet_slots(SPIGOT_R_OUT, zb + 2.0, zb)
    # hinge: centre knuckle is bezel material; clear the lid knuckles' swing
    gold = gold - lid_knuckle_sweep(S["MIN_CLR"])
    gold = gold - Pos(0, AX_Y, Z_AX) * Rot(0, 90, 0) * Cylinder(S["PIN_HOLE_FREE"] / 2, 2 * (
        S["KNUCKLE_CENTER_W"] / 2 + S["KNUCKLE_GAP"] + S["KNUCKLE_SIDE_W"]) + 1)
    # pin entry: a free tunnel from the side knuckle out through the bezel wall, so the
    # rod slides in with the lid closed (plug the small hole with paint afterwards)
    x_in = S["KNUCKLE_CENTER_W"] / 2 + S["KNUCKLE_GAP"]
    gold = gold - Pos(S["PIN_ENTRY_SIDE"] * (x_in + 30), AX_Y, Z_AX) * Rot(0, 90, 0) * \
        Cylinder(S["PIN_HOLE_FREE"] / 2, 60)
    # lid switch: drops in from the top (lever up) and sits on a ledge; the lid presses
    # the lever directly. A smaller hole under the ledge lets the wires out.
    sl, sw, sh = S["LID_SW"]
    c2 = 2 * S["LID_SW_CLR"]
    pl = lid_switch_plane()
    top = Z_LID - pl.origin.Z + 2.0
    gold = gold - pl * Pos(S["LID_SW_LEVER_OVER"] / 2, 0, (-sh / 2 + top) / 2) * Box(sl + S["LID_SW_LEVER_OVER"] + c2, sw + c2, top + sh / 2)
    z_bot = S["Z_BEZEL"] - 2.0 - pl.origin.Z
    gold = gold - pl * Pos(0, 0, (z_bot - sh / 2) / 2 + 0.01) * Box(sl - 3.0, sw - 2.0, -sh / 2 - z_bot + 0.02)
    # power slide switch under the lid: body pocket open below, actuator slot on top
    bl, bw, bh = S["PWR_SW_BODY"]
    aw, ah = S["PWR_SW_ACT"]
    pp = pwr_switch_plane()
    z_lo, z_hi = zb - 2.0 - pp.origin.Z, bh / 2 + 0.1     # body pocket, open below
    g = S["PWR_SW_GIVE"]
    gold = gold - pp * Pos(0, 0, (z_lo + z_hi) / 2) * Box(bl + 2 * g, bw + 2 * g, z_hi - z_lo)
    # slot over the slider: its full travel, open through the bezel top
    gold = gold - pp * Pos(0, 0, bh / 2 + 3.0) * Box(aw + S["PWR_SW_TRAVEL"] + 2 * g + 0.2, aw + 2 * g + 0.2, 6.0)
    # front catch slot + ledge, and sockets for the lid's locating pins
    gold = gold - bezel_catch_cut() - bezel_pin_sockets()
    return {"gold": gold}


def lid_ring():
    zt = S["Z_LIDCAP"]
    gold = shell(Z_LID, zt, R, RI)
    if ORNAMENT == "engraved":
        gold = gold.cut(*plate_edge_grooves(Z_LID + 0.8, zt - 0.6))
    else:
        for i in range(12):
            a = i * 30.0 + 15.0
            if abs(((a - 270.0 + 180) % 360) - 180) > 25:
                gold = gold + rivet(a, lat_of((Z_LID + zt) / 2, R))
    gold = gold + ((lid_knuckles(0.0) & Sphere(R + S["HINGE_PROUD"] + 0.01)) - Sphere(RI))
    gold = gold - bezel_knuckle_zone(S["MIN_CLR"])
    # pin: free through the entry-side knuckle, press fit in the far one
    x_in = S["KNUCKLE_CENTER_W"] / 2 + S["KNUCKLE_GAP"]
    e = S["PIN_ENTRY_SIDE"]
    gold = gold - Pos(e * (x_in + 30), AX_Y, Z_AX) * Rot(0, 90, 0) * Cylinder(S["PIN_HOLE_FREE"] / 2, 60)
    gold = gold - Pos(-e * (x_in + 30), AX_Y, Z_AX) * Rot(0, 90, 0) * Cylinder(S["PIN_HOLE_PRESS"] / 2, 60)
    # front catch + two tapered locating pins (hold it shut, stop it sliding sideways)
    gold = gold + lid_catch() + lid_pins() - lid_lift_notch()
    # screw tabs at the top (on the bed when printed upside down)
    for a in (30.0, 150.0, 270.0):
        tab = rev([(bs(41.0), zt - 3.0), (RI + 1, zt - 3.0), (RI + 1, zt), (bs(41.0), zt)], 16.0, a - 8.0) & Sphere(RI + 0.5)
        gold = gold + tab
        gold = gold - Pos(bs(46) * math.cos(math.radians(a)), bs(46) * math.sin(math.radians(a)), zt - 2) * \
            Cylinder(S["M3_CLR"] / 2, 10)
    return {"gold": gold}


def lid_cap():
    zt = S["Z_LIDCAP"]
    gold = (Sphere(R0) & zslab(zt, S["Z_SPINDLE_SEAT"]))
    la0, la1 = S["LID_LEAF_LAT"]
    # riveted strip at the bottom edge of the cap
    zstrip = sph_r(0, R) * math.sin(math.radians(la0 - 1.0))
    gold = gold + (Sphere(R) & zslab(zt, max(zt + 2.5, zstrip)))
    # eight raised leaf panels with engraved midrib + side veins
    n = S["LID_LEAVES"]
    lc = (la0 + la1) / 2
    for i in range(n):
        th = 90.0 + i * 360.0 / n
        top = gnomonic(th, lc, 0, la1 - lc)[1]
        bot = gnomonic(th, lc, 0, la0 - lc)[1]
        length = top - bot
        half_w = gnomonic(th, lc, 180.0 / n * 0.8, 0)[0]
        leaf, rib = leaf_shape(length, 2 * half_w)
        leaf = affinity.translate(leaf, 0, (top + bot) / 2)
        rib = affinity.translate(rib, 0, (top + bot) / 2)
        veins = []
        for k in range(1, 4):
            y = bot + length * k / 4.5
            for s in (-1, 1):
                veins.append(LineString([(0, y), (s * half_w * 0.55, y + length * 0.12)]).buffer(0.3))
        gold = gold + relief(leaf, th, lc, R)
        gold = gold - cone_patch(unary_union([rib] + veins).intersection(leaf.buffer(-1.0)), th, lc, R - 0.6, R + 1)
    # inserts + spindle rod socket
    for a in (30.0, 150.0, 270.0):
        gold = gold - Pos(bs(46) * math.cos(math.radians(a)), bs(46) * math.sin(math.radians(a)),
                          zt + S["INSERT_L"] / 2 - 0.01) * Cylinder(S["INSERT_D"] / 2, S["INSERT_L"])
    gold = gold - Pos(0, 0, S["Z_SPINDLE_SEAT"] - 6) * Cylinder(S["SPINDLE_ROD_D"] / 2 + 0.05, 12.02)
    return {"gold": gold}


_SPINDLE_PX = [(34.8, 190), (34.8, 182.3), (29, 178.4), (23.2, 172.6), (21.3, 161), (27.1, 147.5),
               (29, 135.8), (25.1, 126.2), (14.5, 116.5), (9.7, 108.8), (13.5, 103), (13.5, 97.2),
               (8.7, 91.4), (6.8, 17.9), (11.6, 12.1), (11.6, 6.3), (6.8, 0.5), (7.7, -11.1),
               (14.5, -22.7), (16.4, -36.3), (12.6, -47.9), (4.8, -57.6), (0.0, -63.4)]


def _spindle_pts(px_from, px_to, min_r):
    sc = S["SPINDLE_H"] / (190 + 63.4)
    pts = [(px * sc, (190 - py) * sc) for px, py in _SPINDLE_PX if px_to <= py <= px_from]
    pts = [(max(r, min_r) if r > 0 else 0.0, z) for r, z in pts]
    fixed = [pts[0]]
    for r, z in pts[1:]:
        pr, pz = fixed[-1]
        fixed.append((min(r, pr + (z - pz)) if r > 0 else 0.0, z))
    return fixed


def spindle_profile():
    rr = S["SPINDLE_ROD_D"] / 2 + 0.05
    return _spindle_pts(190, 91.4, rr + S["SPINDLE_WALL"]), _spindle_pts(12.1, -63.4, rr + S["SPINDLE_WALL"])


def spindle():
    base, fin = spindle_profile()
    rr = S["SPINDLE_ROD_D"] / 2 + 0.05
    b = rev([(0.0, base[0][1])] + base + [(0.0, base[-1][1])]) - Cylinder(rr, 400)
    tip = fin[-1]
    fin = fin[:-1] + [(1.2, tip[1]), (0.0, tip[1])]
    f = rev([(0.0, fin[0][1])] + fin)
    hole_top = fin[0][1] + 9.0
    f = f - Pos(0, 0, (fin[0][1] + hole_top) / 2 - 0.5) * Cylinder(rr, hole_top - fin[0][1] + 1)
    return {"gold": Pos(0, 0, S["Z_SPINDLE_SEAT"]) * (b + f)}


def spindle_printed():
    """One-piece printed spindle (no exposed rod): full concept outline with
    the stem thickened to SPINDLE_PRINTED_MIN_R. A 3 mm dowel (short piece of
    rod, ~22 mm) pins it into the lid-cap socket."""
    mr = S["SPINDLE_PRINTED_MIN_R"]
    sc = S["SPINDLE_H"] / (190 + 63.4)
    raw = [(px * sc, (190 - py) * sc) for px, py in _SPINDLE_PX]
    pts = []
    for i, (r, z) in enumerate(raw):
        rc = max(r, mr) if r > 0 else 0.0
        # keep small beads/collars readable after the stem is thickened
        if 0 < i < len(raw) - 1 and r > raw[i - 1][0] - 1e-6 and r >= raw[i + 1][0] and rc - mr < 1.5:
            rc = mr + 1.5
        pts.append((rc, z))
    fixed = [pts[0]]
    for r, z in pts[1:]:                       # <=45 deg overhang printed upright
        pr, pz = fixed[-1]
        fixed.append((min(r, pr + (z - pz)) if r > 0 else 0.0, z))
    pts = fixed
    tip = pts[-1]
    pts = pts[:-1] + [(1.2, tip[1]), (0.0, tip[1])]
    body = rev([(0.0, 0.0)] + pts)
    rr = S["SPINDLE_ROD_D"] / 2 + 0.05
    body = body - Pos(0, 0, S["SPINDLE_DOWEL_DEPTH"] / 2 - 0.5) * Cylinder(rr, S["SPINDLE_DOWEL_DEPTH"] + 1)
    return {"gold": Pos(0, 0, S["Z_SPINDLE_SEAT"]) * body}


def spindle_rod():
    base, fin = spindle_profile()
    z0 = S["Z_SPINDLE_SEAT"] - 11.5
    z1 = S["Z_SPINDLE_SEAT"] + fin[0][1] + 8.5
    return Pos(0, 0, (z0 + z1) / 2) * Cylinder(S["SPINDLE_ROD_D"] / 2, z1 - z0)



# ---- medallion seat (NFC reader behind a well on the lower front) --------------------------
def nfc_plane():
    n, e, _ = frame(S["NFC_SEAT_THETA"], S["NFC_SEAT_LAT"])
    return Plane(origin=(0, 0, 0), x_dir=e, z_dir=n)          # local y = up the surface


def nfc_board_d():
    if S["NFC_BOARD_D"]:
        return S["NFC_BOARD_D"]
    nl, nw, _ = S["NFC"]
    return math.floor(math.sqrt((RI - 0.5) ** 2 - (nl / 2) ** 2 - (nw / 2) ** 2) * 4) / 4   # corners clear the wall


def _nfc_geo():
    d = nfc_board_d()
    z_in = d + S["NFC_TAPE"]                                # floor's inner face (tape between)
    z_out = z_in + S["NFC_FLOOR_T"]                         # floor's outer face
    k = 1.0 / math.tan(math.radians(S["NFC_WALL_DEG"]))     # radius grows k per mm of height
    r0 = S["NFC_FLOOR_D"] / 2
    # where the well wall meets the base surface (solve r0 + k h = sqrt(R0^2 - (z_out+h)^2))
    h = 0.0
    while r0 + k * h < math.sqrt(max(R0 ** 2 - (z_out + h) ** 2, 0)):
        h += 0.02
    r_rim = r0 + k * h
    return d, z_in, z_out, k, r0, r_rim


def _frustum(r_bot, z0, z1, k):
    return Pos(0, 0, (z0 + z1) / 2) * Cone(r_bot, r_bot + k * (z1 - z0), z1 - z0)


def nfc_seat_add():
    """Material added inside the lower shell: a flat mounting face behind the well, and
    four corner guides. The reader goes in through the bottom opening, antenna side out,
    and is held against the face with foam tape (or a few dots of hot glue)."""
    d, z_in, z_out, k, r0, r_rim = _nfc_geo()
    nl, nw, nh = S["NFC"]
    c = S["NFC_GUIDE_CLR"]
    plate = Pos(0, 0, (z_in + R0 + 4) / 2) * Box(nl + 2 * c + 5.0, nw + 2 * c + 5.0, R0 + 4 - z_in)
    guides = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            x0, y0 = sx * (nl / 2 + c + 0.8), sy * (nw / 2 + c + 0.8)
            guides.append(Pos(x0, y0 - sy * 3.0, z_in - 1.25 + 0.01) * Box(1.6, 7.6, 2.5))
            guides.append(Pos(x0 - sx * 3.0, y0, z_in - 1.25 + 0.01) * Box(7.6, 1.6, 2.5))
    return (nfc_plane() * (plate + union(guides))) & Sphere(RI + 0.3)


def nfc_seat_cut():
    """The well itself, from the floor out through the surface and the ornament."""
    d, z_in, z_out, k, r0, r_rim = _nfc_geo()
    return nfc_plane() * _frustum(r0, z_out, R + 3, k)


def nfc_seat_clear_zone():
    """Ornament (panels, straps, strips) removed around the well, so it reads as a seal."""
    _, _, _, _, _, r_rim = _nfc_geo()
    return cone_patch(SPoint(0, 0).buffer(r_rim + 3.2, 64), S["NFC_SEAT_THETA"], S["NFC_SEAT_LAT"], R0 - 0.01, R + 1)


def nfc_seat_trim():
    """A raised ring round the well with eight rivets, and a star on the floor."""
    _, _, z_out, _, _, r_rim = _nfc_geo()
    th, la = S["NFC_SEAT_THETA"], S["NFC_SEAT_LAT"]
    ring_ = SPoint(0, 0).buffer(r_rim + 2.8, 64).difference(SPoint(0, 0).buffer(r_rim + 0.2, 64))
    out = relief(ring_, th, la, R)
    # star on the floor: 8 points, 0.8 mm high
    pts = []
    for i in range(16):
        a = math.radians(i * 22.5 + 90)
        rr = 7.0 if i % 2 == 0 else 2.6
        pts.append(Vector(rr * math.cos(a), rr * math.sin(a), z_out - 0.01))
    star = extrude(Face(Wire.make_polygon(pts, close=True)), 0.81, dir=(0, 0, 1))
    return out + nfc_plane() * star


def lower_shell():
    zs, zc = S["Z_SEAM"], S["Z_BASECAP"]
    gold = shell(zc, zs, R0, RI)
    tz0, tz1 = S["TRI_LOWER"]                 # tz0 > tz1 (upper edge first)
    gold = gold + raised_band(tz0, zs)          # seam strip
    gold = gold + raised_band(zc, tz1)          # bottom strip
    panels, strap_zone, engr, beads = ornament_row(tz1, tz0, S["TRI_N"], phase=15.0, seed=2)
    tan = strap_zone - Sphere(R0) if strap_zone is not None else None
    gold = gold + panels
    if beads:
        gold = gold.fuse(*beads)
    if ORNAMENT != "engraved":
        for i in range(24):
            gold = gold + rivet(i * 15.0, lat_of((tz0 + zs) / 2, R))
    # speaker grille: 1.4 mm holes inside the down-pointing panel at -X
    grille_th, grille_lat = 180.0 + 15.0 + 7.5, lat_of((tz0 + tz1) / 2)
    holes = []
    for dx, dy in [(0, 0), (-2.6, 1.5), (2.6, 1.5), (0, 3.0), (-1.3, -1.5), (1.3, -1.5), (-5.2, 3.0), (5.2, 3.0),
                   (-3.9, 4.5), (3.9, 4.5), (-1.3, 4.5), (1.3, 4.5)]:
        holes.append(SPoint(dx, dy - 1.5).buffer(0.7, 16))
    holes = unary_union(holes)
    grille = cone_patch(holes, grille_th, grille_lat, RI - 1, R + 1)
    if engr:
        gold = gold.cut(*engr)
    # bayonet spigot, lugs, wedge, O-ring groove
    ri = S["SEAM_COLLAR_R_IN"] - S["SLIDE"] - T
    ro = ri + T
    spigot = ring(ri, ro, zs - 0.01, zs + S["SEAM_COLLAR_H"] - 0.5)
    wedge = rev([(ri, zs), (ri + 10, zs - 10), (R + 5, zs - 10), (R + 5, zs)]) & Sphere(RI + 0.01)
    gold = gold + spigot + wedge + (bayonet_lugs(ro, zs + 2.0) - cyl(ri, zs, zs + 20))
    rg = ro + 2.4
    c = S["ORING_CORD"]
    gold = gold - ring(rg - c * 0.55, rg + c * 0.55, zs - c * 0.75, zs + 0.1)
    gold = gold - grille
    if tan is not None:
        tan = tan - grille
    # medallion seat on the front: clear the ornament, sink the well, add the reader's rails
    if S["NFC_SEAT"]:
        zone = nfc_seat_clear_zone()
        gold = gold - zone
        if tan is not None:
            tan = tan - zone
        gold = gold + nfc_seat_add() + nfc_seat_trim()
        gold = gold - nfc_seat_cut()
        if tan is not None:
            tan = tan - nfc_seat_cut()
        gold = gold + (nfc_seat_trim() & nfc_seat_cut())     # the floor star survives the well cut
    # base-cap tabs
    for a in (0.0, 120.0, 240.0):
        tab = rev([(bs(37.0), zc), (R, zc), (R, zc + 3.0), (bs(37.0), zc + 3.0)], 14.0, a - 7.0) & Sphere(RI + 0.5)
        gold = gold + tab
        gold = gold - Pos(bs(42) * math.cos(math.radians(a)), bs(42) * math.sin(math.radians(a)), zc + 2) * \
            Cylinder(S["M3_CLR"] / 2, 10)
    return {"gold": gold, "tan": tan} if tan is not None else {"gold": gold}


def base_cap():
    zc, zf = S["Z_BASECAP"], S["Z_FOOT"]
    gold = Sphere(R0) & zslab(zf, zc)
    gold = gold + (Sphere(R) & zslab(zc - 2.5, zc))             # strip at the top edge
    if ORNAMENT == "engraved":
        # the bottom of the stack: raised to the full surface, lined like plate edges
        gold = gold + (Sphere(R) & zslab(zf + 4.0, zc))
        gold = gold.cut(*plate_edge_grooves(zf + 5.0, zc - 0.6))
    petals = ORNAMENT != "engraved"
    # petals
    la0, la1 = S["PETAL_LAT"]
    lc = (la0 + la1) / 2
    for i in range(S["PETALS"] if petals else 0):
        th = i * 360.0 / S["PETALS"]
        top = gnomonic(th, lc, 0, la0 - lc)[1]
        bot = gnomonic(th, lc, 0, la1 - lc)[1]
        length = top - bot
        half_w = gnomonic(th, lc, 180.0 / S["PETALS"] * 0.85, 0)[0]
        leaf, rib = leaf_shape(length, 2 * half_w)
        leaf = affinity.translate(leaf, 0, (top + bot) / 2)
        rib = affinity.translate(rib, 0, (top + bot) / 2)
        gold = gold + relief(leaf, th, lc, R)
        gold = gold - cone_patch(rib.intersection(leaf.buffer(-1.0)), th, lc, R - 0.6, R + 1)
    gold = gold & zslab(zf, zc)
    # USB-C: slot through a thin skin at the very bottom, bore + nut pocket inside
    sl, sw = S["USB_SLOT"]
    slot = SPoly(LineString([(-(sl - sw) / 2, 0), ((sl - sw) / 2, 0)]).buffer(sw / 2, 32))
    zs = zf + S["USB_SKIN"]
    w = Wire.make_polygon([Vector(x, y, zf - 1) for x, y in list(slot.exterior.coords)[:-1]], close=True)
    gold = gold - extrude(Face(w), zs - zf + 1.02, dir=(0, 0, 1))
    gold = gold - Pos(0, 0, (zs + zs + S["USB_GRIP"]) / 2) * Cylinder(S["USB_BORE"] / 2, S["USB_GRIP"])
    gold = gold - cyl(S["USB_NUT_D"] / 2, zs + S["USB_GRIP"], zc + 1)
    # cable channel on the cap top (open to the bed) toward +X
    # (at 60 deg, between two inserts, so it doesn't swallow one)
    gold = gold - Rot(0, 0, 60) * Pos(bs(22), 0, zc - 4) * Box(bs(44), 7, 8.02)
    for a in (0.0, 120.0, 240.0):
        gold = gold - Pos(bs(42) * math.cos(math.radians(a)), bs(42) * math.sin(math.radians(a)),
                          zc - S["INSERT_L"] / 2 + 0.01) * Cylinder(S["INSERT_D"] / 2, S["INSERT_L"])
    return {"gold": gold}


def chassis():
    z0, z1 = chassis_z()
    body = ring(S["DECK_R_IN"], S["DECK_R_OUT"], z0, z1)
    sx, sy = S["STANDOFF_X"] / 2, S["STANDOFF_Y"] / 2
    for x in (-sx, sx):
        for y in (-sy, sy):
            # post up to the back plate, slimmer beside the board, then a peg into the hole
            body = body + Pos(x, y, (z0 + Z_STANDOFF_END) / 2) * Cylinder(S["POST_OD"] / 2, Z_STANDOFF_END - z0)
            zt = Z_PLATE - 0.1
            body = body + Pos(x, y, (Z_STANDOFF_END + zt) / 2 - 0.01) * Cylinder(S["STANDOFF_D"] / 2, zt - Z_STANDOFF_END + 0.02)
            body = body + Pos(x, y, zt + S["PEG_L"] / 2 - 0.01) * Cylinder(S["PEG_D"] / 2, S["PEG_L"] + 0.02)
    for (cx, cy, (l, w, _)) in ((0.0, bs(42.0), S["POWERBOOST"]), (-bs(46.0), 0.0, S["LORA"]), (0.0, -bs(40.0), S["SHIFTER"])):
        fr = Pos(cx, cy, z1 + 1.0) * Box(l + 0.6 + 2 * T, w + 0.6 + 2 * T, 2.0) - Pos(cx, cy, z1 + 1.0) * Box(l + 0.6, w + 0.6, 3.0)
        body = body + (fr & cyl(S["DECK_R_OUT"], z1 - 1, z1 + 5))
    cxx, cyy = S["COMPASS_XY"]
    cl, cw_, _ = S["LIS3MDL"]
    tw, tl = cw_ + 0.6 + 2 * T, cl + 0.6 + 2 * T
    ztop = z1 + S["COMPASS_RISE"]
    tower = Pos(cxx, cyy, (z1 + ztop) / 2) * Box(tw, tl, ztop - z1) - \
        Pos(cxx, cyy, (z1 + ztop - T) / 2) * Box(tw - 2 * T, tl - 2 * T, ztop - T - z1)
    lip = Pos(cxx, cyy, ztop + 1.0) * Box(tw, tl, 2.0) - Pos(cxx, cyy, ztop + 1.0) * Box(cw_ + 0.6, cl + 0.6, 3.0)
    body = body + tower + lip
    for a in (45.0, 135.0, 225.0, 315.0):
        body = body - Pos(44 * math.cos(math.radians(a)), 44 * math.sin(math.radians(a)), z0 + 1.5) * Box(10, 10, 6)
    for x in (-27.0, 27.0):
        for y in (-27.0, 27.0):
            body = body - Pos(x, y, z0 + S["INSERT_L"] / 2 - 0.01) * Cylinder(S["INSERT_D"] / 2, S["INSERT_L"])
            body = body + (Pos(x, y, z1 + 1) * Cylinder(4.5, 2.0) - Pos(x, y, z1) * Cylinder(S["INSERT_D"] / 2, 6))
    return {"gold": body}


def battery_tray():
    z0, _ = chassis_z()
    bl, bw, bh = S["BATT"]
    wl, ww = bl + 1.0, bw + 1.0
    zf = z0 - bh - 0.6 - T
    floor = Pos(0, 0, zf + T / 2) * Box(wl + 2 * T, ww + 2 * T, T) - Pos(0, 0, zf + T / 2) * Box(wl - 12, ww - 12, T + 1)
    walls = union([Pos(0, s * (ww / 2 + T / 2), (zf + z0) / 2) * Box(wl + 2 * T, T, z0 - zf) for s in (1, -1)])
    body = floor + walls
    for x in (-27.0, 27.0):
        for s in (1, -1):
            y = s * 27.0
            body = body + Pos(x, y, z0 - 1.5) * Box(9, 9, 3)
            g = [(s * (ww / 2 + T), z0 - 3), (s * (27.0 + 4.5), z0 - 3), (s * (ww / 2 + T), z0 - 3 - (27 + 4.5 - ww / 2 - T))]
            w = Wire.make_polygon([Vector(x - 4.5, yy, zz) for yy, zz in g], close=True)
            body = body + extrude(Face(w), 9, dir=(1, 0, 0))
            body = body - Pos(x, y, z0 - 2) * Cylinder(S["M3_CLR"] / 2, 10)
    for x in (-20.0, 20.0):
        for s in (1, -1):
            body = body - Pos(x, s * (ww / 2 - 3.0), zf + T / 2) * Box(4.0, 2.0, T + 2)
    return {"gold": body}


# =============================================================================
# components for checks
# =============================================================================
def components():
    c = {}
    sx, sy = S["STANDOFF_X"] / 2, S["STANDOFF_Y"] / 2
    c["display"] = cyl(S["DISP_D"] / 2, Z_PLATE, Z_GLASS) - union(
        [Pos(x, y, Z_PLATE) * Cylinder(2.0, 2 * S["PEG_L"] + 1) for x in (-sx, sx) for y in (-sy, sy)])
    c["board"] = Pos(0, 0, (Z_BOARD + Z_PLATE) / 2 - 0.01) * Box(S["BOARD_W"], S["BOARD_H"], S["BOARD_DEPTH"] - 0.02)
    z0, z1 = chassis_z()
    bl, bw, bh = S["BATT"]
    c["battery"] = Pos(0, 0, z0 - 0.3 - bh / 2) * Box(bl, bw, bh)
    for name, (cx, cy, (l, w, h)) in (("powerboost", (0.0, bs(42.0), S["POWERBOOST"])), ("lora", (-bs(46.0), 0.0, S["LORA"])),
                                      ("shifter", (0.0, -bs(40.0), S["SHIFTER"]))):
        c[name] = Pos(cx, cy, z1 + 0.3 + h / 2) * Box(l, w, h)
    cxx, cyy = S["COMPASS_XY"]
    cl, cw_, ch = S["LIS3MDL"]
    c["compass"] = Pos(cxx, cyy, z1 + S["COMPASS_RISE"] + 0.3 + ch / 2) * Box(cw_, cl, ch)
    gb, gp = S["GPS_BOARD"], S["GPS_PATCH"]
    gpl = gps_plane()
    c["gps"] = gpl * (Pos(0, 0, gb[2] / 2 + 0.05) * Box(gb[0], gb[1], gb[2]) + Pos(0, 0, gb[2] + gp[2] / 2) * Box(gp[0], gp[1], gp[2]))
    led_face = RI - S["LED_GAP"]
    zl = sum(S["BAND_Z"]) / 2
    c["led_strip"] = ring(led_face - S["LED_STRIP_T"] + 0.05, led_face, zl - S["LED_STRIP_W"] / 2, zl + S["LED_STRIP_W"] / 2)
    sl, sw, sh = S["LID_SW"]
    lv = S["LID_SW_CLICK"] - S["LID_SW_OVERTRAVEL"] - 0.05      # lever, pressed by the closed lid
    c["lid_switch"] = lid_switch_plane() * (Box(sl, sw, sh) + Pos(S["LID_SW_LEVER_OVER"] / 2, 0, sh / 2 + lv / 2 - 0.1) * Box(sl + S["LID_SW_LEVER_OVER"], sw - 1.0, lv + 0.2))
    bl_, bw_, bh_ = S["PWR_SW_BODY"]
    c["pwr_switch"] = pwr_switch_plane() * Box(bl_, bw_, bh_)
    nl, nw, nh = S["NFC"]
    if S["NFC_SEAT"]:
        c["nfc"] = nfc_plane() * Pos(0, 0, nfc_board_d() - nh / 2) * Box(nl, nw, nh)
    else:
        c["nfc"] = Pos(0, 0, S["Z_BASECAP"] + nh / 2 + 0.3) * Box(nl, nw, nh)
    c["usb_6069"] = cyl(S["USB_BORE"] / 2 - 0.2, S["Z_FOOT"] + S["USB_SKIN"] + 0.05, S["Z_FOOT"] + S["USB_SKIN"] + 14)
    c["spindle_rod"] = spindle_rod()
    return c


def connector_keepout():
    co = S["CONN_OVERHANG"]
    bw, bh = S["BOARD_W"], S["BOARD_H"]
    zone = Pos(0, 0, (Z_BOARD + Z_PLATE) / 2) * Box(bw + 2 * co, bh + 2 * co, S["BOARD_DEPTH"] - 0.02)
    return zone - Pos(0, 0, (Z_BOARD + Z_PLATE) / 2) * Box(bw, bh, S["BOARD_DEPTH"] + 1)


PARTS = {
    "upper_shell": upper_shell, "bezel_ring": bezel_ring, "lid_ring": lid_ring, "lid_cap": lid_cap,
    "spindle": spindle, "spindle_printed": spindle_printed, "lower_shell": lower_shell, "base_cap": base_cap, "chassis": chassis,
    "battery_tray": battery_tray,
}
PRINT_FLIP = {"upper_shell": 0, "bezel_ring": 180, "lid_ring": 180, "lid_cap": 0, "spindle": 0, "spindle_printed": 0,
              "lower_shell": 0, "base_cap": 180, "chassis": 0, "battery_tray": 0}
COLOURS = {"gold": (0.86, 0.68, 0.30), "tan": (0.45, 0.30, 0.15), "clear": (0.95, 0.95, 0.88)}


def summary():
    return dict(R=R, R0=R0, RI=RI, Z_GLASS=Z_GLASS, Z_PLATE=Z_PLATE, Z_BOARD=Z_BOARD, Z_LID=Z_LID,
                AX_Y=round(AX_Y, 2), Z_AX=Z_AX, chassis_z=chassis_z(), led_face=RI - S["LED_GAP"])


# Parts that print upside down. Their 3MF is turned over so it opens ready to print;
# the STL and STEP files stay in assembly position (check.py uses the STLs).
PRINT_FLIPPED = {"bezel_ring", "lid_ring", "base_cap"}


def export_3mf(name, out):
    """Build <name>.3mf from the part's colour STLs, in print orientation."""
    import trimesh
    import numpy as np
    scene = trimesh.Scene()
    flip = trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]) if name in PRINT_FLIPPED else np.eye(4)
    for col in ("gold", "tan", "clear"):
        f = out / f"{name}__{col}.stl"
        if not f.exists():
            continue
        m = trimesh.load(str(f))
        m.apply_transform(flip)
        m.visual.face_colors = [int(255 * v) for v in COLOURS[col]] + [255]
        m.metadata["name"] = f"{name}_{col}"
        scene.add_geometry(m, node_name=f"{name}_{col}", geom_name=f"{name}_{col}")
    (out / f"{name}.3mf").write_bytes(trimesh.exchange.threemf.export_3MF(scene))


def export_part(name, bodies, out):
    import trimesh
    meshes = []
    for col, solid in bodies.items():
        f = out / f"{name}__{col}.stl"
        export_stl(solid, str(f), tolerance=0.02, angular_tolerance=0.05)
        m = trimesh.load(str(f))
        m.visual.face_colors = [int(255 * v) for v in COLOURS[col]] + [255]
        m.metadata["name"] = f"{name}_{col}"
        meshes.append((col, m))
    comp = Compound(children=list(bodies.values())) if len(bodies) > 1 else list(bodies.values())[0]
    export_step(comp, str(out / f"{name}.step"))
    export_3mf(name, out)


if __name__ == "__main__":
    import time, sys
    out = OUT_DIR
    out.mkdir(parents=True, exist_ok=True)
    print(summary())
    only = sys.argv[1:]
    for name, fn in PARTS.items():
        if only and name not in only:
            continue
        t0 = time.time()
        b = fn()
        print(f"{name:13s} " + "  ".join(f"{c}={s.volume/1000:6.1f}cm3 ok={s.is_valid}" for c, s in b.items())
              + f"  {time.time()-t0:5.1f}s", flush=True)
        export_part(name, b, out)
