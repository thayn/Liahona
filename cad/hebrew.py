"""Shape Hebrew text with HarfBuzz (RTL order + niqqud placement) and return
2D outlines as shapely polygons in mm.

CAD text tools do not shape RTL text or position niqqud, so the band text
is shaped here and handed to build123d as plain polygons.
"""
import uharfbuzz as hb
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen
from shapely.geometry import Polygon
from shapely.ops import unary_union


class _FlatPen(BasePen):
    """Collects glyph contours as polylines (curves flattened)."""

    def __init__(self, glyphset, steps=8):
        super().__init__(glyphset)
        self.contours, self.cur, self.steps = [], [], steps

    def _moveTo(self, p):
        self.cur = [p]

    def _lineTo(self, p):
        self.cur.append(p)

    def _curveToOne(self, p1, p2, p3):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            mt = 1 - t
            self.cur.append((
                mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0],
                mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]))

    def _qCurveToOne(self, p1, p2):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            mt = 1 - t
            self.cur.append((mt**2 * p0[0] + 2 * mt * t * p1[0] + t**2 * p2[0],
                             mt**2 * p0[1] + 2 * mt * t * p1[1] + t**2 * p2[1]))

    def _closePath(self):
        if len(self.cur) > 2:
            self.contours.append(self.cur)
        self.cur = []

    _endPath = _closePath


def _contours_to_geom(contours):
    """Even-odd fill of contours -> shapely geometry."""
    geom = None
    for c in contours:
        p = Polygon(c).buffer(0)
        if p.is_empty:
            continue
        geom = p if geom is None else geom.symmetric_difference(p)
    return geom


def shape_text(text, font_path, letter_height, letter_ref="ר"):
    """Return (glyphs, total_width).

    glyphs: list of dicts {geom: shapely geometry (mm), cluster: int, mark: bool}
    in visual left-to-right order. x=0 at the left end of the line, y=0 at the
    baseline. Scaled so the height of `letter_ref` equals letter_height.
    """
    blob = hb.Blob.from_file_path(font_path)
    face = hb.Face(blob)
    font = hb.Font(face)
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()  # -> RTL, Hebrew script
    hb.shape(font, buf, {"kern": True, "mark": True, "mkmk": True})

    tt = TTFont(font_path)
    gs = tt.getGlyphSet()
    order = tt.getGlyphOrder()

    # scale from the reference letter's bbox height
    ref = gs[tt.getBestCmap()[ord(letter_ref)]]
    pen = _FlatPen(gs)
    ref.draw(pen)
    ys = [p[1] for c in pen.contours for p in c]
    scale = letter_height / (max(ys) - min(ys))

    # HarfBuzz returns RTL runs in visual order (left to right) already.
    x = 0.0
    out = []
    for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
        name = order[info.codepoint]
        pen = _FlatPen(gs)
        gs[name].draw(pen)
        ox, oy = x + pos.x_offset, pos.y_offset
        cont = [[((px + ox) * scale, (py + oy) * scale) for px, py in c] for c in pen.contours]
        g = _contours_to_geom(cont) if cont else None
        if g is not None and not g.is_empty:
            out.append({"geom": g, "cluster": info.cluster, "advance": pos.x_advance == 0})
        x += pos.x_advance
    return out, x * scale


def band_polygons(text, font_path, letter_height, min_feature=0.0):
    """Union all glyph geometry into a list of simple polygons (with holes)."""
    glyphs, width = shape_text(text, font_path, letter_height)
    geom = unary_union([g["geom"] for g in glyphs])
    polys = list(geom.geoms) if hasattr(geom, "geoms") else [geom]
    return polys, width


def clusters(text, font_path, letter_height):
    """Group shaped glyphs into clusters (letter + its niqqud).
    Returns ([(geom, x_centre_of_base_letter), ...] in visual order, None for spaces)."""
    glyphs, width = shape_text(text, font_path, letter_height)
    by = {}
    order = []
    for g in glyphs:
        c = g["cluster"]
        if c not in by:
            by[c] = []
            order.append(c)
        by[c].append(g)
    out = []
    for c in order:
        gs = by[c]
        geom = unary_union([g["geom"] for g in gs])
        # base letter = largest piece
        base = max(gs, key=lambda g: g["geom"].area)["geom"]
        b = base.bounds
        out.append((geom, (b[0] + b[2]) / 2, c))
    # words: split where the cluster index jumps over a space
    return out, width
