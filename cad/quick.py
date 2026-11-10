import sys, trimesh, glob
import render as RD
from liahona import COLOURS
names = sys.argv[1].split(",")
items = []
for n in names:
    for f in glob.glob(f"out/{n}__*.stl"):
        col = f.split("__")[1][:-4]
        items.append((trimesh.load(f), COLOURS[col]))
RD.render(items, sys.argv[2], elev=float(sys.argv[3]), azim=float(sys.argv[4]), size=8, dpi=120)
