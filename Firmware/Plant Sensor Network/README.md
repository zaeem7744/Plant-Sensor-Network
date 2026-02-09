# Plant Sensor Network - ESP32-S3 Firmware

ESP32-S3 firmware for comprehensive environmental monitoring with deep sleep power management.

## 🌟 Features

- **13 Environmental Sensors**: Temperature, humidity, pressure, light, soil moisture, pH, EC, and multiple gas sensors
- **Deep Sleep Mode**: 30-minute sampling intervals with ~70% power savings
- **WiFi Connectivity**: Automatic data transmission to backend API
- **Dual Measurement Cycles**: Status check followed by reliable data upload
- **Manual Wake-up**: GPIO button for on-demand measurements
- **Status LED**: Visual feedback for device and WiFi status
- **I²C Multiplexing**: Two TCA9548A muxes handle address conflicts

## 📋 Hardware Requirements

- **ESP32-S3** development board
- **Sensors** (DFRobot and compatible modules):
  - MS8607 (Temperature, Humidity, Pressure)
  - BH1750 (Light)
  - MLX90614 (IR Temperature)
  - Soil Moisture (I²C Capacitive)
  - Alcohol, CH4, H2S, O2, NH3, CO, O3 (Gas sensors)
  - Soil EC + pH (UART)
  - HCHO (UART)
- **Two I²C Multiplexers**: TCA9548A or PCA9548A (addresses 0x70, 0x71)
- **MOSFET**: N-channel for 5V sensor power control (GPIO 14)
- **Button**: Wake-up button on GPIO 7 (pull-up integrated in ESP32)
- **Status LED**: Blue LED on GPIO 21 (with 4.7kΩ series resistor)
- **Power LEDs**: Green LEDs on sensor boxes (always ON when powered)

### Power Supply

- **Battery**: 3× 18650 Li-ion cells in parallel (2900 mAh each = **8700 mAh total**)
- **Voltage**: 3.7V nominal per cell
- **Boost Converter**: Generates stable 5V for ESP32 and sensors
- **Solar Panel**: 10W @ 18V for continuous charging (optional)

## 🔌 Pin Configuration

| Function | GPIO | Notes |
|----------|------|-------|
| I²C SDA | 8 | Connect to both multiplexers |
| I²C SCL | 9 | Connect to both multiplexers |
| Sensor Power | 14 | MOSFET gate (controls 5V rail) |
| UART2 RX (Soil EC+pH) | 19 | RS485 module |
| UART2 TX (Soil EC+pH) | 20 | RS485 module |
| UART1 RX (HCHO) | 5 | HCHO sensor |
| Wake Button | 7 | Button to GND (internal pull-up) |
| Status LED | 21 | Blue LED + 4.7kΩ resistor to GND |

## 🚀 Quick Start

### 1. Install PlatformIO

