# Liahona Manager — Architecture (draft v0.1)

Status: planning. Nothing built yet. Written 2026-11-08 to capture the plan before the firmware work starts.

## Goal

A web-based **Liahona Manager** for preparing camp journeys (users, media, challenges), plus a **local offline version** for running journeys in the field without cell coverage.

Core rule: **the cloud is for preparation; the field runs fully offline.** Nothing a ball or a leader needs during a journey may depend on internet access.

---

## 1. System overview

```
           PREPARE (online)                    LOAD (base camp)            RUN (field, offline)
 ┌──────────────────────────────┐      ┌──────────────────────┐     ┌──────────────────────────────┐
 │ Cloud manager (web)          │      │ Ball <- Wi-Fi / OTA  │     │ Laptop manager (local app)   │
 │  - Users/roles               │ ───► │   or laptop push     │     │  - Journey control, map, log │
 │  - Media library (S3)        │      │ Ball <- package for  │     │  - Leaders' phones via       │
 │  - Converter (ffmpeg)        │      │   its group          │     │    laptop Wi-Fi hotspot      │
 │  - Journey editor            │      └──────────────────────┘     │  - XIAO base station (USB)   │
 │  - Versioned packages        │                                   │      = LoRa radio bridge     │
 └──────────────────────────────┘ ◄──── logs sync when online ───── └──────────────┬───────────────┘
                                                                                   │ LoRa
                                                                          12 balls (+1 spare)
```

---

## 2. Cloud manager

### Users and access
- **AWS Cognito** user pool (or the auth built into whatever hosting is chosen).
- Roles (draft):
  - **Admin**: users, balls, publishing, settings.
  - **Editor**: uploads media, writes stations and challenges, builds journeys.
  - **Viewer**: read-only. Could be used by leaders to preview.

### Media library
- Uploads go straight to **S3** using presigned URLs, so the browser uploads directly and large files never pass through the app server.
- The **original is always kept**. Ball-ready **renditions** are generated and stored alongside it.
- Metadata (title, tags, duration, size, rendition status) lives in the database.

### Converter
- **ffmpeg in a container**, triggered by an S3 upload event. It runs on **AWS Lambda** (container image) for short clips, or on **Fargate** for long ones.
- **Not AWS MediaConvert**: it cannot output MJPEG, which the ESP32-P4 needs for hardware-decoded video.
- Rendition presets, using the settings proven on the bench:

| Type | Output | Notes |
|---|---|---|
| Video | MP4: MJPEG video + AAC audio | **Exactly 720×720** (centre square crop). The panel is the Waveshare 4" 720×720, and the example player does not scale, so any other size tiles. 15 fps, `-q:v 9`, `yuvj420p`, AAC 44.1 kHz stereo 96k, `+faststart`. A length cap applies. If playback is choppy, use 12 fps or `-q:v 12`. |
| Audio | One format the firmware plays (e.g. MP3 or WAV, 44.1 kHz) | **Loudness-normalized** (ffmpeg `loudnorm`) so clips don't jump in volume. |
| Image | 720×720 JPEG | Baseline JPEG, which the chip decodes in hardware. |

Bench-proven video command (confirmed on the board 2026-11-08):
```
ffmpeg -y -i in.mp4 -vf "scale=720:720:force_original_aspect_ratio=increase:flags=lanczos,crop=720:720,setsar=1" \
  -r 15 -c:v mjpeg -q:v 9 -pix_fmt yuvj420p -c:a aac -ar 44100 -ac 2 -b:a 96k -movflags +faststart out.mp4
```
- Firmware build note: the LCD type in sdkconfig must be `CONFIG_BSP_LCD_TYPE_720_720_4_INCH=y`.
- After converting, the converter runs an **ffprobe check**: codec, resolution, size and duration must match the preset, or the rendition is marked failed.

### Journey editor
- **Stations**: name, GPS point and radius, optional NFC tag ID.
- **Challenges**: content (video, audio, image, text), plus an **unlock rule**:
  - NFC tag scanned
  - Entered a GPS area
  - Leader command over LoRa
  - Passcode on the touchscreen
  - Time window
