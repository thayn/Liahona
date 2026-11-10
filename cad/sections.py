import glob, trimesh
import render as RD, liahona as L
from build123d import export_stl
items = []
for f in glob.glob("out/*__*.stl"):
    n, col = f.split("/")[-1][:-4].split("__")
    items.append((trimesh.load(f), L.COLOURS[col], n))
comps = L.components()
cc = {"display": RD.DARK, "board": (0.1, 0.45, 0.3), "battery": (0.3, 0.4, 0.6), "gps": (0.1, 0.5, 0.1),
      "led_strip": (1, 1, 1), "lid_switch": (0.1, 0.1, 0.1), "pwr_switch": (0.6, 0.1, 0.1), "nfc": (0.7, 0.1, 0.1),
      "usb_6069": (0.2, 0.2, 0.2), "compass": (0.5, 0.15, 0.3), "powerboost": (0.15, 0.2, 0.5), "spindle_rod": (0.8, 0.6, 0.25)}
for k, c in cc.items():
    export_stl(comps[k], f"out/_c_{k}.stl", tolerance=0.05, angular_tolerance=0.1)
    items.append((trimesh.load(f"out/_c_{k}.stl"), c, k))
RD.section(items, "out/v2_section_x0.png", normal=(1, 0, 0), title="Section X=0 (back/hinge at left)", extent=95)
import math
t = math.radians(L.S["GPS_ANGLE"])
RD.section(items, "out/v2_section_gps.png", normal=(-math.sin(t), math.cos(t), 0), title="Section through the GPS (20 deg)", extent=95)
t = math.radians(L.S["LID_SW_ANGLE"])
RD.section(items, "out/v2_section_lidsw.png", normal=(-math.sin(t), math.cos(t), 0), title="Section through the lid switch", extent=95)
