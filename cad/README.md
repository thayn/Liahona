# Liahona CAD, v3: 160 mm ball for the 3.4" screen

`python3 liahona.py` rebuilds every part into `out/`. `python3 check.py` re-runs the checks. All dimensions are in the `S = dict(...)` settings block in `liahona.py`. Coordinates: ball centre at the origin, +Z up (display faces up), hinge at −Y (back).

## v4 (175 mm): lid catch, medallion seal, engraved option
Reprint these three for the 175 mm ball:
| Part | What's new |
|---|---|
| `out/175mm/lid_ring.3mf` | A narrow **catch** at the front (a 10 mm tongue with a hook) and **two tapered pins** underneath. A **fingertip notch** sits above the catch |
| `out/175mm/bezel_ring.3mf` | The slot and ledge for the catch, and two sockets for the pins |
| `out/175mm/lower_shell.3mf` | A **medallion seal** on the lower front (under the catch), with the NFC reader mounted right behind it |

Nothing else changes. The base cap no longer carries the reader.

- **Closing the lid:** press the front down until it clicks. The pins line the lid up, so it can't sit off to one side even with a loose hinge. To open it, put a fingertip in the notch at the front and lift firmly. If the catch is too stiff or too loose, change `CATCH_HOOK` (0.5 mm) or `CATCH_PRELOAD` (0.1 mm) and reprint the lid ring.
- **Medallion seal:** a round well about 35 mm across, with a raised ring, eight-point star and 1.6 mm floor. The ornament is cleared around it, so it reads as a seal. Campers hold their medallion over it. The screen now says "tap their medallion on the seal".
- **Fitting the reader:** before the base cap goes on, reach in through the bottom opening. Put the PN532 board **antenna side out** (the side with the coil, no parts) onto the flat face behind the seal, between the four corner guides. Hold it with 0.8 mm foam tape or a few dots of hot glue. Route its cable up to the header as before. It reads through about 2.5 mm of plastic, much less than the old base cap.
- **Print:** lower shell upright as before. The well walls are 60°, and the star and ring print without supports. Lid ring upside down: the tongue and pins point up, so no supports are needed for them.
- **160 mm ball:** it has no room for the reader at the front (the seam wedge and base-cap tabs are in the way), so it keeps the reader on the base cap. Its lid catch and pins are in `out/`.

### Optional: "curious workmanship" engraved version
`out/175mm/engraved/` has a full alternative outer set: `upper_shell`, `lower_shell`, `bezel_ring`, `lid_ring` and `base_cap`. The lid cap keeps its leaves, and everything inside is the same as the regular parts. It includes:
- **Continuous engraved bands** above and below the tan band, with no tiles. Each has a double border line and a beaded edge, and rows of characters packed edge to edge all the way round.
- **Stacked plate edges:** fine horizontal lines on the bezel, lid ring and base cap, so the ball reads as a stack of thin plates.
- **Three binding rings** raised on the back, over the hinge side.

The characters are invented, in a flowing style. To use characters traced from the "Caractors" document, put a `caractors.json` next to `liahona.py`. Its format is a list of `{"w": width, "strokes": [[[x, y], ...], ...]}` in a 1-unit-tall box. Then rebuild with `LIAHONA_SIZE=175 LIAHONA_ORNAMENT=engraved python3 liahona.py upper_shell lower_shell bezel_ring lid_ring base_cap`, which takes about 12 minutes. Print with a 0.4 mm nozzle at 0.12–0.16 mm layers. The engraving is 0.65 mm lines, 0.55 mm deep.

## Sizes
| Size | Screen | Glass | Build |
|---|---|---|---|
| **160 mm** (default) | ESP32-P4-WIFI6-Touch-LCD-**3.4C**, 800 × 800 | 115 mm, 6 mm thick | `python3 liahona.py` → `out/` |
| 175 mm | ESP32-P4-WIFI6-Touch-LCD-4C, 720 × 720 | 126 mm | `LIAHONA_SIZE=175 python3 liahona.py` → `out/175mm/` |

The ball's shape (radius, split heights, ornament, band, letters, insert circles, spindle) scales with the ball. Bought parts keep their real sizes: switches, battery, boards, magnets, the 2 mm hinge rod and the 3 mm spindle rod.

### 175 mm ball already printed (v2)? Reprint these
The v3 fixes are in the 175 mm files too. Only four parts differ from the v2 print:
| Part | Why |
|---|---|
| `out/175mm/bezel_ring.3mf` | Hinge pin entry; drop-in lid-switch pocket; looser power-switch pocket |
| `out/175mm/chassis.3mf` | Posts at 75 × 75 mm with pegs for the back-plate holes |
| `out/175mm/base_cap.3mf` | Optional: the cable channel no longer swallows one of the 3 inserts. The old one works with 2 screws |
| `out/175mm/lid_ring.3mf` | Optional: the entry-side knuckle is a free fit. The old one works if you press the rod through it, or drill it out to 2.2 mm |

Everything else (upper and lower shell, lid cap, battery tray, spindle) is unchanged.

