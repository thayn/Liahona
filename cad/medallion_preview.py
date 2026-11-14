"""Previews for the medallion: paint map (front), back, cap, and a 3D view."""
from pathlib import Path
import numpy as np, trimesh, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
from build123d import Pos, Circle, Plane, mirror
import medallion as MD
import render as RD

OUT = Path(__file__).parent / "out"
DARK = (0.17, 0.23, 0.19); CREAM = (0.93, 0.88, 0.76); GREY = (0.78, 0.78, 0.78)

def fill(ax, shape, color):
    tris = []
    for f in shape.faces():
        v, t = f.tessellate(0.02, 0.1)
        v = np.array([(p.X, p.Y) for p in v])
        tris += [v[list(tri)] for tri in t]
    ax.add_collection(PolyCollection(tris, facecolors=color, edgecolors=color, linewidths=0.2))

S = MD.S
hexf, border, art, words, field = MD.front_art()
outline = hexf + Pos(0, S["BAIL_Y"]) * Circle(S["BAIL_R"]) - Pos(0, S["BAIL_Y"] + 0.4) * Circle(S["HOLE_D"] / 2)

fig, axs = plt.subplots(1, 3, figsize=(15, 6.6))
ax = axs[0]; fill(ax, outline, DARK); fill(ax, border + art + words, CREAM); ax.set_title("Front: paint map (cream = raised)")
ax = axs[1]
py = S["POCKET_Y"]
back = outline - Pos(0, py) * Circle(S["POCKET_D"] / 2) - Pos(0, py - S["POCKET_D"] / 2 + 0.4) * Circle(S["NOTCH_R"])
bt = (MD.text_fit(S["BACK_TOP"], S["BACK_TEXT_H"], 18.0, max_w=24) + MD.text_fit(S["BACK_BOTTOM"], S["BACK_TEXT_H"], -20.0, max_w=21))
fill(ax, back, GREY); fill(ax, bt, (0.45, 0.45, 0.45)); ax.set_title("Back: NFC pocket + engraved text")
ax = axs[2]; fill(ax, Circle(S["CAP_D"] / 2), DARK); fill(ax, MD.star(0, 0, 10.5, 6.5) + (Circle(9.8) - Circle(9.0)), CREAM); ax.set_title("Cap (star faces out)")
for ax, lim in zip(axs, [(-26, 26, -30, 39), (-26, 26, -30, 39), (-16, 16, -16, 16)]):
    ax.set_xlim(lim[0], lim[1]); ax.set_ylim(lim[2], lim[3]); ax.set_aspect("equal"); ax.axis("off")
plt.tight_layout(); plt.savefig(OUT / "medallion_preview.png", dpi=160)

body = trimesh.load(OUT / "medallion.stl"); body.merge_vertices()
print("watertight", body.is_watertight, "bodies", len(body.split()))
RD.render([(body, (0.85, 0.68, 0.32))], OUT / "medallion_3d.png", elev=50, azim=-80)
