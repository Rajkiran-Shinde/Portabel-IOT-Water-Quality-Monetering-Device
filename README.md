<div align="center">

# 💧 Portable IoT Water Quality Monitoring Device

**A dual-core ESP32 firmware for real-time pH, TDS, hardness, salinity and temperature monitoring — with a local dashboard, cloud telemetry, and SMS-based alerting.**

![Platform](https://img.shields.io/badge/platform-ESP32-blue?logo=espressif)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D?logo=arduino)
![Build](https://img.shields.io/badge/build-PlatformIO-orange?logo=platformio)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20Dual--Core-brightgreen)
![Filesystem](https://img.shields.io/badge/filesystem-LittleFS-lightgrey)
![Cloud](https://img.shields.io/badge/cloud-Firebase%20RTDB-yellow?logo=firebase)
![License](https://img.shields.io/badge/license-MIT-informational)

</div>

---

## Executive Summary

Water quality testing in most small-scale, off-grid, or field contexts (rural wells, aquaculture tanks, small water-treatment setups) is still done with handheld meters that give a single instantaneous reading and no memory of what happened five minutes ago, let alone five days ago. This project builds a **self-contained, WiFi-connected water quality probe** around an ESP32 that continuously measures pH, Total Dissolved Solids (TDS), Electrical Conductivity (EC), salinity, hardness, and temperature, and turns that raw analog data into something actionable: a live local dashboard, a historical log in the cloud, and an SMS alert the moment the water crosses a safety threshold.

The engineering brief driving this firmware was simple to state and hard to satisfy on a single-core microcontroller: **the device has to serve a web dashboard to a phone browser, talk to Firebase over the internet, and sample noisy analog sensors fast enough to catch real excursions — all at the same time, without any one task stalling another.** A naive single-loop Arduino sketch would either drop web requests while blocking on a sensor read, or miss a fast conductivity spike while waiting on an HTTP response from Twilio. The solution implemented here is to stop treating the ESP32 as a single-threaded Arduino board and instead use its two physical Xtensa cores as two independent workers under FreeRTOS: one dedicated to networking, one dedicated to hard real-time sensing and safety logic.

On top of that architectural decision, the firmware leans heavily on **signal-processing techniques borrowed from lab instrumentation** rather than raw ADC reads: Olympic-scoring (trimmed-mean) filtering to reject splash and EMI noise, exponential moving averages to stop the display from jittering, and temperature-compensated calibration curves so that a probe calibrated at 31.3°C still reports correctly at 18°C. The result is intended to get lab-adjacent accuracy out of low-cost hobbyist probes (an analog pH board and a two-pin conductivity probe) rather than the ±1 pH-unit noise those sensors are notorious for when read naively.

Finally, the system was designed to be **field-configurable without a laptop.** Every value that operators are likely to want to change after deployment — the SMS destination number, the Twilio credentials, alert thresholds' notification cadence, how often the cloud gets updated — lives in ESP32 NVS flash (via `Preferences.h`) and can be rewritten from the web dashboard itself, over WiFi, with no re-flash required.

---

## Project Preview

| Local Dashboard | Live Sensor View | Trend Graphs |
|---|---|---|
| ![Homepage](Images/S1,%20Homepage.png) | ![Live Dashboard](Images/S2,%20LiveDashboard.png) | ![Graphs](Images/S3,%20Graphs.png) |

| AI Analysis View | Calibration Page |
|---|---|
| ![AI Analysis](Images/S3,%20Ai%20Analysis.png) | ![Calibration](Images/S4,%20Calibration%20Page.png) |

| Sensor Placement | Internal Build | Enclosure |
|---|---|---|
| ![Sensor Placement](Images/H1,%20Sensor%20Placement.jpg) | ![Internal](Images/H2,%20Internal%20Sneek%20Pic.jpg) | ![Outer Body](Images/H3,%20Outer%20Body.jpg) |

> **Note on the screenshots above:** the dashboard UI (`index.html` / CSS / JS) that these screenshots show is what the firmware's web server is built to serve from LittleFS, but the static frontend source isn't checked into this snapshot of the repository — only the firmware (`src/`, `include/`) and the raw images are present. See [Folder Structure](#folder-structure) for what's actually versioned here.

---

## Features

- **Dual-core task separation** — networking and sensing run as independent FreeRTOS tasks pinned to separate physical cores, so a slow HTTP client can never delay a safety-critical sensor read.
- **Local web dashboard over mDNS** — reachable at `http://wqi.local` with no need to look up the device's DHCP-assigned IP.
- **REST API for live telemetry** — `GET /api/data` returns the current pH, TDS, temperature, and hardness as JSON for the dashboard to poll.
- **Remote-configurable alerting** — `POST /api/save-alerts` lets the dashboard rewrite Twilio credentials, the alert message template, and notification intervals, persisted to flash immediately.
- **Trimmed-mean + EMA sensor filtering** — both the pH and TDS pipelines discard outlier samples before averaging, then smooth the result over time.
- **Three-point, temperature-compensated pH calibration** — separate acid-side and alkaline-side slopes, each corrected for the live water temperature using a Kelvin-scaled Nernst-style relationship.
- **Polynomial EC → TDS/Salinity/Hardness derivation** — a single conductivity measurement is turned into three separate water-quality figures via fixed conversion factors.
- **Non-blocking DS18B20 temperature reads** — the 750 ms conversion delay inherent to the sensor never stalls the loop.
- **Debounced, four-level alert state machine** (`SAFE → CAUTION → WARNING → DANGER`) with an 8-second stabilization window to reject momentary splash-induced false alarms.
- **Non-blocking buzzer feedback** — distinct beep cadences per alert level, driven entirely by `millis()` timers.
- **"Smart" SMS gatekeeper** — sends an SMS the instant severity escalates, then falls back to periodic reminders instead of texting on every loop iteration.
- **Direct Twilio REST integration** — no third-party gateway service; the ESP32 itself POSTs to the Twilio Messages API over TLS.
- **Dual Firebase Realtime Database modes** — continuous 24/7 streaming, or an idle/trigger mode for battery-conscious spot-checks initiated remotely from the app.
- **NVS-backed configuration persistence** — user-tunable settings survive power loss and reflashes, using wear-leveled flash storage instead of raw EEPROM.

---

## Problem Statement

Consumer and mid-range water testers report one number at one instant. They don't log history, they don't notify anyone when conditions change, and cross-referencing pH against TDS and temperature — which is often necessary to tell a real contamination event from sensor drift — is left entirely to the human holding the meter. For a small aquaculture operation, a rural household well, or a hostel water tank, that means problems are typically discovered only after someone notices a taste, smell, or health complaint.

A microcontroller-based alternative solves the *data* problem cheaply, but naively porting a single-threaded Arduino sketch onto that microcontroller reintroduces a different problem: **contention between network I/O and time-sensitive sensing.** Any sketch that blocks on `WiFiClient` calls, `delay()`-based sensor timing, or synchronous HTTP requests while also trying to serve a browser is going to either miss sensor events or become an unresponsive web server, sometimes both at once.

## Proposed Solution

This firmware treats the ESP32 as what it actually is — a dual-core SoC — rather than as a single-threaded Arduino board. `xTaskCreatePinnedToCore` splits the application into two independent, indefinitely-running FreeRTOS tasks:

- **Core 0** owns everything network-facing: WiFi, the asynchronous web server, mDNS, and serving the dashboard's static assets from LittleFS.
- **Core 1** owns everything time-sensitive: polling the ADS1115 ADC, walking the OneWire bus, running the filtering/calibration math, driving the buzzer state machine, and pushing to Firebase.

The two tasks communicate through a small set of shared global floats (`live_ph`, `live_tds`, `live_temp`, `live_hardness`) written only by Core 1 and read only by Core 0 — a deliberately simple single-writer/single-reader pattern that avoids the need for mutexes for this specific data shape, at the cost of Core 0 occasionally reading a value mid-update (acceptable here since a stale-by-milliseconds float has no practical impact on a dashboard refreshing every couple of seconds). `Arduino`'s own `loop()` is left empty and immediately calls `vTaskDelete(NULL)` on itself, handing total control to the two pinned tasks — a clear signal that this is not a conventional Arduino sketch dressed up, but a FreeRTOS application using the Arduino core as a HAL.

---

## System Architecture

```
                          ┌─────────────────────────────┐
                          │           ESP32              │
                          │                              │
   ┌───────────┐          │   ┌──────────┐  ┌─────────┐  │
   │  Browser  │◄────────►│   │  Core 0  │  │ Core 1  │  │
   │ (Phone/PC)│  HTTP    │   │ Web/WiFi │  │ Sensors │  │
   └───────────┘  mDNS    │   │  Task    │  │  Task   │  │
                          │   └────┬─────┘  └────┬────┘  │
                          │        │             │       │
                          │   LittleFS       ADS1115(I2C)│
                          │   (dashboard)    OneWire(GPIO4)│
                          │                  Buzzer(GPIO18)│
                          └────────┬─────────────┬────────┘
                                   │             │
                              WiFi │             │ HTTPS
                                   ▼             ▼
                          ┌──────────────┐ ┌─────────────┐
                          │  Firebase    │ │  Twilio SMS │
                          │  Realtime DB │ │    API      │
                          └──────────────┘ └─────────────┘
```

---

## Hardware Components

| Component | Purpose | Why It Was Chosen |
|---|---|---|
| **ESP32 Dev Module** | Main controller | Dual-core Xtensa LX6 with WiFi built in — a prerequisite for the Core 0/Core 1 split this firmware relies on; a single-core MCU (e.g. an 8266 or an AVR) couldn't implement this architecture at all. |
| **ADS1115 (Adafruit ADS1X15)** | 16-bit external I2C ADC | The ESP32's built-in ADC is non-linear and noisy near its rails when WiFi is active. Offloading both analog probes (pH and TDS) to a dedicated 16-bit ADC over I2C gives far more stable readings than the SoC's native ADC could. |
| **Analog pH probe + interface board** | pH sensing | Read on ADS1115 channel `A1`; converted to pH via a temperature-compensated, two-slope calibration (see [Working Principle](#working-principle)). |
| **Two-pin conductivity/TDS probe** | EC / TDS / salinity / hardness sensing | Read on ADS1115 channel `A0`; a single conductivity measurement is algebraically expanded into TDS, salinity, and hardness via fixed conversion factors, avoiding the cost of three separate sensors. |
| **DS18B20 (waterproof, OneWire)** | Water temperature | Needed both as a standalone reading and as an input to the pH/TDS temperature-compensation math; OneWire keeps the wiring to a single data pin (`GPIO4`). |
| **Piezo buzzer** | Local audible alert | Driven directly from `GPIO18` with a non-blocking state machine so it can beep at different cadences per alert level without stalling the sensor loop. |

---

## Software Architecture

**Toolchain:** PlatformIO targeting `board = esp32dev`, `framework = arduino`, with `board_build.filesystem = littlefs` and the `huge_app.csv` partition table. The partition choice matters: pulling in `ESPAsyncWebServer`, `ArduinoJson`, and the `Firebase-ESP-Client` library produces a binary large enough that the default ESP32 partition scheme (which reserves space for OTA and a second app slot) doesn't leave enough room for the application — `huge_app.csv` trades away OTA/dual-app support for a much bigger single app partition.

**RTOS layer:** Two tasks are created in `setup()` via `xTaskCreatePinnedToCore`, one pinned to core `0` (web/network) and one to core `1` (sensors/logic), each with a 10 KB stack and priority `1`. `loop()` is intentionally empty and self-deletes.

**Filesystem:** LittleFS is mounted read-only-from-the-app's-perspective and served directly by `ESPAsyncWebServer::serveStatic("/", LittleFS, "/")` with `index.html` as the default document — the standard PlatformIO pattern of flashing a separate filesystem image (`pio run --target uploadfs`) alongside the firmware image.

**REST API surface:**

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/data` | `GET` | Returns `{ ph, tds, temp, hardness }` as JSON, sourced from the Core 1 → Core 0 shared globals. |
| `/api/save-alerts` | `POST` (JSON body) | Accepts new Twilio credentials, alert message template, and notification intervals (via `AsyncCallbackJsonWebHandler`); writes them to both RAM and NVS flash. |

**Async networking:** `ESPAsyncWebServer` + `AsyncTCP` handle HTTP without blocking the Core 0 task loop, and `ESPmDNS` publishes the device as `wqi.local` so the dashboard doesn't depend on knowing a DHCP-assigned IP.

**Cloud sync:** `Firebase-ESP-Client` handles authentication (anonymous sign-up against the project's API key), NTP time sync (`pool.ntp.org`, GMT+5:30 offset baked into `config.h`) so historical pushes carry real UNIX timestamps, and both live-value and history-log writes to the Realtime Database.

**Serialization:** `ArduinoJson` v6 is used both to build the `/api/data` response and to parse the JSON body posted to `/api/save-alerts`.

**Persistence:** `Preferences.h` (ESP32 NVS) stores every runtime-configurable value under the `"wqi-app"` namespace — chosen deliberately over raw `EEPROM.h` emulation because NVS wear-levels writes across flash sectors, which matters here since these values can be rewritten from the dashboard at any time in the field.

---

## Frameworks & Libraries

| Library | Role |
|---|---|
| `Adafruit ADS1X15` | Driver for the ADS1115 16-bit ADC; both analog probes are read through it in single-ended mode with `GAIN_ONE` (±4.096 V range, 125 µV/count). |
| `OneWire` + `DallasTemperature` | Low-level 1-Wire bus handling and DS18B20-specific commands; `setWaitForConversion(false)` is used explicitly to avoid the sensor's 750 ms blocking conversion delay. |
| `ESPAsyncWebServer` (esphome fork) + `AsyncTCP` (esphome fork) | Non-blocking HTTP server — chosen over the synchronous `WebServer` class specifically so Core 0 can serve multiple simultaneous dashboard clients without stalling. |
| `ArduinoJson` (v6) | Building and parsing the JSON payloads exchanged with the dashboard. |
| `Firebase-ESP-Client` (mobizt) | Realtime Database read/write, NTP-backed timestamping, and token/auth lifecycle management for Firebase. |
| `Preferences.h` | ESP32-native NVS key/value storage for field-configurable settings. |
| `HTTPClient` + `WiFiClientSecure` | Used directly (not through a third-party SMS gateway) to POST to the Twilio REST API over TLS. |
| `ESPmDNS` | Publishes the `wqi.local` hostname on the local network. |

---

## Hardware Pinout

| Signal | ESP32 Pin | Defined As | Notes |
|---|---|---|---|
| I2C SDA | GPIO 21 (ESP32 default) | — | To ADS1115 `SDA` |
| I2C SCL | GPIO 22 (ESP32 default) | — | To ADS1115 `SCL` |
| OneWire data (DS18B20) | GPIO 4 | `ONE_WIRE_BUS` | Requires an external ~4.7 kΩ pull-up to 3.3 V |
| Buzzer | GPIO 18 | `BUZZER_PIN` | Driven `HIGH`/`LOW`, no PWM |
| TDS/EC probe | ADS1115 channel `A0` | `TDS_ADC_CHANNEL` | |
| pH probe | ADS1115 channel `A1` | `PH_ADC_CHANNEL` | |

*The I2C pins are the ESP32's hardware defaults — `config.h` doesn't override them, so the wiring above assumes `Wire.begin()` is called with no custom pin arguments.*

---

## Working Principle

**Sense** — Core 1 polls the ADS1115 for both analog probes and the DS18B20 for temperature, on independent timers (`ADC_INTERVAL = 10 ms` for TDS's circular buffer, a 20-sample burst per cycle for pH, `TEMP_INTERVAL = 750 ms` for temperature).

**Process** — Raw samples are filtered (bubble-sort + trimmed mean for both analog channels, EMA additionally for pH), converted from ADC counts to physical units, and temperature-compensated:
- pH: `voltage → 3-point split-slope calibration → Kelvin-scaled temperature correction → pH value`
- TDS/EC: `voltage → 2%/°C compensation → EC polynomial (73.787v² + 608.27v − 13.535) → TDS/Salinity/Hardness via fixed factors`

**Decision** — `AlertSystem::checkAlerts()` compares the latest pH, TDS, and hardness against three-tier thresholds (below) and runs the result through an 8-second debounce before it's allowed to change the committed alert level.

| Parameter | Caution | Warning | Danger |
|---|---|---|---|
| TDS (mg/L) | > 400 | > 500 | > 1600 |
| Hardness (mg/L) | > 150 | > 200 | > 600 |
| pH | < 6.8 or > 8.2 | < 6.5 or > 8.5 | < 6.0 or > 9.0 |

**Communication** — Firebase is polled every 3 s for a remote mode flag and pushed to on a schedule (2 s live values, `fb_update_interval` — default 5 min — for history), and Twilio is called directly whenever the SMS gatekeeper decides a notification is due.

**Actuation** — The buzzer state machine translates the current alert level into a beep pattern: silent (`SAFE`), 1 Hz toggle (`CAUTION`), 4 Hz toggle (`WARNING`), solid tone (`DANGER`).

**Feedback** — The dashboard polls `/api/data` for live values; Firebase's `sensor_history` node accumulates a timestamped log for trend graphs; Twilio delivers an SMS summary to the configured phone number.

---

## Engineering Highlights

- **FreeRTOS dual-core pinning over cooperative multitasking.** Rather than interleaving web and sensor work in one loop with careful `millis()` bookkeeping, the two concerns are given entirely separate execution contexts. This is the single architectural decision the rest of the firmware is built around.
- **Single-writer/single-reader shared state instead of mutexes.** The four `live_*` globals are written exclusively by Core 1 and read exclusively by Core 0. This sidesteps the complexity of FreeRTOS mutexes/semaphores for a data shape where a torn read has no meaningful consequence — a deliberate simplicity trade-off, not an oversight, though it's worth knowing this pattern only holds because there's exactly one writer.
- **Zero `delay()` calls outside of one-time hardware bring-up.** Every recurring behavior — ADC sampling, temperature polling, buzzer cadence, Firebase push intervals, SMS gatekeeping — is driven by `millis()`-based state machines, which is what makes the dual-core split actually pay off (a `delay()` anywhere in the sensor loop would still stall Core 1 internally).
- **Trimmed-mean ("Olympic scoring") filtering.** Both analog channels sort a burst of raw samples and discard the extremes before averaging — a technique borrowed from scoring judged competitions, applied here to reject transient EMI spikes and splash artifacts that a plain average would let through.
- **Debounced alert escalation.** A naive threshold check would let a single bumped probe or air bubble trigger a false `DANGER` alarm. The `_candidateLevel`/`SENSOR_STABILIZE_TIME` mechanism requires 8 continuous seconds at a new severity before it's trusted.
- **Escalate-immediately, remind-periodically SMS logic.** The alert system doesn't simply text on a timer (which would spam a sustained problem) or only on the rising edge (which would miss a slow degradation from `WARNING` to `DANGER` if the earlier alert already fired). It does both, with automatic reset of the "already notified" tracker on de-escalation.
- **NVS over raw EEPROM emulation.** `Preferences.h` was chosen specifically because it wear-levels writes across flash sectors, relevant here because Twilio settings and intervals are designed to be rewritten repeatedly from the dashboard over the device's lifetime.

---

## Communication Flow

```
Sensors (I2C / OneWire)
        │
        ▼
Core 1: filter → calibrate → compensate
        │
        ├──► live_ph / live_tds / live_temp / live_hardness (shared globals)
        │
        ├──► AlertSystem.checkAlerts() ──► buzzer state machine (GPIO 18)
        │                              └──► SMSManager.sendAlert() ──HTTPS──► Twilio API
        │
        └──► FirebaseManager.processAndPush() ──HTTPS──► Firebase RTDB
                                                             │
Core 0: AsyncWebServer                                       │
        │                                                    │
        ├──► GET /api/data ◄── reads live_* globals          │
        ├──► POST /api/save-alerts ──► Preferences (NVS)     │
        └──► serveStatic("/") ◄── LittleFS                   │
                                                              ▼
                                                   Dashboard (browser) ◄── polls RTDB / /api/data
```

---

## Folder Structure

```
Portabel-IOT-Water-Quality-Monetering-Device/
├── include/                 # Public headers — one per subsystem
│   ├── config.h              # Pins, credentials, calibration constants, timing intervals
│   ├── alerts.h               # AlertSystem: threshold logic + buzzer state machine
│   ├── firebase.h              # FirebaseManager: RTDB sync, mode polling
│   ├── ph.h                     # PHSensor: filtered, calibrated pH
│   ├── tds.h                     # TDSSensor: EC/TDS/salinity/hardness
│   ├── temp.h                     # TempSensor: non-blocking DS18B20 driver
│   ├── sms.h                       # SMSManager: direct Twilio REST calls
│   └── turbidity.h                  # Scaffolded, currently empty (see Future Improvements)
├── src/
│   ├── main.cpp               # FreeRTOS task setup, REST endpoints, orchestration
│   ├── ph.cpp / tds.cpp / temp.cpp / alerts.cpp / firebase.cpp / sms.cpp
│   └── turbidity.cpp           # Scaffolded, currently empty
├── lib/                      # PlatformIO private-library slot (unused / default README)
├── test/                     # PlatformIO test slot (unused / default README)
├── Images/                   # Hardware and dashboard screenshots
├── platformio.ini             # Board, framework, filesystem, and dependency manifest
└── .gitignore
```

> The `data/` directory that would normally hold the LittleFS-served dashboard (`index.html`, CSS, JS shown in the screenshots above) is not present in this snapshot — only the firmware and raw images are versioned here.

---

## Installation

1. **Install PlatformIO** (VS Code extension, or PlatformIO Core CLI).
2. **Clone the repository** and open it as a PlatformIO project.
3. **Wire the hardware** per the [Hardware Pinout](#hardware-pinout) table above.
4. **Set your own credentials in `include/config.h`** before building — see the [Security Notice](#-security-notice-read-before-you-push-this-repo-anywhere-public) below; do not reuse the placeholder values checked into this repo.
5. **Build and upload the firmware:**
   ```bash
   pio run --target upload
   ```
6. **Provide the dashboard files** (`index.html`, CSS, JS) in a `data/` directory at the project root, then upload the filesystem image:
   ```bash
   pio run --target uploadfs
   ```
7. **Open the Serial Monitor** at `115200` baud to confirm WiFi connection, LittleFS mount, and Firebase auth.

## Usage

1. Power the device; it connects to the WiFi network configured in `config.h` and prints its status to Serial.
2. Visit `http://wqi.local` (or the device's IP shown in the Serial log) from any device on the same network.
3. Open the dashboard's alert settings panel to enter Twilio credentials and a destination phone number — this is written straight to NVS flash, no reflash required.
4. Choose a monitoring mode in Firebase (`constant` for continuous logging, or leave it as anything else to use trigger mode) and, if using trigger mode, flip `settings/monitoring_control/trigger_reading` to request a one-off stabilized reading.
5. Watch the buzzer and SMS alerts respond automatically as pH, TDS, and hardness are evaluated against the built-in thresholds.

---

## ⚠️ Security Notice (read before you push this repo anywhere public)

`include/config.h` in this codebase currently hardcodes the **WiFi SSID/password** and the **Firebase Web API key + Database URL** in plaintext (the Twilio SID/Auth Token block is present but commented out). That's a reasonable shortcut for a single offline prototype sitting on a bench, but it becomes a real exposure the moment this repository is pushed to a public GitHub remote — anyone can read those values out of the commit history even after they're later removed.

Before publishing this repo:
- Move all credentials out of `config.h` and into a file that's listed in `.gitignore` (a `secrets.h` that `config.h` includes, for example), or supply them as PlatformIO build flags.
- If this repository (or an earlier version of it) has already been pushed publicly with real keys in place, treat the Firebase API key as compromised: rotate it and review your Realtime Database security rules to confirm they don't allow unauthenticated public read/write.
- Keep the Twilio block commented out (as it already is) or move it to the same gitignored secrets file — don't uncomment it in a commit.

---

## Future Improvements

- **Wire up the turbidity sensor.** `turbidity.h`/`turbidity.cpp` exist as empty scaffolding, and the Firebase payload currently pushes a hardcoded placeholder (`0.5`) in the `turbidity` field — this is the most immediate gap between what's modeled and what's implemented.
- **Mutex-protect the shared sensor globals**, or migrate to a FreeRTOS queue, if additional consumers of `live_ph`/`live_tds`/etc. are ever added on Core 0 — the current single-writer/single-reader assumption is fine today but is easy to silently break later.
- **Move all secrets out of `config.h`** per the notice above, ideally with a documented `config.h.example` template for new contributors.
- **Add OTA updates** — currently traded away by the `huge_app` partition scheme; revisiting the partition table (or moving some libraries to reduce binary size) would be needed to bring this back.
- **Version the dashboard frontend** (`data/index.html`, CSS, JS) in this repository so a fresh clone is buildable end-to-end without sourcing the LittleFS assets separately.
- **Composite Water Quality Index (WQI) score.** `AlertSystem::checkAlerts()` already accepts a `wqi` parameter that's currently unused — a natural next step is computing a single composite index from pH/TDS/hardness/turbidity rather than alerting on each parameter independently.

---

## Author

Maintained as part of an embedded systems / IoT product engineering portfolio, combining FreeRTOS-based firmware architecture, analog sensor signal processing, and cloud/SMS integration on the ESP32 platform.

## License
Copyright © 2026 Rajkiran Shinde. All Rights Reserved. See the [LICENSE](LICENSE) file for more information.
