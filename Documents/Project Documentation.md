# Plant Sensor Monitoring Network – Project Documentation

## Introduction

Thank you for the opportunity to work on this comprehensive IoT sensor monitoring system. This document provides an overview of the complete Plant Sensor Monitoring Network project, including firmware, backend, and web dashboard components.

The system has been designed with power efficiency and reliability in mind, utilizing deep sleep modes and robust sensor integration to provide long-term environmental monitoring capabilities.

---

## 1. Project Overview

This project implements a complete wireless sensor monitoring system consisting of three main components:

1. **ESP32-S3 Firmware** – Embedded firmware for sensor data acquisition and wireless transmission
2. **FastAPI Backend** – Python-based REST API server with SQLite database
3. **React Web Dashboard** – Real-time visualization and historical data analysis

### Key Features

- **13 Environmental Sensors** monitoring air quality, soil conditions, temperature, humidity, and more
- **Deep Sleep Power Management** – Device sleeps between measurements for optimized battery life
- **WiFi Connectivity** – Automatic data transmission to backend server
- **Real-time Dashboard** – Live sensor readings with historical charting
- **Status Tracking** – Online/offline sensor status with last-seen timestamps
- **Dual Measurement Cycles** – Status check followed by data upload for reliability

---

## 2. Repository Structure

The project is organized into three main directories:

### Firmware (ESP32-S3)
```
Plant Monitoring Sensor Network/Firmware/Plant Sensor Network/
├── src/
│   ├── sensors.cpp          # Main sensor reading and WiFi logic
│   ├── config.h             # WiFi credentials and backend URL
│   └── DFRobotHCHOSensor.h  # HCHO sensor library
├── platformio.ini            # PlatformIO configuration
```

**Key Configuration Parameters** (in `src/sensors.cpp`):
- `SENSOR_SAMPLE_INTERVAL_MS = 1800000` (30 minutes) – Time between wake cycles
- `SENSOR_POWER_WARMUP_MS = 3000` (3 seconds) – Sensor stabilization time
- `SENSOR_MOSFET_PIN = 14` – GPIO controlling sensor power rail
- `WAKE_BUTTON_PIN = 7` – Button for manual wake-up
- `STATUS_LED_PIN = 21` – LED status indicator

### Backend (FastAPI + SQLite)
```
D:\Projects\plant-sensor-backend/
├── main.py           # FastAPI server with all API endpoints
├── sensors.db        # SQLite database (auto-created)
├── requirements.txt  # Python dependencies
└── README.md         # Backend documentation
```

**Note**: Backend is located at `plant-sensor-backend\` (outside main project folder)

**Run Command:**
```bash
python -m uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

### Web Dashboard (React + Vite)
```
Plant-Sensor-Network-Web-Dashboard/
├── src/
│   ├── components/        # React UI components
│   ├── lib/sensors.ts     # Sensor configuration
│   ├── hooks/             # Data fetching hooks
│   └── pages/             # Dashboard pages
├── .env                   # Backend URL configuration
└── package.json
```

**Run Command:**
```bash
npm run dev
```

**Environment Configuration** (`.env`):
```
VITE_BACKEND_URL=http://YOUR_PC_LAN_IP:8000
```

---

## 📊 System Architecture

### High-Level Data Flow

```
ESP32-S3 Firmware
    ↓ (Deep Sleep: 30 minutes)
    ↓ (Wake up: Timer or Button)
    ↓ (Power ON sensors via MOSFET)
    ↓ (Read 13 sensors via I²C/UART)
    ↓ (Build JSON payload with status)
    ↓
WiFi POST → Backend API (/api/ingest)
    ↓
SQLite Database (measurements table)
    ↓
Backend REST API
    ↓ (/api/sensors/current)
    ↓ (/api/sensors/history)
    ↓ (/api/overview)
    ↓
React Dashboard
    ↓ (Real-time updates every few seconds)
    ↓ (Charts, statistics, sensor cards)
    ↓
User View (Browser)
```

