# Liahona ball firmware — v0.4.0 (milestone 3: journey engine)

Foundation + bench self-test for the Waveshare **ESP32-P4-WIFI6-Touch-LCD-4C** (4", 720×720).
Built and checked with **ESP-IDF v6.1** (target `esp32p4`). The journey screens (v0.4) are new and have not run on hardware yet.

## What it does
- **Compass screen**, styled like the concept renderings:
  - A fixed dial with **N at the top**, as in the concept. Only the spindles turn: the thin cream spindle points north.
  - The **gold spindle** points to the station. With no station set, it rests near north and drifts gently.
  - With a station, the distance shows in the middle ("340 m"); without one, the heading.
  - **Press and hold anywhere** to open the self-test.
- **Bench demo:** on the self-test page, once the GPS has a fix, tap **Mark**. That spot becomes the station. Walk away, and the gold spindle points back to it with the distance.
- **Self-test screen.** Tap **Self-test**. It has one row per part:
  - **Green**: PASS.
  - **Red**: FAIL.
  - **Grey**: still waiting, or needs you to do something.

| Row | How it passes |
|---|---|
| Touch | Tap the screen anywhere. |
| I2C bus | Lists every address found. Expect `10 18 1C 24 40 5D`. |
| Compass | LIS3MDL found. The numbers change as you turn it. |
| GPS | PA1010D is sending data. It shows a position once it has a fix; try outside or by a window. |
| NFC | PN532 found. Hold a tag near it and its ID appears. |
| LoRa | SX1262 answers, then shows `#<ball> listening, TX off`. Once packets arrive: counts and the last signal strength. A leader message or command shows here for 10 s. **Tap the row** to send a status report (needs TX enabled). |
| Lid switch | Press and release the switch once. |
| LEDs | Colours cycle red, green, blue, white, then a chase. **Check by eye.** The LEDs button switches between test, off and gold glow. |
| Battery | Shows the voltage if the 100k/100k divider is wired, otherwise "not wired". |
| SD card | A FAT32 card is mounted. Free space is shown, and whether `/liahona/manifest.json` exists. |
| Speaker | Press **Beep** and you should hear a two-note chime. |
| Firmware | Version and which app slot it's running from (`ota_0`). |

A part that isn't connected shows FAIL. Nothing else is affected, so you can test with only some parts wired.

## Build and flash (VS Code + ESP-IDF extension)
1. Open the `firmware` folder of the cloned repo in VS Code.
2. Set the target to **esp32p4**.
3. **The first time only:** run **Erase Flash**. This firmware uses a new partition layout with two app slots, so firmware can update over the air later.
4. **Build, Flash and Monitor.** The first build downloads the board package, LVGL and the LED driver automatically, so it takes longer.

The 4" 720×720 screen is already selected in `sdkconfig.defaults`. If you ever delete `sdkconfig`, it's rebuilt from that file.

## Where things live
- `main/board_pins.h` holds **all pins and hardware settings**. Change wiring here only.
  - **Set `LED_COUNT` to your real number of LEDs** (78 for now; trim after the final fit).
- `main/ui_compass.c`: compass screen. `main/ui.c`: self-test screen.
- `main/fonts/`: Cinzel and Inter, converted for LVGL (SIL Open Font License).
- `main/sensors.c`: I2C scan and compass.
- `main/gps.c`, `main/nfc.c`, `main/lora.c`, `main/leds.c`: one file per part.
- `components/liahona_radio/`: SX1262 driver and packet format, shared with the base station.
- `main/inputs.c`: lid switch and battery.
- `main/storage.c`: SD card.
- `main/audio_test.c`: speaker.
- `partitions.csv`: two 8 MB app slots (for OTA) plus a 4 MB storage partition.

## LoRa link (v0.3)
The ball talks to the base station (`../base_station`) on 915 MHz (settings in `components/liahona_radio/radio_config.h`, the same file the base station uses).

**Wiring added for v0.3** (Core1262 module):
- A short wire on the module itself: **DIO2 → TXEN**. The radio then switches its own antenna to transmit.
- **RXEN → header pin 13** (GPIO20). Check the board's silkscreen says 20 next to that pin.

**Turning transmit on:** `LORA_TX_ENABLED` in `board_pins.h` is **0**. Set it to 1 only when the **Core1262-HF** is fitted **and** its 915 MHz antenna is plugged in. Never with the LF (410–510 MHz) module.

**Ball number:** put a file `liahona/ball_id.txt` on the SD card containing just the number (1–254). Without it the ball is #1.

What the ball does with TX on:
- Sends a status report (position, battery, lid, step, heading) every 30 s, and straight away when pinged or when you tap the LoRa row.
- ACKs leader messages and commands (unlock, skip, dim, restore, stage), so the base station knows they arrived.
- `lora_send_checkin()` is ready for NFC station check-ins (the base ACKs those).

Host tests for the radio code (no hardware): `make -C components/liahona_radio/test_host`.

## Journey (v0.4)

The ball now plays a journey from the SD card: stage briefs, questions, check-ins, roll call by medallion, darkening and hints, plus audio clues.

### SD card layout (FAT32)
```
liahona/
  journey.json    Manager → Journeys → download the Ball file, rename it to journey.json
  roster.json     Manager → Settings → Ball roster (teams, people, medallion UIDs)
  ball_id.txt     this ball's number, e.g. 1 (matches the team's ball in the roster)
  media/          audio named in the journey: .mp3 or .wav (16-bit PCM)
```
With no `journey.json`, the ball stays on the compass. With a bad file, the journey screen shows the error.

### How a team plays
- **Gather:** every team member taps their medallion on the ball. A leader can tap twice to start without everyone.
- **Brief**, then the stage's tasks in order. Each tap on the ball gives a short tick.
- **Supported tasks:** intro, question, true/false, check-in (tap the station's place tag), gather fragments (decoys darken the ball), testimony, leader approve, rest, scripted darken, audio clue and name that hymn.
- **Other task types** show their text; a leader taps their medallion to move on.
- **After a wrong answer**, the ball goes dark and then gives the same question again. A leader can skip it by tapping their medallion, or with **Skip** on the Field dashboard. For 2 s after the light comes back, medallion taps are ignored, so the last restoring tap can't skip by accident.
- **Darkening:** wrong answers or a missed deadline darken the ball for the times in `penalties.darken_s`. With unity restore, all members tap to bring the light back; a visiting leader can also restore it.
- **Hints** appear after `hint_after` wrong tries. Audio tasks have **Play again**.
- **Compass button** appears when the current place has coordinates. Tap the compass to come back.
- **Long-press** anywhere for the self-test page; its Back button returns to the journey.

### Leader commands (from the Field dashboard)
Unlock and Skip move the current task on. Jump to stage *n*, or 0 to restart. Dim and Restore turn the screen and LEDs down or up.

### Resume
Progress (stage, task, wrong count) is kept in NVS. After a power cycle the ball picks up where it left off. Replacing `journey.json` with a different journey starts it fresh.

Host test for the engine (no hardware): `make -C components/journey/test_host && components/journey/test_host/test_journey`.

## Known limits (next milestones)
- **Compass:** uncalibrated and not tilt-compensated. Calibration is coming. Set `MAG_DECLINATION_DEG` in `board_pins.h` for your camp, so GPS bearings and the compass agree.
- **LoRa:** tested both ways to about 300 m with the stock antennas. Leader text messages are stored but not shown on the journey screen yet.
- **Not in this version yet:** video playback, the remaining task types, Wi-Fi via the ESP32-C6, and OTA updates. The partitions are already set up for OTA.
