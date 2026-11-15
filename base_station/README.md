# Liahona base station — v0.1.0

Firmware for the **Seeed XIAO ESP32-S3 + Wio-SX1262** kit. It replaces Meshtastic. The base station listens for the balls and passes everything to the laptop over USB, and it sends leader messages and commands to the balls.

It uses the same radio driver and packet format as the ball (`../firmware/components/liahona_radio`), so the two always match.

## Build and flash (VS Code + ESP-IDF extension)
1. **Connect the 860–930 MHz antenna** to the Wio-SX1262 before powering it.
2. Open the `base_station` folder in VS Code.
3. Set the target to **esp32s3**.
4. Plug the XIAO in with USB-C and choose its port.
5. **Build, Flash and Monitor.** If flashing doesn't start, hold **B** (boot), tap **R** (reset), let go of B, and flash again.

This overwrites Meshtastic. To go back, use the Meshtastic web flasher.

## Talking to it
Open a serial monitor on the XIAO's port (any baud rate; it's native USB). It prints one JSON object per line:
```
{"ev":"hello","fw":"liahona-base 0.1.0","freq":915000000,"bw":500,"sf":10,"power":14,"tcxo":true}
{"ev":"status","ball":3,"seq":12,"fix":true,"lat":46.6612345,"lon":-121.0012345,"batt":3.92,"low_batt":false,"lid":"open","stage":0,"heading":123.4,"rssi":-80,"snr":9.5}
```
Type commands, one per line, as plain words or JSON:

| Words | JSON | What happens |
|---|---|---|
| `ping 3` / `ping all` | `{"cmd":"ping","to":3}` | Ball replies with its status straight away |
| `msg 3 Head to the river` | `{"cmd":"msg","to":3,"text":"..."}` | Text for the ball's screen (48 bytes max) |
| `unlock 3`, `skip 3`, `dim all`, `restore all` | `{"cmd":"unlock","to":3}` | Journey commands |
| `stage 3 4` | `{"cmd":"stage","to":3,"stage":4}` | Jump ball 3 to step 4 |
| `power 20` | `{"cmd":"power","dbm":20}` | Base transmit power, -9 to 22 dBm |
| `info` | `{"cmd":"info"}` | Prints the hello line again |

Messages and commands to one ball are resent up to 3 times until the ball ACKs (`{"ev":"ack",...}`); otherwise you get `{"ev":"timeout",...}`. Messages to `all` aren't acknowledged.

The Liahona Manager's field dashboard will use this same JSON over USB.

## Pins (Wio-SX1262 for XIAO)
SCK 7, MISO 8, MOSI 9, NSS 41, RESET 42, BUSY 40, DIO1 39, RXEN 38, DIO2 drives the antenna switch, TCXO 1.8 V. These match Meshtastic's `seeed_xiao_s3` variant.