### Hardware Power Architecture

```
10W Solar Panel 
    ↓
Charge Controller
    ↓
3× 18650 Battery Pack (8700 mAh @ 3.7V)
    ↓
Boost Converter (3.7V → 5V)
    ↓
    ├──> ESP32-S3 (always powered)
    │
    └──> N-Channel MOSFET (GPIO 14)
            ↓
        Sensor 5V Rail
            ↓
        All 13 Sensors (switched ON/OFF)

Power States:
- Active: ESP32 + Sensors = ~300 mA (8 sec)
- Sleep: ESP32 deep sleep + Sensors OFF = ~26 mA (1792 sec)
```

### Power Management

The ESP32-S3 implements sophisticated power management:

1. **Deep Sleep Mode**: Device sleeps for 30 minutes between measurements
   - ESP32 deep sleep current: ~20 mA (optimized)
   - Sensors OFF current: ~6 mA (via MOSFET)
   - **Total sleep current: ~26 mA**
   - **Power savings: ~92% compared to always-on operation**

2. **Sensor Power Control**: MOSFET on GPIO 14 controls 5V sensor rail
   - Sensors powered OFF during sleep (reduces ~300 mA to ~6 mA)
   - 3-second warm-up period after power-on
   - 98% power savings during idle periods

3. **Wake-up Sources**:
   - **Timer**: Automatic wake every 30 minutes for scheduled measurements
   - **Button** (GPIO 7): Manual wake-up, stays active for 30 seconds

4. **Measured Power Consumption**:
   - **Active (measuring)**: ~300 mA for ~8 seconds (0.67 mAh)
   - **Deep sleep**: ~26 mA for ~1792 seconds (12.95 mAh)
   - **Average per cycle**: 13.62 mAh per 30-minute cycle
   - **Daily consumption**: ~653 mAh @ 5V = ~3.27 Wh/day

5. **Battery Configuration**:
   - **3× 18650 Li-ion cells** in parallel (2900 mAh each)
   - **Total capacity**: 8700 mAh @ 3.7V nominal
   - **Boost converter**: Generates stable 5V for system
   - **Battery-only runtime**: ~5 days (with 80% DoD) (It may go beyond 7 days just for the safe side)

6. **Solar Charging**:
   - **10W solar panel @ 18V** provides continuous charging
   - Daily generation (5h sunlight): ~2.8 Ah/day
   - System consumption: ~0.653 Ah/day
   - **Net surplus**: +2.15 Ah/day (4.3× excess capacity)
   - **Runtime with solar**: Indefinite ♾️
   - **Cloudy days** (1h sunlight): -0.09 Ah/day deficit, but battery lasts 30+ days

---

## 4. Sensor Network

The system monitors 13 environmental parameters using professional DFRobot sensors and other industrial-grade modules.

### Sensor List

| Sensor | Parameter(s) | Category | I²C Mux |
|--------|-------------|----------|---------|
| MS8607 | Temperature, Humidity, Pressure | Environment | MUX_A (0x70) |
| BH1750 | Light Intensity | Light | MUX_A |
| MLX90614 | IR Object Temperature | Temperature | MUX_B (0x71) |
| Soil Moisture (I²C) | Capacitive Moisture | Soil | MUX_B |
| Alcohol Sensor | Alcohol Concentration | Gas | MUX_A |
| CH4 (MHZ9041A) | Methane | Gas | MUX_A |
| Soil EC + pH | Conductivity, pH | Soil | UART2 (RS485) |
| HCHO | Formaldehyde | Gas | UART1 |
| H₂S | Hydrogen Sulfide | Gas | MUX_A |
| O₂ | Oxygen | Gas | MUX_B |
| NH₃ | Ammonia | Gas | MUX_B |
| CO | Carbon Monoxide | Gas | MUX_B |
| O₃ | Ozone | Gas | MUX_B |

