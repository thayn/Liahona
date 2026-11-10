"""Assembly, collision/clearance, overhang and wall checks + renders."""
import math, json, itertools, time
from pathlib import Path
import numpy as np
import trimesh
from build123d import Pos, Rot, export_stl
import liahona as L
import render as RD

OUT = Path(__file__).parent / "out"
REPORT = {}


def to_mesh(shape, tol=0.05, ang=0.1):
    tmp = OUT / "_tmp.stl"
    export_stl(shape, str(tmp), tolerance=tol, angular_tolerance=ang)
    return trimesh.load(str(tmp))


def load_parts():
    """Merge each part's colour STLs into one mesh (built by liahona.py)."""
    import glob
    meshes, bodies = {}, {}
    for n in L.PARTS:
        fs = sorted(glob.glob(str(OUT / f"{n}__*.stl")))
        ms = [trimesh.load(f) for f in fs]
        bodies[n] = {f.split("__")[1][:-4]: m for f, m in zip(fs, ms)}
        meshes[n] = trimesh.boolean.union(ms, engine="manifold") if len(ms) > 1 else ms[0]
    return meshes, bodies


def bbox_overlap(a, b, pad=0.0):
    ba, bb = a.bounding_box(), b.bounding_box()
    return not (ba.max.X + pad < bb.min.X or bb.max.X + pad < ba.min.X or
                ba.max.Y + pad < bb.min.Y or bb.max.Y + pad < ba.min.Y or
                ba.max.Z + pad < bb.min.Z or bb.max.Z + pad < ba.min.Z)


def collisions(meshes, tol_mm3=1.0, skip=()):
    """Pairwise intersection volume using manifold3d on meshes."""
    hits = []
    names = list(meshes)
    for a, b in itertools.combinations(names, 2):
        if a.startswith("letter_ring") and b.startswith("letter_ring"):
            continue
        if (a, b) in skip or (b, a) in skip:
            continue
        A, B = meshes[a], meshes[b]
        lo = np.maximum(A.bounds[0], B.bounds[0]); hi = np.minimum(A.bounds[1], B.bounds[1])
        if np.any(lo > hi):
            continue
        try:
            v = trimesh.boolean.intersection([A, B], engine="manifold").volume
        except Exception:
            v = float("nan")
        if not (v < tol_mm3):
            hits.append((a, b, round(float(v), 2)))
    return hits


def clearances(solids, pairs):
    out = []
    for a, b in pairs:
        d = solids[a].distance_to(solids[b])
        out.append((a, b, round(d, 2)))
    return out


def overhang_report(mesh, name, limit=50.0):
    """Area of downward faces steeper than `limit` deg from vertical, ignoring
    the first layer on the bed and small bridges (reported separately)."""
    flip = L.PRINT_FLIP.get(name.split("_")[0] if name.startswith("letter") else name, 0)
    if name.startswith("letter_ring"):
        flip = 0
    m = mesh.copy()
    if flip:
        m.apply_transform(trimesh.transformations.rotation_matrix(math.radians(flip), [1, 0, 0]))
    zmin = m.bounds[0][2]
    m.apply_translation([0, 0, -zmin])
    n = m.face_normals
    c = m.triangles_center
    a = m.area_faces
    down = n[:, 2] < 0
    ang = np.degrees(np.arcsin(np.clip(-n[:, 2], 0, 1)))  # 0 = vertical wall, 90 = ceiling
    on_bed = c[:, 2] < 0.3
    bad = down & (ang > limit) & ~on_bed
    horiz = bad & (ang > 85)
    return dict(height=round(m.bounds[1][2], 1), steep_area_mm2=round(a[bad].sum(), 1),
                ceiling_area_mm2=round(a[horiz].sum(), 1),
                steep_regions=cluster_points(c[bad], a[bad]))


def cluster_points(pts, areas, eps=4.0, top=6):
    if len(pts) == 0:
        return []
    from scipy.cluster.hierarchy import fcluster, linkage
    if len(pts) > 4000:
        idx = np.random.default_rng(0).choice(len(pts), 4000, replace=False)
        pts, areas = pts[idx], areas[idx]
    if len(pts) == 1:
        return [dict(at=np.round(pts[0], 1).tolist(), area=round(float(areas[0]), 1))]
    lab = fcluster(linkage(pts, "single"), eps, "distance")
    res = []
    for k in np.unique(lab):
        sel = lab == k
        res.append(dict(at=np.round(pts[sel].mean(0), 1).tolist(), area=round(float(areas[sel].sum()), 1),
                        span=round(float(np.ptp(pts[sel], 0).max()), 1)))
    return sorted(res, key=lambda r: -r["area"])[:top]


