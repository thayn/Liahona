"""Assembly renders: front, back, lid open, section."""
import math, glob, sys, trimesh
import render as RD
import liahona as L
T = trimesh.transformations
def part(n):
    return [(trimesh.load(f), L.COLOURS[f.split("__")[1][:-4]]) for f in glob.glob(f"out/{n}__*.stl")]
shell_parts = ["upper_shell", "bezel_ring", "lid_ring", "lid_cap", "spindle", "lower_shell", "base_cap"]
rod = trimesh.load_mesh(trimesh.util.wrap_as_stream(b"")) if False else None
items = sum([part(n) for n in shell_parts], [])
from build123d import export_stl as _e
_e(L.spindle_rod(), "out/_rod.stl"); items.append((trimesh.load("out/_rod.stl"), (0.85, 0.65, 0.3)))
which = sys.argv[1:] or ["front", "back", "open", "section"]
if "front" in which:
    RD.render(items, "out/v2_front.png", elev=16, azim=100, title="Front", size=8, dpi=120)
if "back" in which:
    RD.render(items, "out/v2_back.png", elev=12, azim=-80, title="Back (hinge)", size=8, dpi=120)
if "bottom" in which:
    RD.render(items, "out/v2_bottom.png", elev=-60, azim=-60, title="Bottom (USB-C)", size=8, dpi=120)
if "open" in which:
    M = T.translation_matrix([0, L.AX_Y, L.Z_AX]) @ T.rotation_matrix(math.radians(100), [1, 0, 0]) @ T.translation_matrix([0, -L.AX_Y, -L.Z_AX])
    it = []
    for n in shell_parts:
        for m, c in part(n):
            if n in ("lid_ring", "lid_cap", "spindle"):
                m = m.copy(); m.apply_transform(M)
            it.append((m, c))
    from build123d import export_stl
    export_stl(L.components()["display"], "out/_disp.stl")
    it.append((trimesh.load("out/_disp.stl"), RD.DARK))
    RD.render(it, "out/v2_open.png", elev=40, azim=70, title="Lid open", size=8, dpi=120)
