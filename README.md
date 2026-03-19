# 🌱 Plant Monitoring Sensor Network — ESP32-S3 Firmware

![ESP32](https://img.shields.io/badge/ESP32--S3-E7352C?style=flat-square&logo=espressif&logoColor=white)
![C++](https://img.shields.io/badge/C/C++-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![PlatformIO](https://img.shields.io/badge/PlatformIO-FF7F00?style=flat-square&logo=platformio&logoColor=white)
![I2C](https://img.shields.io/badge/I2C-blue?style=flat-square)
![UART](https://img.shields.io/badge/UART-green?style=flat-square)
![MQTT](https://img.shields.io/badge/MQTT-660066?style=flat-square&logo=mqtt&logoColor=white)

A full-stack IoT system for comprehensive environmental monitoring with **13 sensors**, deep-sleep power optimization achieving **~92% power savings**, and I²C multiplexing for address conflict resolution.

> **This is the firmware repository.** See also: [Backend API](https://github.com/zaeem7744/Plant-Sensor-Network-Backend) | [Web Dashboard](https://github.com/zaeem7744/Plant-Sensor-Network-Web-Dashboard)

---

## 📸 Screenshots

### Web Dashboard — All Sensors Overview
![Dashboard Overview](Images/Dashboard%201.png)

### Dashboard — Detailed Sensor Views
<p>
  <img src="Images/Dashboard%202.png" width="45%" />
  <img src="Images/Dashboard%203.png" width="45%" />
</p>

### Dashboard — Charts & Statistics
![Dashboard Charts](Images/Dashboard%204.png)

### Backend API Running
![Backend](Images/Backend%201.png)

---

## 🔧 Features

- **13 Environmental Sensors** — Temperature, humidity, pressure, light (BH1750), soil moisture, pH, EC, and gas sensors (CO, CH₄, NH₃, H₂S, HCHO, O₂, O₃, Alcohol)
- **Deep-Sleep Power Optimization** — ~92% power savings using timed wake cycles and MOSFET-based power switching for peripherals
- **I²C Multiplexing** — Two TCA9548A multiplexers for resolving I²C address conflicts across 13 sensors
- **MOSFET Power Switching** — Individual power control for sensor groups to minimize idle current draw
- **Battery + Solar Powered** — Designed for long-term outdoor deployment
- **REST API Data Transmission** — Sends sensor readings to FastAPI backend over Wi-Fi
- **Modular Firmware Architecture** — Clean separation of sensor drivers, communication, and power management

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     ESP32-S3 MCU                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
│  │ Sensor   │  │  Power   │  │  Wi-Fi   │  │  Deep     │  │
│  │ Drivers  │  │  Mgmt    │  │  Client  │  │  Sleep    │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └─────┬─────┘  │
│       │              │             │               │        │
└───────┼──────────────┼─────────────┼───────────────┼────────┘
        │              │             │               │
   ┌────┴────┐    ┌────┴────┐   ┌───┴───┐     ┌────┴────┐
   │TCA9548A │    │ MOSFET  │   │FastAPI│     │ Timer   │
   │  Mux x2 │    │ Switch  │   │Backend│     │ Wakeup  │
   └────┬────┘    └─────────┘   └───┬───┘     └─────────┘
        │                           │
   ┌────┴────┐                 ┌────┴────┐
   │13 Sensor│                 │ SQLite  │
   │ Modules │                 │   DB    │
   └─────────┘                 └─────────┘
```

---

## 📡 Sensors

| Sensor | Measurement | Protocol |
|--------|------------|----------|
| BH1750 | Light Intensity (lux) | I²C |
| MS8607 | Temperature, Humidity, Pressure | I²C |
| Soil EC + pH | Electrical Conductivity, pH | Analog/I²C |
| MQ-135 (Alcohol) | Alcohol concentration (ppm) | Analog |
| MQ-4 (CH₄) | Methane LEL, Module Temp | Analog |
| MQ-7 (CO) | Carbon Monoxide | Analog |
| MQ-136 (H₂S) | Hydrogen Sulfide | Analog |
| HCHO Sensor | Formaldehyde (ppm) | Analog |
| MQ-137 (NH₃) | Ammonia | Analog |
| O₂ Sensor | Oxygen | Analog |
| O₃ Sensor | Ozone | Analog |

---

## 📂 Project Structure

```
├── Firmware/          # ESP32-S3 PlatformIO project
│   └── Plant Sensor Network/
│       ├── src/       # Main firmware source code
│       ├── lib/       # Sensor driver libraries
│       └── platformio.ini
├── Schemetic/         # Circuit schematics and PCB design files
├── Images/            # Dashboard and system screenshots
├── Documents/         # Technical documentation, BOM
└── Requirements/      # System specifications
```

---

## 🛠️ Tech Stack

- **MCU:** ESP32-S3
- **Language:** C/C++
- **IDE:** PlatformIO
- **Protocols:** I²C, UART, Wi-Fi, HTTP REST
- **Power:** Deep sleep, MOSFET switching, battery + solar
- **Backend:** [FastAPI + SQLite](https://github.com/zaeem7744/Plant-Sensor-Network-Backend)
- **Frontend:** [React + TypeScript + Tailwind CSS](https://github.com/zaeem7744/Plant-Sensor-Network-Web-Dashboard)

---

## 👤 Author

**Muhammad Zaeem Sarfraz**
- 🔗 [LinkedIn](https://www.linkedin.com/in/zaeemsarfraz7744/)
- 📧 Zaeem.7744@gmail.com
- 🌍 Vaasa, Finland
