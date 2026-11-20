# Wi-Fi chip firmware (ESP32-C6)

The board's ESP32-C6 ships with very old ESP-Hosted firmware (the boot log shows `coprocessor=0.0.0`). The ball uses ESP-Hosted 3.1.0, and with that mix Wi-Fi joins but downloads stall. This project builds matching 3.1.0 firmware for the C6. It's based on Espressif's `examples/ota/coprocessor_ota/cp`.

## Build (once)
1. In VS Code, open the `firmware_c6` folder (File → Open Folder). Open it on its own, not the ball's `firmware` folder.
2. Set the target to **esp32c6** (the chip name in the bottom bar, or `idf.py set-target esp32c6`).
3. Build. Don't flash: the C6 isn't on the USB port.
4. The result is `firmware_c6/build/liahona_c6.bin`.

## Install it on the ball (no extra cable)
1. Copy `liahona_c6.bin` to the SD card as `liahona/c6_wifi.bin`.
2. Put the card in the ball and turn it on. After about 6 seconds the Firmware row on the self-test shows "Wi-Fi chip: 5% … 100%". Then the ball restarts.
3. In the boot log, the line `esp-hosted fw versions: host=3.1.0 coprocessor=3.1.0` means it worked. The file is renamed to `c6_wifi.bin.done`.

The C6 keeps its old firmware until the new copy is complete, so if it fails part-way, just restart the ball to try again. If it says "update refused", the factory firmware is too old to update this way. Then the C6 has to be flashed through its own serial port with a USB-to-serial adapter.
