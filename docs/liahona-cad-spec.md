# Liahona CAD spec (handoff for the modeling chat)

Build doc (parts, wiring, cables): https://claude.ai/artifact/32GDe3uSEHMJTknNvQRSWi
Concept renderings: https://claude.ai/artifact/7NPjnPJMJrXtZBQgZBM36C

## Tooling
- Model in build123d (Python), every dimension in one settings block at the top.
- Export STEP (for edits in Fusion/Onshape) and STL for every part; multi-colour parts also as 3MF with one body per filament.
- Before sharing: render each part, section cuts, and check watertight mesh, min wall, overhangs, collisions and clearances.

## Decisions (current, v2)
- Ball: **175 mm** diameter (changed from 150 to match the concept renderings).
- Filament: printer handles up to 5 colours. Gold = shells, panels, frames, rivets, lid, spindle; darker tan = equator band + straps between triangle panels; clear = letters printed through the band wall (no separate letter ring).
- Ornament per concept: triangle leaf panels above and below the band, rivet rims, petal base, eight raised leaf panels on the lid. 1.2 mm relief on top of the 2.4 mm wall.
- Hebrew band: נֵר לְרַגְלִי דְבָרֶךָ (Psalm 119:105), small (8 mm) spaced letters, 3 repeats around with lit diamonds between, in a 16 mm tan band; LED strip on a ring inside the upper shell, 9 mm behind the letters. Text shaped with HarfBuzz and checked visually.
- Screen: Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (measured, below). Lid seam at z 52; glass recessed 2.9 mm (2.4 lip + 0.5 gasket).
- Hinge: slim brass-pin knuckle barrel set into the back rim (~0.8 mm proud), no external block.
- Lid switch (E-Switch SS0750301F040P1A) hidden in the bezel, pressed through a 2 mm plunger by the lid rim.
- Power slide switch (C&K JS202011CQN) inside, under the lid on the bezel top (front-right).
- USB-C (Adafruit 6069) at the very bottom centre behind a USB-C-shaped slot. Ball charges lying down or in a stand.
- GPS on a 55° bracket in the shoulder cavity at 20°, patch up and out. Compass on a raised tower at +X; speaker grille at −X in the lower shell.
- Spindle 76 mm (scaled with the ball): printed base + finial, 3 mm brass rod as the stem.
- No magnets anywhere (compass). Brass hardware near the compass.

## Measured screen (Thayn's calipers)
- Round display + metal back plate: 126 mm diameter, 6 mm thick.
- Board stack on the back: 85 × 65 mm, total depth including plate 15 mm (board adds ~9 mm). Small connector overhang past the board edges.
- Four mounting standoffs on the back plate, outside the board corners: 75 × 73 mm center to center. Height and thread (M2.5 or M3): STILL NEEDED.
- Do not use the five screws holding the board to the plate.
- Connectors all face outward along the board edges, parallel to the plate:
  - Left: USB-A (unused), PH2.0 speaker.
  - Top: two USB-C, small SH1.0, wide camera ribbon connector (unused). Power plug must be a USB-C angled up/down to turn the cable away from the display.
  - Right: microSD slot between two buttons (BOOT/RESET). Leave room to push/eject the card.
  - Bottom: two SH1.0 (I2C, UART); display ribbon folds out of this edge into a slot in the back plate — keep-out, never press on it. MIC1/MIC2 at the corners.
- 40-pin header is a female socket (use male jumper ends).

## Print rules (from earlier failures: thin walls, bad overhangs, electronics not fitting)
- Min wall 2.4 mm (multiples of 0.4 mm line width); ornament sits on top of the min wall, never cuts into it.
- Overhangs ≤ ~45–50° in print orientation; angled undersides on raised detail; flat foot on the base.
- Spherical caps can't print support-free as hollow shells: split so each hollow part stays ≤45–50°, and make the near-pole regions (lid top, base) solid parts printed flat-side down.
- Model every component at real size, check collisions and ≥0.4 mm clearance, reserve cable/plug space, and check assembly order.
- Test-print interfaces first: bezel/screen seat, seam lock, hinge, letter band section; print the chassis alone and load real parts before the shell.

## CAD v2 status (2026-11-03) — liahona_cad_v2.zip
Split planes (z from ball centre): lid cap 62.7–87.5 | lid ring 52–62.7 | bezel 38–52 | upper shell −24–38 | lower shell −66.5–−24 | base cap −85.5 (foot) to −66.5.
Checks: all colour meshes closed; no collisions; lid swing 0–110° clear; display + chassis module drops in from the top clear.
Joints: bezel and seam are 20° twist-locks (3 lugs); lid/base caps use 3 brass M3 screws into heat-set inserts.
Thayn's v1 feedback that v2 addresses: round USB hole on the side, external hinge block, external flaws, letters too big / not in a band, wrong colours, no ornament.

Still needed: standoff height/thread; 6069 nose depth (plug through a 1.6 mm skin); SS07 lever travel (plunger length); PN532, Core1262-HF, PowerBoost holes; LoRa antenna spot (suggest shoulder cavity ~160°); charging stand; lid and bayonet detents.