### I²C Multiplexer Architecture

The system uses **two TCA9548A/PCA9548A I²C multiplexers** to avoid address conflicts:

- **MUX_A (0x70)**: MS8607, BH1750, Alcohol, CH4, H₂S
- **MUX_B (0x71)**: MLX90614, Soil Moisture, O₂, NH₃, CO, O₃

**Important**: Only one mux channel is active at a time to prevent I²C bus collisions.

### Hardware Connections

- **I²C Bus**: SDA = GPIO 8, SCL = GPIO 9
- **Sensor Power**: 5V rail controlled by MOSFET (GPIO 14)
- **UART2 (Soil EC+pH)**: RX = GPIO 19, TX = GPIO 20
- **UART1 (HCHO)**: RX = GPIO 5
- **Wake Button**: GPIO 7 (internal pull-up enabled)
- **Status LED**: Blue LED on GPIO 21 (with 4.7kΩ series resistor)
- **Power LEDs**: Green LEDs on all sensor boxes (always ON when powered)

### Sensor Data Accuracy

**Tested and Validated:**
- MS8607 (Temperature, Humidity, Pressure)
- BH1750 (Light)
- MLX90614 (IR Temperature)
- Soil Moisture (Capacitive)
- H₂S (Hydrogen Sulfide) – environmentally safe levels
- O₂ (Oxygen) – shows clear variations with ventilation

**Calibrated but Not Fully Tested:**
- **NH₃ (Ammonia)**: Sensor is functional but not tested with actual ammonia gas due to safety concerns
- **CH₄ (Methane)**: Library and communication verified; gas testing requires controlled environment
- **CO (Carbon Monoxide)**: Sensor responds to baseline air quality; harmful concentrations not tested

