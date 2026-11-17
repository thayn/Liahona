# Liahona CAD, v3: 160 mm ball for the 3.4" screen

`python3 liahona.py` rebuilds every part into `out/`. `python3 check.py` re-runs the checks. All dimensions are in the `S = dict(...)` settings block in `liahona.py`. Coordinates: ball centre at the origin, +Z up (display faces up), hinge at −Y (back).

## Sizes
| Size | Screen | Glass | Build |
|---|---|---|---|
| **160 mm** (default) | ESP32-P4-WIFI6-Touch-LCD-**3.4C**, 800 × 800 | 115 mm, 6 mm thick | `python3 liahona.py` → `out/` |
| 175 mm | ESP32-P4-WIFI6-Touch-LCD-4C, 720 × 720 | 126 mm | `LIAHONA_SIZE=175 python3 liahona.py`. The last 175 mm print files are in `out/175mm/` |

The ball's shape (radius, split heights, ornament, band, letters, insert circles, spindle) scales with the ball. Bought parts keep their real sizes: switches, battery, boards, magnets, the 2 mm hinge rod and the 3 mm spindle rod.

## What changed in v3
- **160 mm ball** around the 115 mm glass. There's still a 9 mm shoulder round the glass.
- **Hinge pin can go in now.** A 2.2 mm channel runs from the right-hand knuckle (seen from the front, at the back) out through the bezel wall. Close the lid, push a **32 mm piece of 2 mm brass rod** in from the right until it stops (it ends about 1 mm inside), then dab the hole with paint. The right lid knuckle is a free fit; the left one grips the rod (2.05 mm), so the rod can't walk out.
- **Chassis boards** (PowerBoost, LoRa, compass) moved in, so the module still drops in through the smaller display spigot. The spigot has four shallow grooves where the chassis posts pass.
- **GPS** sits steeper (65°) and a little lower to fit the smaller shoulder.
- **Rivet strip** under the bezel kept at full height.
- **Lid switch from measurements:** 12.8 × 5.8 mm body, lever 3.83 mm above the body when free, clicks at 2.04 mm.
  - The switch sits lower, hanging about 1 mm below the bezel, to leave room above it for the free lever and the plunger collar.
  - With the lid shut, the lid pushes the plunger down until the lever is 0.5 mm past the click.
  - With the lid open, the plunger stands about 2.3 mm above the bezel top.
  - The plunger is a 5.7 mm shaft with a 4 mm collar. Put the plunger in before the switch.
- **Power switch** body is now the measured 9.11 × 3.58 mm.

## Files per part
- `<part>.3mf`: every colour body already aligned. In Bambu Studio, open it, choose "load as one object with multiple parts", then assign a filament to each part.
- `<part>__gold.stl`, `__tan.stl`, `__clear.stl`: the same bodies as separate STLs.
- `<part>.step`: editable in Fusion or Onshape.

## Colours
- **Gold:** shells, raised triangle panels, frames, rivets, lid, spindle.
- **Darker tan:** the equator band and the straps between the triangle panels.
- **Clear:** the letters, printed through the full wall of the band, so the LEDs shine straight through them. No separate letter ring is needed.

To print the straps in gold, merge `__tan` into gold for the upper/lower shell. That cuts most of the filament swaps.

## Parts and print orientation
| Part | Orientation | Notes |
|---|---|---|
| upper_shell (gold/tan/clear) | seam (z −24) down | Triangle row, rivet strip, band with 3 text repeats and lit diamonds. Inside: seam collar, LED ring 9 mm behind the letters, display spigot, shoulder web, tilted GPS bracket |
| bezel_ring | flipped | Riveted rim, display pocket + lip, twist-lock, hinge centre knuckle. Hidden pockets for the lid switch (with plunger) and the power slide switch |
| lid_ring | upside down | Riveted rim, two lid knuckles (0.8 mm proud, no outside block) |
| lid_cap | flat face down | Eight raised leaf panels with veins. Solid: use ~10 % infill |
| spindle | upright, 0.2 mm nozzle | Turned base + finial; the 3 mm brass rod is the stem. Scaled to 76 mm for the bigger ball |
| lower_shell (gold/tan) | bottom down | Seam strip with rivets, lower triangle row, speaker grille holes inside one panel at −X, bayonet spigot + O-ring groove |
| base_cap | flipped (flat top on the bed) | Twelve raised petals, flat foot, USB-C slot at the very bottom centre, 6069 bore + nut pocket, cable channel |
| chassis, battery_tray, plunger | as before | Any PETG |

