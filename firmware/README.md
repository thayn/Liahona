# Liahona ball firmware — v0.2.0 (milestone 1)

Foundation + bench self-test for the Waveshare **ESP32-P4-WIFI6-Touch-LCD-4C** (4", 720×720).
Built and checked with **ESP-IDF v6.1** (target `esp32p4`). It compiles cleanly, but it has not run on hardware yet.

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
| LoRa | SX1262 answers. It does **not** transmit yet. |
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
  - **Set `LED_COUNT` to your real number of LEDs.**
- `main/ui_compass.c`: compass screen. `main/ui.c`: self-test screen.
- `main/fonts/`: Cinzel and Inter, converted for LVGL (SIL Open Font License).
- `main/sensors.c`: I2C scan and compass.
- `main/gps.c`, `main/nfc.c`, `main/lora.c`, `main/leds.c`: one file per part.
- `main/inputs.c`: lid switch and battery.
- `main/storage.c`: SD card.
- `main/audio_test.c`: speaker.
- `partitions.csv`: two 8 MB app slots (for OTA) plus a 4 MB storage partition.

## Known limits (next milestones)
- **Compass:** uncalibrated and not tilt-compensated. Calibration is coming. Set `MAG_DECLINATION_DEG` in `board_pins.h` for your camp, so GPS bearings and the compass agree.
- **LoRa:** presence check only. Messaging comes with the base station firmware. Don't transmit without an antenna attached.
- **Not in this version yet:** video and audio playback from the SD card, the journey engine (manifest, unlock rules), Wi-Fi via the ESP32-C6, and OTA updates. The partitions are already set up for OTA.
