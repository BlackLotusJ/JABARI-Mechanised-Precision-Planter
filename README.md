# 🌱 JABARI-Mechanised-Precision-Planter

**ESP32-S3 based IR break-beam seed counting system with a real-time web dashboard for precision agriculture.**

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino-teal)
![License](https://img.shields.io/badge/License-MIT-green)
![Event](https://img.shields.io/badge/IEEE-YESIST12-orange)

---

## 📖 Overview

The **JABARI Precision Seeder Monitor** is an embedded system designed to evaluate the performance of a precision planter in real time. Using dual IR break-beam sensors, it detects individual seeds as they fall through the seed tube, classifies each event (single, double, skip, or noise), and computes key agricultural performance metrics.

All metrics are served through a lightweight web dashboard hosted directly on the ESP32-S3 in **Access Point (AP) mode** — no router, no internet, and no external server required.

This project was developed for the **IEEE YESIST12** innovation challenge under the Maker Fair category.

---

## ✨ Features

- **Dual IR break-beam seed detection** using ADC-based analog sensing
- **Real-time seed classification** — Single, Double, Skip, and Noise events
- **Automatic metric computation:**
  - Total seed count
  - Singulation percentage
  - Skip count
  - Double count
  - Seed rate (seeds/meter)
  - Seeds per minute
- **Built-in WiFi Access Point** (fixed IP `192.168.4.1`)
- **Responsive web dashboard** — optimized for desktop (no-scroll, single-page) and mobile
- **Live Serial Monitor reporting** — event logs + periodic metric summary tables
- **Adjustable planter speed** via dashboard (0–20 km/h)
- **One-click counter reset**
- **Status LED indicator** — fast blink when counting, slow blink when idle

---

## 🛠️ Hardware Requirements

| Component | Quantity | Notes |
|---|---|---|
| ESP32-S3 DevKit | 1 | Any ESP32-S3 board with ADC1 pins |
| IR Break-Beam Sensor Pair 1 | 1 | Analog output preferred |
| IR Break-Beam Sensor Pair 2 | 1 | Redundant channel for anti-dust robustness |
| Status LED | 1 | Optional (built-in LED works) |
| Jumper wires | — | — |
| USB power supply | 1 | 5V via USB |

---

## 🔌 Wiring Diagram

| ESP32-S3 Pin | Connected To | Function |
|---|---|---|
| `GPIO 1` (ADC1_CH0) | IR Sensor 1 analog out | Seed detection channel 1 |
| `GPIO 2` (ADC1_CH1) | IR Sensor 2 analog out | Seed detection channel 2 |
| `GPIO 8` | Status LED (anode) | Activity indicator |
| `GND` | Sensor GND, LED cathode | Common ground |
| `3V3` | Sensor VCC | Power |

> ⚠️ **Note:** Both sensors must break simultaneously for a valid seed detection. This dual-channel logic prevents false triggers from dust, debris, or vibration.

---

## 🧠 Seed Counting Algorithm

The system uses a **three-state finite state machine** with microsecond timing to classify events:

| Event | Sustained Beam Break | Interpretation |
|---|---|---|
| **Noise** | < 1.5 ms | Dust particle or chatter |
| **Single Seed** | 1.5 – 4.0 ms | One seed |
| **Double Seed** | ≥ 4.0 ms | Two or more seeds stuck together |
| **Skip** | > 300 ms gap after last seed | Missing seed in sequence |

Timing constants can be tuned in the source code:

```cpp
#define T_LOW_US        1500    // < 1.5 ms = noise/dust
#define T_HIGH_US       4000    // >= 4.0 ms = double seed
#define T_DEBOUNCE_US   50000   // 50 ms debounce after seed exits
#define SKIP_TIMEOUT_US 300000  // 300 ms without seed = skip
```

---

## 📊 Dashboard Metrics

| Metric | Description |
|---|---|
| **Total Seeds** | Cumulative count of all seeds detected |
| **Singulation %** | Percentage of single-seed drops vs. total events |
| **Skips** | Missed seed placements |
| **Doubles** | Events where two or more seeds dropped together |
| **Seed Rate** | Seeds per meter (based on planter speed & uptime) |
| **Seeds / Minute** | Throughput rate |
| **Uptime** | Session duration in seconds |
| **Noise Filtered** | Dust/debris events discarded |

---

## 🚀 Getting Started

### 1. Prerequisites

Install the following in the **Arduino IDE**:

- **ESP32 Board Package** by Espressif (v2.0.0 or later)
- **ArduinoJson** library by Benoit Blanchon (v6.x)

Board selection: **ESP32S3 Dev Module**

### 2. Configure WiFi

Open the `.ino` file and edit the AP credentials near the top:

```cpp
const char* ssid = "Jerry's A34";
const char* password = "Jerry321";   // Must be ≥ 8 characters
```

The AP IP is **fixed** at `192.168.4.1` (configurable in the same file).

### 3. Upload

1. Connect the ESP32-S3 via USB
2. Select the correct **COM port** and **board**
3. Click **Upload**

### 4. Connect to the Dashboard

1. Open **Serial Monitor** at **115200 baud**
2. Wait for the boot banner:
   ```
   ========================================
     NETWORK READY
   ========================================
     WiFi SSID     : Precision Planter
     WiFi Password : Jabari_2026
     Dashboard URL : http://192.168.4.1
   ========================================
   ```
3. On your laptop or phone, connect to WiFi **`Precision Planter`** (password: `Jabari_2026`)
4. Ignore the *"No Internet"* warning — this is a local AP
5. Open a browser and go to **`http://192.168.4.1`**
6. The dashboard loads instantly 🎉

---

## 📟 Serial Monitor Output

### Event Log (real-time)

```
[SINGLE] Count: 42 | Sustain: 2310 us
[SINGLE] Count: 43 | Sustain: 2180 us
[DOUBLE] Count: 45 | Sustain: 4520 us
[NOISE]  Sustain: 890 us
[SKIP]   Detected! Total skips: 3
```

### Metric Summary (every 2 seconds)

```
+----------------------------------------------+
|            SEEDER LIVE METRICS               |
+----------------------------------------------+
|  Total Seeds      : 247                      |
|  Singles          : 220                      |
|  Doubles          : 12                       |
|  Skips            : 8                        |
|  Noise Filtered   : 15                       |
|  Singulation      :   89.1 %                 |
|  Seed Rate        :   0.34 seeds/m           |
|  Seeds / Minute   :   43.3                   |
|  Planter Speed    :   6.0 km/h               |
|  Uptime           : 342 s                    |
+----------------------------------------------+
```

The reporting interval can be adjusted:

```cpp
#define SERIAL_REPORT_INTERVAL_MS  2000   // Every 2 seconds
```

---

## 🌐 API Endpoints

The dashboard communicates with the ESP32 via a simple REST API.

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Serves the HTML dashboard |
| `/api/data` | GET | Returns all metrics as JSON |
| `/api/speed?value=X` | POST | Set planter speed (0–20 km/h) |
| `/api/reset` | POST | Reset all counters |

### Sample JSON Response (`/api/data`)

```json
{
  "seedCount": 247,
  "singleCount": 220,
  "doubleCount": 12,
  "skipCount": 8,
  "noiseCount": 15,
  "singulation": 89.1,
  "seedRate": 0.34,
  "seedsPerMin": 43.3,
  "planterSpeed": 6.0,
  "uptimeSec": 342
}
```

---

## 📁 Project Structure

```
JABARI-Precision-Seeder/
├── JABARI_Seeder.ino        # Main ESP32-S3 firmware
├── README.md                # This file
└── LICENSE                  # MIT License
```

---

## ⚙️ Tuning Tips

### Adjusting Sensor Sensitivity

If your sensors trigger on ambient light or don't trigger on seeds, modify:

```cpp
#define BEAM_THRESHOLD  3500   // Lower = less sensitive; Higher = more sensitive
```

- **Beam broken** when ADC reading **< threshold**
- **Beam intact** when ADC reading **≥ threshold**

### Reducing False Doubles

If normal single seeds are being classified as doubles, lower `T_HIGH_US`:

```cpp
#define T_HIGH_US  3500   // Was 4000
```

### Reducing False Skips

If skips trigger too aggressively on slow planting, increase `SKIP_TIMEOUT_US`:

```cpp
#define SKIP_TIMEOUT_US  500000   // Was 300000 (500 ms)
```

### Anti-Dust Robustness

The code requires **both** IR sensors to break simultaneously. If you only have one sensor, change:

```cpp
bool beamBroken = sensor1Broken && sensor2Broken;   // AND
// to:
bool beamBroken = sensor1Broken || sensor2Broken;   // OR
```

---

## 🐛 Troubleshooting

| Problem | Solution |
|---|---|
| Can't find WiFi SSID | Check Serial Monitor for boot errors; re-flash firmware |
| Dashboard won't load | Verify you're on `http://` not `https://`; confirm IP is `192.168.4.1` |
| Counts wildly high | Lower `BEAM_THRESHOLD`; check sensor alignment |
| Counts zero | Verify wiring on GPIO 1 & 2; check sensor power |
| Serial garbage | Ensure baud rate is **115200** |
| "No Internet" popup | **Normal** — click "Keep Connection" |

---

## 🧪 Testing Without Hardware

To verify the dashboard without sensors, temporarily replace `processSensors()` in `loop()` with a random seed generator, or simply connect GPIO 1 and 2 to GND momentarily to simulate a beam break.

---

## 🛣️ Roadmap

- [ ] SD card logging for field data
- [ ] GPS integration for spatial seed mapping
- [ ] OTA firmware updates
- [ ] CSV export from dashboard
- [ ] Multi-row monitoring (multiple sensor pairs)
- [ ] Battery-powered field deployment

---

## 🤝 Contributing

Contributions, issues, and feature requests are welcome!  
Feel free to open an issue or submit a pull request.

1. Fork the repo
2. Create your feature branch: `git checkout -b feature/AmazingFeature`
3. Commit your changes: `git commit -m 'Add AmazingFeature'`
4. Push to the branch: `git push origin feature/AmazingFeature`
5. Open a Pull Request

---

## 📜 License

This project is licensed under the **MIT License** — see the `LICENSE` file for details.

---

## 👥 Authors

**JABARI Team**  
Developed for **IEEE YESIST12** — Precision Agriculture Track

---

## 🙏 Acknowledgements

- Espressif Systems for the ESP32-S3 platform
- Benoit Blanchon for the ArduinoJson library
- IEEE YESIST12 organizing committee

---

## 📸 Screenshots

> *Add dashboard screenshots here once available*

```
┌─────────────────────────────────────────────┐
│  🌱 JABARI Precision Planter                │
│  Real-time seed monitoring · IEEE YESIST12  │
├──────────────┬──────────────┬───────────────┤
│ Singulation  │  TOTAL SEEDS │ Session uptime│
│    89.1%     │     247      │     342s      │
│  ⚠ Skips  ⚡ │  43.3 /min   │ Noise: 15     │
│    8     12  │              │               │
│  Seed Rate   │              │ ESP32-S3 · AP │
│  0.34 s/m    │              │               │
└──────────────┴──────────────┴───────────────┘
```

---

**⭐ If this project helped you, consider giving it a star!**
```