## What changed from v1 (v2, 175 mm)
- Ball is 175 mm. The lid seam moved up to z 52, giving a 9 mm shoulder around the glass.
- Ornament follows the concept: triangle leaf panels above and below the band, rivet rims, petal base, leaf panels on the lid. The ornament is 1.2 mm of relief on top of the 2.4 mm wall and never cuts into it. Raised edges are stepped or chamfered so they print without supports.
- Letters are 8 mm (was 13), spaced out like the concept. The text repeats three times around, with a lit diamond between repeats.
- **USB-C** is now a USB-C-shaped slot at the bottom of the foot, sized so plug overmolds fit. The round 6069 sits behind a 1.6 mm skin, so only the slot shows. The ball has to lie on its side, or sit in a stand, while it charges.
- **Power switch** is inside, under the lid on the bezel top (front-right). The actuator sits 0.4 mm below the surface, so you flip it with a fingernail.
- **Lid switch** is hidden in the bezel. The lid rim presses a small plunger (2 mm), which presses the SS07 lever.
- **Hinge** is a slim knuckle barrel set into the back rim, 0.8 mm proud. There's no external block.
- **GPS** sits on a 55° bracket in the shoulder cavity, patch facing up and out, away from the display.
- **LED strip** sits on a ring built into the upper shell (it no longer rides on the chassis), 9 mm behind the letters.

## Check results
- All colour meshes are closed volumes.
- No collisions between any parts or components.
- Lid swing 0–110°: clear.
- Display + chassis module dropped in from the top: clear all the way.
- LED to letters: 9 mm. GPS to compass: 12 mm. Battery to compass: 14 mm.
- Overhangs: only short bridges (insert/USB pocket ceilings, the compass-tower roof, the hinge pin) and the 0.6 mm bevel steps on the ornament.
- Thin spots are all intended: retaining lips, ornament straps, spindle walls (1.2 mm), and the 1.6 mm USB skin.

## Still needed
- **3.4C board:** the back board and its 4 mounting holes are assumed to match the 4C (85 × 65 mm board, holes 75 × 73 mm apart). Please measure. The connector keep-out (5 mm round the board) clips the spigot at the corners, so check that the USB-C plugs clear when you test-fit.
- **Power switch:** height, and the slider's width, height and travel (body 9.11 × 3.58 mm measured).
- Standoff height/thread (placeholder 9 mm, M2.5).
- 6069 nose depth (to confirm the plug engages through the 1.6 mm skin).
- SS07 lever travel (plunger length). PN532, Core1262 and PowerBoost holes.
- LoRa antenna: suggest the shoulder cavity at ~160°, opposite the GPS.
- A charging stand, since the port is at the bottom.

## Participant medallion (`medallion.py`)
A one-piece Covenant Path badge with each participant's NFC tag sealed inside. Single colour, painted after printing.
- `python medallion.py` writes `out/medallion.stl` and `.step`, and prints the pause height. `python medallion_preview.py` draws the paint map and a side section.
- 48 × 65 × 4.8 mm, with a 5.2 mm lanyard hole. The front art is raised 0.8 mm: base-coat dark, then dry-brush the raised parts light. MAKE PROMISES / KEEP PROMISES is engraved on the back.
- **Tag slot:** 26.2 mm wide, from z 1.0 to 2.2 mm, for a 25.4 × 1.0 mm tag. Set `TAG_D` and `TAG_T` to your measured tag. The slot is rounded up to whole 0.2 mm layers.
- **Print:** face up, no supports, 0.2 mm layers. In Bambu Studio, add a pause on the first layer above the slot (it starts at 2.2 mm and shows 2.40 in the layer slider). At the pause, drop the tag in flat and resume.
- Use non-metallic paint over the tag area, because metal-flake paint can block NFC.
- Font: Oswald Bold (`fonts/`, SIL Open Font License).