Install PlatformIO via VS Code extension or CLI:
- **VS Code**: Install "PlatformIO IDE" extension
- **CLI**: Follow [PlatformIO installation guide](https://platformio.org/install/cli)

### 2. Configure WiFi and Backend

Edit `src/config.h`:

```cpp
#define WIFI_SSID "YourWiFiNetwork"
#define WIFI_PASSWORD "YourWiFiPassword"
#define BACKEND_URL "http://YOUR_PC_IP:8000/api/ingest"
```

Replace:
- `YourWiFiNetwork` with your WiFi SSID
- `YourWiFiPassword` with your WiFi password
- `YOUR_PC_IP` with the IP address of your backend server

### 3. Upload Firmware

```bash
# From this directory
pio run --target upload

# Or use VS Code PlatformIO toolbar: Upload button
```

### 4. Monitor Serial Output

```bash
pio device monitor

# Or use VS Code PlatformIO toolbar: Serial Monitor
```

## ⚙️ Configuration Parameters

Edit `src/sensors.cpp` to adjust timing:

```cpp
// Wake-up interval (default: 30 minutes)
static const uint32_t SENSOR_SAMPLE_INTERVAL_MS = 1800000;

// Sensor warm-up time (default: 3 seconds)
static const uint32_t SENSOR_POWER_WARMUP_MS = 3000;

// Button active duration (default: 30 seconds)
static const uint32_t BUTTON_ACTIVE_TIME_MS = 30000;
```

**Common Intervals:**
- 5 minutes: `300000`
- 10 minutes: `600000`
- 30 minutes: `1800000` (default)
- 1 hour: `3600000`

## 🔋 Power Management

### Power Optimization Journey

The system underwent extensive power optimization to achieve maximum battery life:

**Initial Configuration (Always-ON)**:
- All sensors continuously powered
- ESP32 always active (no sleep)
- Peak current: **330-340 mA** (with methane + pH sensors)
- Daily consumption: ~8 Ah/day
- **Battery life: ~1 day** ❌

**Step 1: Identify High-Power Components**
- Methane sensor: Consumes ~140 mA
- pH probe: Causes ~40 mA spikes
- VOC sensors: ~37 mA combined
- Remaining sensors + ESP32: ~141 mA

**Step 2: MOSFET Power Switching**
- Added N-channel MOSFET on GPIO 14 to control sensor 5V rail
- Sensors powered ON only during readings
- Sensors OFF current: **6 mA** (vs 300 mA active)
- Power savings: **~98%** during idle ✅

**Step 3: ESP32 Deep Sleep**
- ESP32 always-on current: ~60 mA
- Optimized deep sleep current: **~20 mA**
- Power savings: **~67%** when sleeping ✅

**Final Optimized Configuration**:
- Sensors: Powered ON for ~8 seconds per cycle, OFF otherwise
- ESP32: Deep sleep between measurements (30-minute intervals)
- **Active current**: ~300 mA for 8 seconds
- **Sleep current**: ~26 mA (6 mA sensors OFF + 20 mA ESP32 deep sleep)
- **Overall power savings: ~92% compared to always-on design** 🎉

### Deep Sleep Mode

The firmware implements aggressive power saving:

1. **Active Period** (~8 seconds per 30-minute cycle):
   - Wake from deep sleep
   - Power ON sensors via MOSFET (GPIO 14)
   - Wait 3 seconds for sensor warm-up
   - Perform two measurement cycles
   - Upload data to backend via WiFi
   - Power OFF sensors via MOSFET
   - Enter deep sleep

2. **Deep Sleep** (~29 minutes 52 seconds):
   - ESP32 deep sleep: ~20 mA
   - Sensors powered OFF via MOSFET: ~6 mA
   - **Total sleep current: ~26 mA**
   - Wake-up sources: Timer or button (GPIO 7)

### Measured Power Consumption

| State | Current @ 5V | Duration (30-min cycle) | Energy |
|-------|--------------|-------------------------|--------|
| **Active (measuring)** | ~300 mA | ~8 seconds | 0.67 mAh |
| **Deep Sleep** | ~26 mA | ~1792 seconds | 12.95 mAh |
| **Average per cycle** | - | 1800 seconds (30 min) | **13.62 mAh** |

**Daily Consumption** (48 cycles):
- **653 mAh/day @ 5V** = **3.27 Wh/day**

### Battery Life Calculations

**With 8700 mAh Battery Pack (3× 18650 cells)**:

**Battery-only operation** (no solar):
- Usable capacity: 8700 mAh × 80% = 6960 mAh (80% DoD recommended)
- Runtime: 6960 mAh ÷ 653 mAh/day = **10.7 days** ≈ **11 days**

**With 10W Solar Panel**:
- Panel output: 10W @ 18V = ~0.56A peak
- Realistic daily generation (5 hours sunlight): 0.56A × 5h = **2.8 Ah/day**
- System consumption: 0.653 Ah/day
- **Net gain: +2.15 Ah/day** (surplus charges battery)
- **Runtime: Indefinite** ✅ (solar exceeds consumption by 4.3×)

**Cloudy day scenario** (1 hour effective sunlight):
- Solar generation: 0.56 Ah/day
- Net deficit: -0.653 + 0.56 = **-0.09 Ah/day** (slight deficit)
- Battery can sustain system for **77 days** on cloudy weather before full discharge

**No sunlight for extended period**:
- Battery lasts **~11 days** before requiring recharge

### Power Savings Summary

| Configuration | Daily Consumption | Battery Life (8700 mAh) | Improvement |
|---------------|-------------------|-------------------------|-------------|
| **Always-ON** | ~8000 mAh | 1.1 days | Baseline |
| **With optimizations** | ~653 mAh | 10.7 days | **12× longer** |
| **With 10W solar** | Net positive | Indefinite | **♾️** |

*Actual battery life depends on WiFi signal strength, temperature, sensor warm-up time, and solar panel exposure.*

## 🔄 Firmware Behavior

### Dual Measurement Cycle

Each wake-up performs **two complete cycles**:

1. **First Cycle (Status Check)**:
   - Read all sensors
   - Output to serial monitor
   - **Do NOT** post to backend
   - Used for reliability verification

2. **Second Cycle (Data Upload)**:
   - Read all sensors again
   - Output to serial monitor
   - **POST data** to backend API
   - Complete JSON printed with `COMPLETE JSON=` prefix

### Wake-up Sources

**Timer Wake-up (Automatic)**:
- Wakes every 30 minutes (configurable)
- Performs dual cycle
- Returns to deep sleep immediately

**Button Wake-up (Manual)**:
- Press button on GPIO 7
- Performs dual cycle
- Stays active for 30 seconds (LED on)
- Returns to deep sleep after timeout

### LED Status Indicators

**Blue Status LED (GPIO 21)**:

| Pattern | Meaning |
|---------|---------|
| Solid ON | Device awake, WiFi connected |
| Rapid blink (5×) | WiFi connection failed |
| Slow blink (1 Hz) | Button mode, WiFi offline |
| OFF | Deep sleep mode |

**Green Power LEDs (on sensor boxes)**:
- Always ON when sensors are powered (MOSFET enabled)
- OFF when sensors are unpowered (deep sleep)
- Not software-controllable

## 📡 Data Format

### JSON Payload Structure

```json
{
  "ts_ms": 1234567890,
  "MS8607": {
    "status": "online",
    "T": 24.5,
    "RH": 60.2,
    "P": 1013.25
  },
  "BH1750": {
    "status": "online",
    "lux": 185.0
  },
  "MLX90614": {
    "status": "online",
    "T_object": 19.8
  },
  "SOIL_CAP_I2C": {
    "status": "online",
    "moisture": 450
  },
  "ALCOHOL": {
    "status": "online",
    "value": 12.5
  },
  "CH4": {
    "status": "online",
    "value": 0.0
  },
  "SOIL_EC_PH": {
    "status": "online",
    "ec": 0.8,
    "ph": 6.5
  },
  "HCHO_UART": {
    "status": "online",
    "value": 0.01
  },
  "MULTIGAS": {
    "H2S": {"status": "online", "value": 0.0, "unit": "ppm"},
    "O2": {"status": "online", "value": 20.9, "unit": "%vol"},
    "NH3": {"status": "online", "value": 0.3, "unit": "ppm"},
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

**Status Field**: Each sensor reports one of:
- `"online"` – Successfully read
- `"offline"` – Failed to read or not responding
- `"warming_up"` – Sensor still stabilizing

## 🧰 Troubleshooting

### WiFi Connection Issues

**LED blinks rapidly 5 times:**
- Verify WiFi credentials in `config.h`
- Check WiFi signal strength
- Ensure 2.4 GHz WiFi (ESP32 doesn't support 5 GHz)
- Confirm backend server is accessible on local network

**Device connects but data not received:**
- Verify backend URL includes `/api/ingest` endpoint
- Check backend server is running on specified port
- Confirm firewall allows incoming connections on port 8000

### Sensor Reading Issues

**Sensor shows "offline" status:**
- Check I²C wiring (SDA = GPIO 8, SCL = GPIO 9)
- Verify sensor power supply (5V via MOSFET on GPIO 14)
- Ensure correct I²C multiplexer channel assignment
- Check serial monitor for initialization errors

**I²C bus errors:**
- Reduce I²C speed in `platformio.ini` (not typically needed)
- Add pull-up resistors (4.7kΩ) on SDA/SCL lines
- Verify no address conflicts (use `i2cdetect` equivalent)

### Serial Monitor Output

**No output on serial monitor:**
- Baud rate must be **115200**
- Check USB cable supports data (not charge-only)
- Press reset button on ESP32

**Garbled output:**
- Set baud rate to 115200
- Check for brownout (power supply issue)

## 📚 Additional Documentation

- **Main Documentation**: See `PROJECT_DELIVERY_DOCUMENTATION.md` in project root for complete system overview
- **Sensor Details**: Visit [DFRobot](https://www.dfrobot.com/) for sensor datasheets and specifications

## 🔗 Related Repositories

This firmware works with:
- **Backend API**: FastAPI server with SQLite database
- **Web Dashboard**: React-based real-time monitoring interface

See main project documentation for complete system setup.

## 📝 License

This project is proprietary and confidential. All rights reserved.

## 🙏 Acknowledgments

Sensor hardware sourced from [DFRobot](https://www.dfrobot.com/).

---

**Firmware Version**: 1.0  
**Last Updated**: February 2026  
**Board**: ESP32-S3  
**Framework**: Arduino (via PlatformIO)