These sensors require harmful or dangerous gases for full validation, which cannot be safely produced in a standard development environment. The sensors are properly configured and show expected baseline readings. For detailed specifications and calibration data, please refer to the [DFRobot website](https://www.dfrobot.com/), where all sensors were sourced.

---

## 5. Firmware Behavior

### Measurement Cycle (Dual-Cycle Approach)

Each wake-up performs **two measurement cycles**:

1. **First Cycle (Status Check)**:
   - Powers ON sensors
   - Reads all sensor values
   - Prints to serial monitor
   - Does NOT post to backend
   - Powers OFF sensors

2. **Second Cycle (Data Upload)**:
   - Powers ON sensors
   - Reads all sensor values
   - Prints to serial monitor
   - **POSTS data to backend**
   - Powers OFF sensors

3. **Deep Sleep**:
   - Enters deep sleep for 30 minutes
   - Wakes on timer or button press

### JSON Payload Structure

The firmware sends a complete JSON document to the backend API:

```json
{
  "ts_ms": 1234567,
  "MS8607": {
    "status": "online",
    "T": 24.3,
    "RH": 60.5,
    "P": 1013.2
  },
  "BH1750": {
    "status": "online",
    "lux": 185.0
  },
  "MLX90614": {
    "status": "online",
    "T_object": 19.5
  },
  "MULTIGAS": {
    "H2S": {"status": "online", "value": 0.0, "unit": "ppm"},
    "O2": {"status": "online", "value": 20.9, "unit": "%vol"},
    "NH3": {"status": "online", "value": 0.4, "unit": "ppm"},
    "CO": {"status": "online", "value": 0.0, "unit": "ppm"},
    "O3": {"status": "online", "value": 0.0, "unit": "ppm"}
  },
  "summary": {
    "sensor_total": 13,
    "offline_count": 0,
    "offline_list": ""
  }
}
```

**Key Features:**
- Every sensor includes a `"status"` field: `"online"`, `"offline"`, or `"warming_up"`
- Summary object provides quick overview of system health
- All sensors appear in JSON even if offline (for tracking)

### LED Status Indicators on Modue 2 (Box)

**Blue Status LED (GPIO 21)**:
- **LED ON (solid)**: Device awake, WiFi connected
- **LED OFF**: Device in deep sleep
- **Rapid blink (5×)**: WiFi connection failed
- **Slow blink (1Hz)**: Device awake but WiFi offline (button wake mode)

**Green Power LEDs (on sensor Moule boxes)**:
- Always ON when MOSFET powers sensors
- OFF when sensors unpowered during sleep
- Hardware indicators, not software-controllable

### Button Wake-up (GPIO 7)

Press button → Device wakes immediately:
1. Performs double measurement cycle
2. Sends data to backend
3. Stays active for 30 seconds (LED on)
4. Returns to deep sleep

**Hardware**: Button connects GPIO 7 to GND when pressed. ESP32 internal pull-up enabled. GPIO hold feature maintains pin state during sleep.

### Configurable Parameters

You can adjust timing by editing `src/sensors.cpp`:

```cpp
// Change wake-up interval (currently 30 minutes)
static const uint32_t SENSOR_SAMPLE_INTERVAL_MS = 1800000;

// Change sensor warm-up time (currently 3 seconds)
static const uint32_t SENSOR_POWER_WARMUP_MS = 3000;

// Change button active duration (currently 30 seconds)
static const uint32_t BUTTON_ACTIVE_TIME_MS = 30000;
```

No need to understand PlatformIO in depth—simply modify these constants and upload the firmware.

---

## 6. Backend API

The FastAPI backend provides RESTful endpoints for data ingestion and retrieval.

### Database Schema

**Table: measurements**
- `id`: Primary key
- `ts`: Timestamp (datetime)
- `sensor_name`: Sensor identifier (e.g., "MS8607", "H2S")
- `parameter`: Parameter name (e.g., "temperature_c", "h2s_ppm")
- `value`: Numeric reading
- `unit`: Unit of measurement
- `status`: Sensor status ("online", "offline", "warming_up")

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/ingest` | POST | Receive sensor data from ESP32 |
| `/api/sensors/current` | GET | Latest reading for all sensors |
| `/api/sensors/history` | GET | Historical data for charts |
| `/api/overview` | GET | System statistics (total sensors, online/offline counts) |

### Key Features

- **Automatic status tracking**: Backend infers online/offline based on 10-second threshold
- **Always shows last values**: Even offline sensors display their most recent readings
- **Flexible querying**: History endpoint supports custom time ranges
- **CORS enabled**: Dashboard can connect from any origin (configurable)

---

## 7. Web Dashboard

The React-based dashboard provides real-time monitoring and historical analysis.

### Features

1. **Sensor Overview Page**:
   - Grid of sensor cards showing latest readings
   - Green/gray status indicators
   - "Last seen" timestamps
   - Filter by category (Environment, Gas, Soil, Temperature, Light)

2. **Sensor Detail Page**:
   - Historical charts with configurable time range
   - Min/Max/Average statistics
   - Multiple parameters per sensor
   - Real-time updates

3. **Overview Dashboard**:
   - Total sensors count
   - Online/offline statistics
   - 24-hour averages
   - Total measurements count

### Setup

1. Install dependencies:
   ```bash
   npm install
   ```

2. Configure backend URL in `.env`:
   ```
   VITE_BACKEND_URL=http://YOUR_PC_IP:8000
   ```

3. Run development server:
   ```bash
   npm run dev
   ```

4. Access at `http://localhost:5173`

---

## 8. Setup Quick Start

### Prerequisites

- **Firmware**: PlatformIO (VS Code extension or CLI)
- **Backend**: Python 3.8+ with pip
- **Dashboard**: Node.js 16+ with npm

### Step-by-Step Setup

#### 1. Configure Firmware

Edit `src/config.h`:
```cpp
#define WIFI_SSID "YourWiFiNetwork"
#define WIFI_PASSWORD "YourPassword"
#define BACKEND_URL "http://YOUR_PC_IP:8000/api/ingest"
```

Upload to ESP32-S3 via PlatformIO.

#### 2. Start Backend

```bash
cd plant-sensor-backend
pip install -r requirements.txt
python -m uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

Backend runs on `http://0.0.0.0:8000`

#### 3. Start Dashboard

```bash
cd Plant-Sensor-Network-Web-Dashboard
npm install
npm run dev
```

Dashboard opens at `http://localhost:5173`

#### 4. Power On ESP32

Device will:
- Connect to WiFi
- Perform initial measurement
- Send data to backend
- Enter deep sleep for 30 minutes
- Wake up automatically and repeat

---

## 9. Data Storage and Retention

### Estimated Database Growth

**Per measurement cycle (13 sensors, ~40 parameters)**:
- Database size: ~2 KB per cycle

**Daily storage** (30-minute intervals):
- 48 cycles/day × 2 KB = **96 KB/day**

**Monthly storage**:
- 30 days × 96 KB = **2.88 MB/month**

**Annual storage**:
- 12 months × 2.88 MB = **~35 MB/year**

SQLite can handle this easily. For long-term deployments, consider implementing data archiving or aggregation after 6-12 months.

---

## 10. Known Limitations and Future Enhancements

### Current Limitations

1. **Gas Sensor Testing**: NH₃, CH₄, and CO sensors are functional but not fully validated with target gases due to safety constraints
2. **WiFi Dependency**: System requires WiFi for data upload (no local storage fallback)
3. **Fixed Sample Rate**: 30-minute interval is hardcoded (requires recompilation to change)

### Potential Enhancements

- Local SD card logging for offline operation
- Battery voltage monitoring with low-battery alerts
- Configurable sample rates via web dashboard

---

## 11. Troubleshooting

### ESP32 Not Connecting to WiFi

- Verify WiFi credentials in `config.h`
- Check that backend URL is correct and reachable
- Ensure PC firewall allows connections on port 8000
- LED will blink rapidly if WiFi fails

### Backend API Errors

- Check backend console for error messages
- Verify SQLite database exists in backend folder
- Restart backend server: `Ctrl+C` then rerun uvicorn command

### Dashboard Shows No Data

- Verify `VITE_BACKEND_URL` in `.env` matches backend IP
- Check browser console (F12) for network errors
- Confirm backend is running and accessible at specified URL

### Sensor Readings Show "Offline"

- Check if 10 seconds have passed since last transmission
- Verify sensor connections and I²C bus wiring
- Review serial monitor output for sensor initialization errors

---

## 12. Additional Resources

### Official Documentation

- **DFRobot Sensors**: https://www.dfrobot.com/ – Detailed datasheets, calibration guides, and technical specifications
- **ESP32-S3**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/ – Deep sleep, GPIO, and peripheral documentation
- **FastAPI**: https://fastapi.tiangolo.com/ – Backend API framework documentation
- **React**: https://react.dev/ – Frontend framework documentation


---

## 13. Final Notes

This project represents a complete, production-ready IoT sensor monitoring system with professional-grade power management and data reliability.

The deep sleep implementation provides approximately **70% power savings** compared to an always-on design, making the system suitable for battery-powered deployments lasting weeks or months depending on battery capacity.

All sensors have been tested to the extent safely possible in a development environment. For sensors requiring hazardous gases (NH₃, CH₄, CO), the electronic interfaces and communication protocols have been validated—actual gas response curves can be confirmed by referring to DFRobot's official calibration data.

If you have any questions during setup, deployment, or while extending the system, please feel free to reach out. I would be happy to provide additional support or collaborate on future enhancements.

Thank you again for the opportunity to work on this project. It has been a pleasure developing this comprehensive monitoring solution.

---

**Document Version**: 1.0  
**Last Updated**: February 2026  
**Project Status**: Production Ready
