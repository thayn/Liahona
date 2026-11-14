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

z0, z1 = MD.slot_z()
fig, axs = plt.subplots(1, 3, figsize=(16, 6.8), gridspec_kw=dict(width_ratios=[1, 1, 1.15]))
ax = axs[0]; fill(ax, outline, DARK); fill(ax, border + art + words, CREAM); ax.set_title("Front: paint map (cream = raised)")
ax = axs[1]
bt = (MD.text_fit(S["BACK_TOP"], S["BACK_TEXT_H"], 18.0, max_w=24) + MD.text_fit(S["BACK_BOTTOM"], S["BACK_TEXT_H"], -20.0, max_w=21))
fill(ax, outline, GREY); fill(ax, bt, (0.45, 0.45, 0.45))
r = (S["TAG_D"] + S["SLOT_CLEAR_D"]) / 2
ax.add_patch(plt.Circle((0, S["SLOT_Y"]), r, fill=False, ls="--", lw=1.5, ec=(0.2, 0.4, 0.8)))
ax.text(0, S["SLOT_Y"], "hidden tag slot\n%.1f mm" % (2 * r), ha="center", va="center", color=(0.2, 0.4, 0.8), fontsize=11)
ax.set_title("Back: engraved text (tag sealed inside)")
for ax in axs[:2]:
    ax.set_xlim(-26, 26); ax.set_ylim(-30, 39); ax.set_aspect("equal"); ax.axis("off")

# side section through the middle
body = trimesh.load(OUT / "medallion.stl"); body.merge_vertices()
print("watertight", body.is_watertight, "bodies", len(body.split()))
sec = body.section(plane_origin=(0, 0, 0), plane_normal=(1, 0, 0))
ax = axs[2]
p2 = sec.discrete
for e in p2:
    ax.fill(e[:, 1], e[:, 2], color=(0.85, 0.68, 0.32), lw=0)
# show the slot (a void) in white so the tag reads clearly
rs = (S["TAG_D"] + S["SLOT_CLEAR_D"]) / 2
ax.add_patch(plt.Rectangle((S["SLOT_Y"] - rs, z0), 2 * rs, z1 - z0, color="white"))
ax.add_patch(plt.Rectangle((S["SLOT_Y"] - S["TAG_D"] / 2, z0), S["TAG_D"], S["TAG_T"], color=(0.2, 0.4, 0.8)))
ax.axhline(z1, color="r", lw=1, ls=":")
ax.annotate("PAUSE here: after the layer ending at %.2f mm,\ndrop the tag in, resume" % z1, xy=(-10, z1), xytext=(-29, 6.6),
            arrowprops=dict(arrowstyle="->", color="r"), color="r", fontsize=10)
ax.annotate("tag (%.1f mm) sits on a %.1f mm floor" % (S["TAG_T"], z0), xy=(-4, z0 + 0.5), xytext=(-12, -1.6),
            arrowprops=dict(arrowstyle="->", color=(0.2, 0.4, 0.8)), color=(0.2, 0.4, 0.8), fontsize=10)
ax.set_xlim(-31, 39); ax.set_ylim(-2.5, 8); ax.set_aspect(3)
ax.set_xlabel("mm (cut through the centre, bottom on the bed)"); ax.set_yticks([0, z0, z1, 4, 4.8]); ax.grid(axis="y", alpha=0.3)
ax.set_title("Side section (height exaggerated 3x)")
plt.tight_layout(); plt.savefig(OUT / "medallion_preview.png", dpi=150)