- **Journey**: an ordered or branching set of stations, assigned per group or ball.
- **Compass behaviour** per step: point toward the next station, spin, hidden, and so on.

### Publishing: versioned packages
- **Publish** freezes a journey into an immutable **package** per group:
  - `manifest.json`: journey, steps, unlock rules, media list with SHA-256 hashes, package version, and minimum firmware version.
  - The converted media files.
- **Size check** against the ball's SD budget before publishing is allowed.
- Packages are never edited, only superseded. That way you always know exactly what was on each ball.

---

## 3. Loading the balls

- **Primary: Wi-Fi at base camp.** The ball checks in, sees its assigned package version, and downloads **only changed files** by comparing hashes. It verifies every hash, then switches to the new package atomically, so it keeps the old one until the new one is complete.
- **Fallback: laptop push** over the same protocol, using the laptop as the server. No internet needed.
- **Last resort:** copy to the SD card by hand. This means opening the ball, so avoid it.
- **Firmware OTA** uses the same channel, with dual app partitions and rollback.
- **Firmware requirement:** OTA for both content and firmware has to be built into the firmware **from the start**.

---

## 4. Local / field manager

- Runs on a **laptop**: the same app, packaged to run locally (e.g. Docker or a single executable), with a local database.
- **Before a trip:** "Download for offline". This pulls the published journeys, all packages, the ball roster and map tiles for the area.
- **In the field:**
  - The **XIAO + Wio-SX1262 base station** plugs into the laptop over USB and acts as a **LoRa radio bridge only**. It forwards messages and does no web hosting.
  - Leaders open the dashboard on their phones through the **laptop's Wi-Fi hotspot**.
  - Dashboard: ball positions on the offline map, battery, step and progress, check-ins, unlock or hint commands, messages to the balls, and an event log.
- **After the trip:** logs and check-ins sync to the cloud when it's next online, and appear on the journey history.

**Change to the build doc:** the base station is no longer a self-hosted dashboard. It becomes a USB LoRa bridge, and the laptop app hosts the dashboard.

### LoRa message budget (to design later)
LoRa is slow, around 1–5 kbps, so **media never goes over LoRa**. LoRa carries only small messages:
- Position and battery heartbeats
- Step progress and check-ins
- Leader commands: unlock, hint, recall, message text
- Acknowledgements

---

## 5. Hosting options (decision pending)

| Option | What | Pros | Cons |
|---|---|---|---|
| **A. AWS serverless** | Cognito, S3, API Gateway + Lambda, DynamoDB, ffmpeg Lambda/Fargate, CloudFront | Pay per use, near $0 when idle, scales | More moving parts; the local version is a separate build target |
| **B. One small server** | One VPS or Lightsail running the app + Postgres + an ffmpeg worker; media in S3 | Simplest; **the same app runs on the laptop** for offline | A few dollars a month; you look after one machine |

Leaning: **Option B**, because one codebase can run both in the cloud and on the laptop, which is the hardest requirement. S3 is still used for media either way.

---

## 6. Decisions still to make

- [ ] Who needs accounts, and which roles.
- [ ] Hosting: A (serverless) or B (one server).
- [ ] Ball loading: from the cloud directly, through the laptop, or both.
- [ ] Media budget per journey: minutes of video, number of clips, SD card size (16–32 GB planned).
- [ ] Audio format the firmware will play (follows the firmware audio work).
- [x] Video resolution: **720×720**, confirmed on the board 2026-11-08. Revisit only if a custom player adds scaling.
- [ ] Offline map tile source and area size.
- [ ] Ball identity: how each ball is registered (e.g. by its chip MAC or an NFC tag).
- [ ] Backups and retention for originals and logs.

## 7. Suggested build order (later)

1. **Firmware first**: package and manifest format, SD layout, OTA for content and firmware. This defines what the manager must produce.
2. Converter script, run locally (the same ffmpeg presets), to produce test packages by hand.
3. Local manager (single user): media library, journey editor, publish, push to a ball.
4. Field dashboard with the LoRa bridge.
5. Cloud deployment with users and roles, plus log sync.
