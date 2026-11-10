"""Simple shaded renders and section cuts with matplotlib + trimesh."""
import numpy as np
import trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

GOLD = (0.85, 0.68, 0.32)
FROST = (0.93, 0.93, 0.88)
GREY = (0.55, 0.57, 0.6)
DARK = (0.2, 0.22, 0.25)


def shade(mesh, color, light=(0.4, -0.6, 0.8)):
    n = mesh.face_normals
    l = np.array(light) / np.linalg.norm(light)
    k = np.clip(n @ l, 0, 1) * 0.65 + 0.35
    c = np.array(color)[None, :] * k[:, None]
    return np.clip(c, 0, 1)


def render(items, path, elev=20, azim=-60, title=None, lim=None, size=7, dpi=130):
    """items: list of (trimesh, rgb)."""
    fig = plt.figure(figsize=(size, size))
    ax = fig.add_subplot(111, projection="3d")
    polys, cols = [], []
    for m, c in items:
        polys.append(m.vertices[m.faces])
        cols.append(shade(m, c))
    pc = Poly3DCollection(np.concatenate(polys), facecolors=np.concatenate(cols), linewidths=0)
    ax.add_collection3d(pc)
    allv = np.concatenate([m.vertices for m, _ in items])
    lo, hi = allv.min(0), allv.max(0)
    ctr = (lo + hi) / 2
    r = lim or (hi - lo).max() / 2
    ax.set_xlim(ctr[0] - r, ctr[0] + r)
    ax.set_ylim(ctr[1] - r, ctr[1] + r)
    ax.set_zlim(ctr[2] - r, ctr[2] + r)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()
    if title:
        ax.set_title(title)
    plt.tight_layout()
    plt.savefig(path, dpi=dpi)
    plt.close(fig)


def section(items, path, normal=(1, 0, 0), origin=(0, 0, 0), title=None, extent=85, labels=None):
    """2D section through several meshes. items: list of (mesh, rgb, name)."""
    fig, ax = plt.subplots(figsize=(8, 8))
    n = np.array(normal, float)
    # in-plane axes: for X normal -> (Y, Z); for Y normal -> (X, Z)
    if abs(n[0]) > 0.5:
        u, v = np.array([0, 1, 0]), np.array([0, 0, 1])
    else:
        u, v = np.array([1, 0, 0]), np.array([0, 0, 1])
    for m, c, name in items:
        sec = m.section(plane_origin=origin, plane_normal=n)
        if sec is None:
            continue
        M = np.eye(4)
        M[:3, :3] = np.vstack([u, v, n])
        M[:3, 3] = -M[:3, :3] @ np.array(origin, float)
        planar, _ = sec.to_2D(to_2D=M)
        for poly in planar.polygons_full:
            from matplotlib.path import Path as MPath
            from matplotlib.patches import PathPatch
            rings = [np.asarray(poly.exterior.coords)] + [np.asarray(i.coords) for i in poly.interiors]
            verts = np.concatenate(rings)
            codes = np.concatenate([[MPath.MOVETO] + [MPath.LINETO] * (len(r) - 2) + [MPath.CLOSEPOLY]
                                    for r in rings])
            ax.add_patch(PathPatch(MPath(verts, codes), fc=c, ec="k", lw=0.4, alpha=0.9))
    ax.set_aspect("equal")
    ax.set_xlim(-extent, extent)
    ax.set_ylim(-extent, extent + 20)
    ax.grid(alpha=0.25)
    if title:
        ax.set_title(title)
    if labels:
        from matplotlib.patches import Patch
        ax.legend(handles=[Patch(color=c, label=l) for l, c in labels], loc="lower left", fontsize=7)
    plt.tight_layout()
    plt.savefig(path, dpi=130)
    plt.close(fig)