## What changed in v3
- **160 mm ball** around the 115 mm glass. There's still a 9 mm shoulder round the glass.
- **Hinge pin can go in now.** A 2.2 mm channel runs from one side knuckle out through the bezel wall: on the right when you stand behind the ball, hinge toward you. Close the lid, push a **32 mm piece of 2 mm brass rod** in there until it stops (it ends about 1 mm inside), then dab the hole with paint. The entry-side lid knuckle is a free fit; the far one grips the rod (2.05 mm), so the rod can't walk out.
- **Chassis boards** (PowerBoost, LoRa, compass) moved in, so the module still drops in through the smaller display spigot. The spigot has four shallow grooves where the chassis posts pass.
- **GPS** sits steeper (65°) and a little lower to fit the smaller shoulder.
- **Rivet strip** under the bezel kept at full height.
- **Base cap:** the USB cable channel now runs at 60°, between two inserts. Before, it swallowed the insert at 0°.
- **Lid switch, drop-in:** the 12.8 × 5.8 mm switch drops into a pocket from the bezel top, lever up, and rests on a ledge. Its leads go out through a smaller hole underneath.
  - The pocket is 1 mm longer than the body, for the lever, with 0.25 mm clearance each side.
  - With the lid shut, the lid itself pushes the lever 0.5 mm past the click (2.04 mm). With the lid open, the lever stands about 2.3 mm proud.
  - No plunger part.
- **Power switch from measurements:** body 9.11 × 3.58 × 3.5 mm, slider 1.44 mm wide, 1.78 mm tall, 3.71 mm travel. 0.45 mm clearance each side for the body and the slot.
  - The slider top sits 0.3 mm below the bezel top, in a slot long enough for its full travel.
  - The board itself is the measured 84.56 × 65.41 mm.
- **Mounting holes:** measured 75 × 75 mm apart, 4 mm across. The 4C uses the same spacing. There are no screws; the chassis is fixed with pegs and glue.
  - Each chassis post (6.6 mm across) runs up to the back plate.
  - Its top 9 mm, beside the board, is slimmer (5.5 mm) to clear the board's corner notches.
  - It ends in a 3.2 × 3 mm peg that drops into the plate hole. Glue it with a dab of hot glue or epoxy.
  - The spigot grooves leave a 1.2 mm wall.

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
The `.3mf` files are exported **already in print orientation**: bezel_ring, lid_ring and base_cap come out flipped, so you can import them and slice without rotating. The STL and STEP files stay in assembly orientation.

Use tree (auto) supports for lid_ring. The others print without supports.

| Part | Orientation | Notes |
|---|---|---|
| upper_shell (gold/tan/clear) | seam (z −24) down | Triangle row, rivet strip, band with 3 text repeats and lit diamonds. Inside: seam collar, LED ring 9 mm behind the letters, display spigot, shoulder web, tilted GPS bracket |
| bezel_ring | flipped | Riveted rim, display pocket + lip, twist-lock, hinge centre knuckle. A drop-in pocket for the lid switch (open at the top) and a hidden pocket for the power slide switch |
| lid_ring | upside down | Riveted rim, two lid knuckles (0.8 mm proud, no outside block) |
| lid_cap | flat face down | Eight raised leaf panels with veins. Solid: use ~10 % infill |
| spindle | upright, 0.2 mm nozzle | Turned base + finial; the 3 mm brass rod is the stem. Scaled to 76 mm for the bigger ball |
| lower_shell (gold/tan) | bottom down | Seam strip with rivets, lower triangle row, speaker grille holes inside one panel at −X, bayonet spigot + O-ring groove |
| base_cap | flipped (flat top on the bed) | Twelve raised petals, flat foot, USB-C slot at the very bottom centre, 6069 bore + nut pocket, cable channel |
| chassis, battery_tray | as before | Any PETG |

## What changed from v1 (v2, 175 mm)
- Ball is 175 mm. The lid seam moved up to z 52, giving a 9 mm shoulder around the glass.
- Ornament follows the concept: triangle leaf panels above and below the band, rivet rims, petal base, leaf panels on the lid. The ornament is 1.2 mm of relief on top of the 2.4 mm wall and never cuts into it. Raised edges are stepped or chamfered so they print without supports.
- Letters are 8 mm (was 13), spaced out like the concept. The text repeats three times around, with a lit diamond between repeats.
- **USB-C** is now a USB-C-shaped slot at the bottom of the foot, sized so plug overmolds fit. The round 6069 sits behind a 1.6 mm skin, so only the slot shows. The ball has to lie on its side, or sit in a stand, while it charges.
- **Power switch** is inside, under the lid on the bezel top (front-right). The actuator sits 0.4 mm below the surface, so you flip it with a fingernail.
- **Lid switch** sits in the bezel top, lever up. The lid rim presses the lever directly.
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
- Standoff height/thread (placeholder 9 mm, M2.5).
- 6069 nose depth (to confirm the plug engages through the 1.6 mm skin).
- PN532, Core1262 and PowerBoost holes.
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