def wall_report(mesh, min_wall=2.3, min_len=5.0):
    """Thin-wall finder on cross-sections: morphological opening of every
    section polygon by min_wall/2; leftover slivers longer than min_len are
    reported (sharp corners and knife edges at split planes are short)."""
    from shapely.geometry import MultiPolygon
    h = min_wall / 2
    spots = []
    planes = []
    for ang in range(0, 180, 15):
        a = math.radians(ang)
        planes.append(((0, 0, 0), (math.cos(a), math.sin(a), 0)))
    zlo, zhi = mesh.bounds[0][2], mesh.bounds[1][2]
    for z in np.arange(zlo + 0.7, zhi - 0.5, 2.0):
        planes.append(((0, 0, z), (0, 0, 1)))
    for org, nrm in planes:
        sec = mesh.section(plane_origin=org, plane_normal=nrm)
        if sec is None:
            continue
        try:
            planar, M = sec.to_2D()
        except Exception:
            continue
        for poly in planar.polygons_full:
            opened = poly.buffer(-h, join_style=2).buffer(h, join_style=2)
            diff = poly.difference(opened.buffer(0.05))
            geoms = diff.geoms if hasattr(diff, "geoms") else [diff]
            for g in geoms:
                if g.is_empty or g.area < 0.3:
                    continue
                mrr = g.minimum_rotated_rectangle
                xs, ys = mrr.exterior.xy
                e = [math.hypot(xs[i + 1] - xs[i], ys[i + 1] - ys[i]) for i in range(2)]
                length, width = max(e), min(e)
                if length >= min_len:
                    c = np.array([g.centroid.x, g.centroid.y, 0, 1.0])
                    p3 = (M @ c)[:3]
                    spots.append(dict(at=np.round(p3, 1).tolist(), len=round(length, 1),
                                      w=round(width, 2), plane=("z=%.1f" % org[2]) if nrm[2] else "vert"))
    # merge spots that are close
    merged = []
    for sp in sorted(spots, key=lambda d: -d["len"]):
        if all(np.linalg.norm(np.array(sp["at"]) - np.array(m["at"])) > 6 for m in merged):
            merged.append(sp)
    return dict(count=len(spots), spots=merged[:12])


def main():
    t0 = time.time()
    meshes, bodies = load_parts()
    comps = L.components()
    cm = {f"[{k}]": to_mesh(v, 0.03, 0.08) for k, v in comps.items()}
    cm["[connector_keepout]"] = to_mesh(L.connector_keepout(), 0.03, 0.08)
    allm = dict(meshes); allm.update(cm)
    REPORT["mesh"] = {n: {c: dict(watertight=bool(m.is_watertight), cm3=round(m.volume / 1000, 1))
                          for c, m in b.items()} for n, b in bodies.items()}
    expected = {("[standoffs]", "[connector_keepout]"), ("[board]", "[standoffs]"),
                ("spindle", "[spindle_rod]"), ("lid_cap", "[spindle_rod]"), ("plunger", "[lid_switch]")}
    REPORT["collisions"] = collisions(allm, skip=expected)
    print("collisions:", REPORT["collisions"], flush=True)
    # lid swing
    Tm = trimesh.transformations
    swing = []
    for a in range(0, int(L.S["LID_OPEN_DEG"]) + 1, 10):
        M = Tm.translation_matrix([0, L.AX_Y, L.Z_AX]) @ Tm.rotation_matrix(math.radians(a), [1, 0, 0]) @ \
            Tm.translation_matrix([0, -L.AX_Y, -L.Z_AX])
        v = 0.0
        for nm in ("lid_ring", "lid_cap", "spindle"):
            mv = allm[nm].copy(); mv.apply_transform(M)
            for other in ("bezel_ring", "upper_shell", "[display]", "plunger"):
                v += trimesh.boolean.intersection([mv, allm[other]], engine="manifold").volume
        swing.append((a, round(float(v), 2)))
    REPORT["lid_swing_overlap_mm3"] = swing
    print("swing:", swing, flush=True)
    # module insertion from the top
    moving = ["chassis", "battery_tray", "[display]", "[board]", "[standoffs]", "[battery]",
              "[powerboost]", "[lora]", "[shifter]", "[compass]"]
    static = ["upper_shell", "[gps]", "[led_strip]"]
    ins = []
    for dz in range(2, 90, 4):
        v = 0.0
        for mname in moving:
            mv = allm[mname].copy(); mv.apply_translation([0, 0, dz])
            for sname in static:
                v += trimesh.boolean.intersection([mv, allm[sname]], engine="manifold").volume
        ins.append((dz, round(float(v), 2)))
    REPORT["module_insertion_overlap_mm3"] = ins
    print("insertion:", ins, flush=True)
    # clearances (mesh distance via sampling)
    def mdist(a, b):
        pa = allm[a].sample(4000)
        d = trimesh.proximity.ProximityQuery(allm[b]).signed_distance(pa)
        return round(float(-d.max()), 2)
    pairs = [("[gps]", "upper_shell"), ("[gps]", "[compass]"), ("[led_strip]", "upper_shell"),
             ("[lid_switch]", "bezel_ring"), ("[pwr_switch]", "bezel_ring"), ("[display]", "bezel_ring"),
             ("chassis", "upper_shell"), ("[battery]", "[compass]"), ("[usb_6069]", "base_cap"),
             ("[nfc]", "battery_tray")]
    REPORT["clearances"] = [(a, b, mdist(a, b)) for a, b in pairs]
    print("clearances:", REPORT["clearances"], flush=True)
    # print checks per part
    REPORT["overhang"], REPORT["walls"] = {}, {}
    for n, m in meshes.items():
        REPORT["overhang"][n] = overhang_report(m, n)
        REPORT["walls"][n] = wall_report(m)
        print(n, REPORT["overhang"][n]["steep_area_mm2"], REPORT["walls"][n]["count"], flush=True)
    (OUT / "report.json").write_text(json.dumps(REPORT, indent=1))
    print("done", round(time.time() - t0, 1))


if __name__ == "__main__":
    main()
