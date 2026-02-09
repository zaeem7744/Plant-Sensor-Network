# Plant Sensor Monitoring Network

A comprehensive IoT environmental monitoring system with ESP32-S3 firmware, FastAPI backend, and React web dashboard.

## 📚 Documentation

**For complete setup and technical details, see:**
- **[Delivery Documents/PROJECT_DELIVERY_DOCUMENTATION.md](./Delivery%20Documents/PROJECT_DELIVERY_DOCUMENTATION.md)** - Complete project documentation (4-5 pages)
- **[Delivery Documents/PROJECT_WORKFLOW_WIFI.md](./Delivery%20Documents/PROJECT_WORKFLOW_WIFI.md)** - Project workflow and system architecture

**Component-specific documentation:**
- **[Firmware README](./Firmware/Plant%20Sensor%20Network/README.md)** - ESP32-S3 firmware setup and configuration
- **[Backend README](D:/Projects/plant-sensor-backend/README.md)** - FastAPI server setup and API documentation
- **[Web Dashboard README](D:/Projects/Plant-Sensor-Network-Web-Dashboard/README.md)** - React dashboard setup and usage

## 🌟 Project Overview

This system monitors **13 environmental sensors** with optimized power management:

- **Temperature, Humidity, Pressure** (MS8607)
- **Light Intensity** (BH1750)
- **IR Object Temperature** (MLX90614)
- **Soil Moisture** (Capacitive I²C)
- **Soil EC + pH** (UART RS485)
- **Gas Sensors**: Alcohol, CH₄, HCHO, H₂S, O₂, NH₃, CO, O₃

### Key Features

✅ **Deep Sleep Power Management** - 30-minute sampling intervals (~70% power savings)  
✅ **WiFi Connectivity** - Automatic data transmission to backend API  
✅ **Real-time Web Dashboard** - Live monitoring with historical charts  
✅ **Status Tracking** - Online/offline sensor detection  
✅ **Manual Wake-up** - Button trigger for on-demand measurements  
✅ **Dual Measurement Cycles** - Reliability through redundancy

## 🚀 Quick Start

### Prerequisites

- **Firmware**: PlatformIO (VS Code extension or CLI)
- **Backend**: Python 3.8+ with pip
- **Frontend**: Node.js 16+ with npm

### 1. Configure Firmware

Edit `Firmware/Plant Sensor Network/src/config.h`:

```cpp
#define WIFI_SSID "YourWiFiNetwork"
#define WIFI_PASSWORD "YourPassword"
#define BACKEND_URL "http://YOUR_PC_IP:8000/api/ingest"
```

Upload to ESP32-S3 via PlatformIO.

### 2. Start Backend

```bash
cd D:\Projects\plant-sensor-backend
pip install -r requirements.txt
python -m uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

### 3. Start Frontend

```bash
cd Plant-Sensor-Network-Web-Dashboard
npm install
npm run dev
```

Configure `.env` with backend URL:
```env
VITE_BACKEND_URL=http://YOUR_PC_IP:8000
```

### 4. Power On ESP32

Device will connect to WiFi, collect sensor data, and transmit to backend automatically.

## 📊 System Architecture

```
┌─────────────────────┐
│   ESP32-S3 Firmware │
│   (13 Sensors)      │
│   Deep Sleep Mode   │
└──────────┬──────────┘
           │ WiFi POST
           │ (every 30 min)
           ▼
┌─────────────────────┐
│  FastAPI Backend    │
│  SQLite Database    │
│  REST API           │
└──────────┬──────────┘
           │ HTTP GET
           │ (real-time)
           ▼
┌─────────────────────┐
│   React Dashboard   │
│   Live Charts       │
│   Sensor Cards      │
└─────────────────────┘
```

## 🔋 Power Consumption

**Hardware:**
- Battery: 3× 18650 Li-ion (8700 mAh @ 3.7V)
- Solar: 10W @ 18V panel (optional)
- Boost converter: 3.7V → 5V

**Deep Sleep Implementation:**
- Active (measuring): ~300 mA for ~8 seconds
- Deep sleep: ~26 mA for ~29 min 52 sec
- **Average per cycle**: 13.62 mAh per 30 minutes
- **Daily consumption**: ~653 mAh @ 5V (~3.27 Wh/day)

**Battery Life:**
- Battery only: **~11 days**
- With 10W solar (5h sun): **Indefinite** (6× surplus)
- Cloudy days (1h sun): **77+ days** (slight deficit)

**Power Optimization:**
- Always-ON consumption: ~8 Ah/day (1 day runtime)
- Optimized consumption: ~0.65 Ah/day (11 days runtime)
- **Improvement: 12× longer battery life**
- **Power savings: ~92% compared to always-on operation**

## 📁 Repository Structure

```
Plant Monitoring Sensor Network/
├── README.md                            # This file (quick start guide)
├── Delivery Documents/
│   ├── PROJECT_DELIVERY_DOCUMENTATION.md # Complete delivery documentation
│   └── PROJECT_WORKFLOW_WIFI.md          # System architecture & workflow
├── Firmware/
│   └── Plant Sensor Network/
│       ├── src/
│       │   ├── sensors.cpp              # Main firmware code
│       │   ├── config.h                 # WiFi and backend config
│       │   └── DFRobotHCHOSensor.h
│       ├── platformio.ini
│       └── README.md
│
├── ../plant-sensor-backend/             # Backend located outside project folder
│   ├── main.py                          # FastAPI server
│   ├── sensors.db                       # SQLite database (auto-created)
│   ├── requirements.txt
│   └── README.md
│
└── Plant-Sensor-Network-Web-Dashboard/
    ├── src/
    │   ├── components/                  # React UI components
    │   ├── hooks/                       # Data fetching hooks
    │   ├── lib/                         # Utilities and config
    │   └── pages/                       # Dashboard pages
    ├── .env                             # Backend URL config
    ├── package.json
    └── README.md
