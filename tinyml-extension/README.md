# 🧠 TinyML Gauge Classification Extension

Personal extension of the original **AI-Based Industrial Gauge & Resistor Monitoring System**, exploring on-device image classification using **Edge Impulse TinyML** on the ESP32-S3.

<p align="center">
  <img src="https://img.shields.io/badge/Edge%20Impulse-TinyML-6C5CE7?style=for-the-badge" />
  <img src="https://img.shields.io/badge/ESP32--S3-On--Device%20Inference-303030?style=for-the-badge&logo=espressif&logoColor=white" />
  <img src="https://img.shields.io/badge/Input-96x96-2563EB?style=for-the-badge" />
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Classes-Safe%20%2F%20Warning%20%2F%20Danger-22C55E?style=flat-square" />
  <img src="https://img.shields.io/badge/Inference-Offline-F59E0B?style=flat-square" />
  <img src="https://img.shields.io/badge/Model-Edge%20Impulse%20Arduino-8B5CF6?style=flat-square" />
</p>

---

## 🚀 Overview

After completing the original project using **Gemini Cloud AI** for gauge image analysis, I independently explored whether the gauge status could be classified directly on the **ESP32-S3** using TinyML.

The goal of this extension was to experiment with:

- On-device image classification
- Reduced dependency on cloud AI
- Faster local inference
- Edge Impulse model deployment on ESP32-S3
- Gauge status classification into **Safe, Warning, and Danger**

This extension is an **experimental prototype** and is separate from the original working project.

---

## 🔄 Original Project vs TinyML Extension

```text
Original Project

OV5640 Camera
      ↓
ESP32-S3
      ↓
HTTPS
      ↓
Gemini Cloud AI
      ↓
Gauge Status
```

```text
TinyML Extension

OV5640 Camera
      ↓
ESP32-S3
      ↓
Crop + Resize to 96x96
      ↓
Edge Impulse Model
      ↓
Local Classification
      ↓
SAFE / WARNING / DANGER
```

The main difference is that the TinyML version performs classification **locally on the ESP32-S3** without sending the image to Gemini.

---

## 🧠 TinyML Model

The model was created and exported using **Edge Impulse**.

### Model Input

```text
Image Size: 96 × 96
Classes: 3
```

### Classification Labels

```text
danger
safe
warning
```

The exported model is provided as an Arduino library:

👉 [Edge Impulse Arduino Library](./ei-gauge-tinyml-classifier-arduino.zip)

---

## 📊 Dataset

The dataset is organized into three classes:

```text
dataset_96x96/
├── safe/
├── warning/
└── danger/
```

The dataset archive included in this repository contains:

| Class | Images |
|---|---:|
| Safe | 500 |
| Warning | 500 |
| Danger | 500 |
| **Total** | **1500** |

All images are stored at approximately **96 × 96 pixels** for TinyML experimentation.

👉 [Download Dataset](./dataset_96x96.zip)

---

## ⚙️ Image Processing Pipeline

The ESP32-S3 captures the original image from the OV5640 camera at VGA resolution.

Before inference, the firmware performs:

```text
OV5640 Camera
      ↓
JPEG Image
      ↓
Decode to RGB888
      ↓
Center Crop
      ↓
Resize to 96 × 96
      ↓
Edge Impulse Inference
```

The model input image can also be inspected through the ESP32 web interface.

---

## 🎯 Classification Logic

After inference, the firmware compares the confidence scores of all three classes.

The class with the highest score is selected.

A minimum confidence threshold is used:

```text
Minimum Confidence = 0.55
```

The result is then mapped to the gauge status:

```text
safe     → GREEN  → SAFE
warning  → YELLOW → WARNING
danger   → RED    → DANGER
```

If the highest confidence is below the threshold:

```text
STATUS → UNCERTAIN
```

---

## 🔊 System Output

After classification, the ESP32-S3 can provide local feedback through:

- OLED status display
- Web interface
- Audio alert
- Automatic monitoring mode

The firmware also supports automatic analysis every:

```text
30 seconds
```

---

## 💻 Firmware

The TinyML integration firmware is available here:

👉 [View TinyML Firmware](./ProjectTinyMl.ino)

The firmware integrates:

- ESP32-S3 camera
- OV5640 image capture
- Edge Impulse inference library
- OLED output
- I2S speaker alerts
- Local web interface
- Automatic gauge monitoring

---

## ⚠️ Experimental Limitations

This TinyML extension was created as a learning and experimentation project rather than a completed replacement for the original Gemini-based system.

During testing, several limitations were observed.

### Image Orientation

The model is sensitive to the orientation of the input image.

Differences between the orientation of training images and the live camera image can cause incorrect classification.

### Dataset Conditions

The dataset represents a limited set of gauge images and conditions.

Changes in:

- Lighting
- Camera angle
- Reflections
- Gauge position
- Image orientation

can affect classification accuracy.

### Image Preprocessing

The current pipeline mainly uses **center cropping and resizing to 96 × 96** before inference.

More robust preprocessing and data augmentation could improve model generalization.

### Classification Scope

The TinyML model currently classifies the gauge into:

- Safe
- Warning
- Danger

It does **not directly estimate the exact pressure value** like the original Gemini-based approach.

---

## 🔮 Future Improvements

Possible improvements include:

- Collecting more real-world gauge images
- Improving dataset diversity
- Making image orientation consistent between training and inference
- Adding more data augmentation
- Improving preprocessing before inference
- Evaluating model accuracy under different lighting conditions
- Comparing TinyML inference with Gemini Cloud AI
- Expanding the model to estimate gauge values instead of only status classes

---

## 📁 Files

```text
tinyml-extension/
├── README.md
├── ProjectTinyML.ino
├── dataset_96x96.zip
└── ei-gauge-tinyml-classifier-arduino.zip
```

---

## 🔗 Original Project

This TinyML work is an extension of the original working project:

👉 [Back to Main Project](../)

The original version uses **Gemini Cloud AI** for gauge reading and provides the main demonstrated system functionality.

---

## 👤 Author

**Pakinthon Chalermchai**  
Mechatronics Engineering Student, KMUTT

Exploring embedded systems, industrial monitoring, and practical TinyML applications.
