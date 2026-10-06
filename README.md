# 📷 AI-Based Industrial Gauge & Resistor Monitoring System

Embedded monitoring system built on **ESP32-S3** for resistor calculation and industrial gauge monitoring using camera sensing, Gemini AI, OLED feedback, Telegram notifications, and audio alerts.

<p>
  <img src="https://img.shields.io/badge/ESP32--S3-Embedded-red?style=flat-square" />
  <img src="https://img.shields.io/badge/OV5640-Camera-blue?style=flat-square" />
  <img src="https://img.shields.io/badge/Gemini-AI-4285F4?style=flat-square" />
  <img src="https://img.shields.io/badge/Telegram-Bot-26A5E4?style=flat-square" />
  <img src="https://img.shields.io/badge/Arduino-C%2FC++-00979D?style=flat-square" />
</p>

---

## 🚀 Project Overview

This project was developed as a second-year Mechatronics Engineering project to reduce errors from manually reading resistor color bands and industrial gauge values.

The system is built around an **ESP32-S3** and operates in two main modes.

### 🔢 Resistor Mode

Users select resistor color bands through the system interface.

The ESP32-S3 calculates the resistance value and tolerance locally without requiring cloud AI.

The result is displayed through the web interface and OLED display.

### 📷 Gauge Monitoring Mode

An **OV5640 Auto Focus Camera** captures an image of an industrial pressure gauge.

The ESP32-S3 sends the image to **Gemini Cloud AI through HTTPS**, where the gauge value and operating condition are analyzed.

The system classifies the gauge condition into:

- 🟢 **SAFE**
- 🟡 **WARNING**
- 🔴 **DANGER**

Results are displayed on the OLED, sent through Telegram, and accompanied by audio alerts.

---

## 🔄 System Flow

<p align="center">
  <img src="assets/system-flowchart.png" width="700"/>
</p>

The system starts by connecting the ESP32-S3 to Wi-Fi and providing access to a local web interface.

From the web interface, the user can choose between **Resistor Mode** and **Gauge Mode**.

Gauge Mode can also operate automatically at a fixed monitoring interval.

---

## ⚙️ System Architecture

```text
                    ┌─────────────────┐
                    │     ESP32-S3    │
                    └────────┬────────┘
                             │
            ┌────────────────┴────────────────┐
            │                                 │
     Resistor Mode                       Gauge Mode
            │                                 │
   Color Band Selection                  OV5640 Camera
            │                                 │
    Local Calculation                    JPEG Image
            │                                 │
      Web + OLED                   HTTPS / Base64 / JSON
                                              │
                                        Gemini Cloud AI
                                              │
                                       Gauge Analysis
                                              │
                              ┌───────────────┼───────────────┐
                              │               │               │
                            SAFE           WARNING          DANGER
                              │               │               │
                              └──── OLED + Telegram + Audio ──┘
```

---

## 🛠️ Hardware

| Component | Function |
|---|---|
| ESP32-S3 CAM | Main controller |
| OV5640 Auto Focus Camera | Captures industrial gauge images |
| SSD1306 OLED 128×64 | Displays system status and results |
| MAX98357A | I2S audio amplifier |
| 3W Speaker | Audio notifications and danger alerts |
| Wi-Fi | Communication with Gemini API and Telegram |

### Wiring Diagram

👉 [View Wiring Diagram](assets/wiringdiagram.jpg)

---

## ✨ Key Features

- Dual-mode embedded system
- 4-band and 5-band resistor calculation
- Local resistor calculation on ESP32-S3
- OV5640 Auto Focus camera integration
- Industrial gauge image analysis using Gemini AI
- HTTPS and JSON communication
- Safe / Warning / Danger classification
- OLED status display
- Telegram notifications
- I2S audio feedback
- Automatic gauge monitoring mode
- Web-based control interface

---

## 🧪 Testing & Results

The system was tested at multiple gauge values to verify status classification and notification behavior.

| Gauge Value | Detected Zone | Status |
|---:|---|---|
| 230 bar | Green | SAFE |
| 300 bar | Yellow / Boundary | WARNING |
| 500 bar | Red | DANGER |

At the boundary between safety zones, the system selects the **higher-risk condition**.

### Test Images

👉 [View Test Images](assets/)

The folder includes:

- Resistor Mode result
- Safe Mode result
- Safe Telegram notification
- Warning Mode result
- Warning Telegram notification
- Danger Mode result
- Danger Telegram notification

---

## 🧠 Gauge Analysis Logic

The system sends the captured image together with an analysis prompt to Gemini AI.

Gemini returns structured information including:

```text
VALUE
COLOR
STATUS
CONFIDENCE
DETAIL
```

The ESP32-S3 processes the result and determines the required output.

### Status Logic

```text
GREEN  → SAFE
YELLOW → WARNING
RED    → DANGER
```

A Danger condition also activates a repeating audio warning.

---

## 🔧 Engineering Challenges & Solutions

### Camera Preview Latency

Continuous image streaming caused high processing load on the ESP32-S3.

**Solution:**  
The system was changed to capture frames only when Preview or Analyze was requested.

### Image Quality

Lower image quality caused inaccurate gauge readings.

**Solution:**  
Camera settings were adjusted to VGA resolution with improved JPEG quality.

### Old Camera Frames

Automatic monitoring occasionally reused an older frame.

**Solution:**  
Three old frames are discarded before capturing the image used for analysis.

### Power Stability

The ESP32-S3 occasionally restarted when powered from an unstable external supply.

**Solution:**  
Power delivery was changed to a more stable USB-C source.

### Audio Delay

Generating audio through online processing introduced unnecessary latency.

**Solution:**  
Audio files were stored locally and played directly through the I2S audio system.

---

## 🎥 Demo Video

▶️ [Watch Project Demo](https://www.youtube.com/watch?v=fDfO5gYfs7E)

---

## 📁 Repository Structure

```text
ai-industrial-gauge-resistor-monitoring/
│
├── README.md
├── original-project.ino
├── project-report.pdf
│
└── assets/
    ├── system-flowchart.png
    ├── wiringdiagram.jpg
    ├── Resistor Mode.jpg
    ├── Safe Mode.jpg
    ├── Safe Telegram.jpg
    ├── Warning Mode.jpg
    ├── Warning Telegram.jpg
    ├── Danger Mode.jpg
    └── Danger Telegram.jpg
```

---

## 💻 Source Code

👉 [View Source Code](original-project.ino)

---

## 📄 Project Report

👉 [View Full Project Report](project-report.pdf)

---

## 🔬 Personal Extension

After completing the original working project, I continued exploring an **on-device TinyML approach using Edge Impulse** as a personal extension.

This work is separate from the original project and focuses on experimenting with local image classification directly on the ESP32-S3.

The TinyML extension and experimental results will be documented separately.

---

## 👨‍💻 Author

**Pakinthon Chalermchai**  
Mechatronics Engineering Student  
King Mongkut's University of Technology Thonburi (KMUTT)
