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

An **OV5640 Auto Focus camera** captures an image of an industrial pressure gauge.

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