```

## 🔧 Configuration

### Firmware (ESP32-S3)

**Key parameters** in `src/sensors.cpp`:
```cpp
SENSOR_SAMPLE_INTERVAL_MS = 1800000;  // 30 minutes
SENSOR_POWER_WARMUP_MS = 3000;        // 3 seconds
BUTTON_ACTIVE_TIME_MS = 30000;        // 30 seconds
```

### Backend (FastAPI)

**Run command**:
```bash
python -m uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

**Online threshold**: 10 seconds (configurable in `main.py`)

### Frontend (React)

**Environment variables** (`.env`):
```env
VITE_BACKEND_URL=http://YOUR_PC_IP:8000
```

**Polling interval**: 5 seconds (configurable in hooks)

## 📡 Hardware Connections

| Function | GPIO | Component |
|----------|------|-----------|
| I²C SDA | 8 | Two TCA9548A multiplexers |
| I²C SCL | 9 | Two TCA9548A multiplexers |
| Sensor Power | 14 | N-channel MOSFET (gate) |
| UART2 RX | 19 | Soil EC+pH (RS485) |
| UART2 TX | 20 | Soil EC+pH (RS485) |
| UART1 RX | 5 | HCHO sensor |
| Wake Button | 7 | Button to GND (10kΩ pull-up) |
| Status LED | 21 | LED + 330Ω resistor to GND |

**I²C Multiplexer Addresses:**
- MUX_A: 0x70 (MS8607, BH1750, Alcohol, CH4, H2S)
- MUX_B: 0x71 (MLX90614, Soil Moisture, O2, NH3, CO, O3)

## 📈 Data Storage

**Database Growth:**
- Per cycle: ~2 KB
- Daily: ~96 KB (48 cycles)
- Monthly: ~2.88 MB
- Yearly: ~35 MB

SQLite handles this easily. For long-term deployments, consider archiving after 6-12 months.

## 🧪 Sensor Testing Status

**Fully Tested:**
- MS8607, BH1750, MLX90614, Soil Moisture, H₂S, O₂

**Calibrated (Not Fully Tested):**
- NH₃, CH₄, CO (require hazardous gases for validation)

All sensors are properly configured. For official specifications, see [DFRobot](https://www.dfrobot.com/).

## 🛠️ Troubleshooting

### ESP32 Not Connecting

- Verify WiFi credentials in `config.h`
- Check backend URL is correct
- Ensure 2.4 GHz WiFi (ESP32 doesn't support 5 GHz)
- LED blinks rapidly if WiFi fails

### Backend Connection Issues

- Verify backend is running on correct port
- Check firewall allows port 8000
- Confirm PC IP address matches configuration

### Dashboard Shows No Data

- Verify `VITE_BACKEND_URL` in frontend `.env`
- Check backend is accessible at specified URL
- Open browser console (F12) for error messages

## 📚 Additional Resources

### Official Documentation
- **DFRobot**: https://www.dfrobot.com/
- **ESP32-S3**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/
- **FastAPI**: https://fastapi.tiangolo.com/
- **React**: https://react.dev/

### API Documentation
- **Swagger UI**: http://localhost:8000/docs (when backend running)
- **ReDoc**: http://localhost:8000/redoc

## 🎯 Use Cases

- **Plant Growth Monitoring**: Track temperature, humidity, light, soil moisture
- **Greenhouse Automation**: Monitor and optimize growing conditions
- **Air Quality Monitoring**: Detect harmful gases (H₂S, CO, O₃)
- **Research & Development**: Long-term environmental data collection
- **Smart Agriculture**: Soil EC, pH, moisture for precision farming

## 🚧 Known Limitations

1. **Gas sensor testing**: NH₃, CH₄, CO not validated with actual gases (safety constraints)
2. **WiFi dependency**: No local storage fallback (offline operation not supported)
3. **Fixed sample rate**: 30-minute interval hardcoded (requires firmware recompilation)

## 🔮 Future Enhancements

- SD card logging for offline operation
- Battery voltage monitoring with alerts
- Over-the-air (OTA) firmware updates
- Configurable sample rates via web dashboard
- SMS/email alerts for out-of-range readings
- Multi-device support (multiple ESP32 nodes)

## 📝 License

This project is proprietary and confidential. All rights reserved.

## 🙏 Acknowledgments

**Hardware**: Sensors sourced from [DFRobot](https://www.dfrobot.com/)  
**Software**: Built with FastAPI, React, and PlatformIO ecosystem

Thank you for using the Plant Sensor Monitoring Network. For questions or support, refer to component-specific READMEs or the main project documentation.

---

**Project Version**: 1.0  
**Last Updated**: February 2026  
**Status**: Production Ready  
**Power Savings**: ~70% with deep sleep mode
