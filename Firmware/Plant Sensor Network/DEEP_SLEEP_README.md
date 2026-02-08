# ESP32-S3 Deep Sleep Implementation

## Overview
The firmware now uses ESP32-S3 deep sleep mode to significantly reduce power consumption between sensor measurements.

## Changes Made

### 1. Complete JSON Serial Output
- **Before**: Only MULTIGAS JSON was printed to serial monitor
- **After**: Complete JSON payload is now printed with `COMPLETE JSON=` prefix, followed by the full JSON document

### 2. Deep Sleep Integration

#### Power Saving
- Device enters **deep sleep** between measurements instead of using `delay()`
- Sleep duration: **30 seconds** (configurable via `SENSOR_SAMPLE_INTERVAL_MS`)
- Current consumption drops to ~10-150μA during sleep (vs ~80-240mA when active)

#### Wake-up Sources

**1. Timer Wake-up (Scheduled Measurements)**
- ESP32 wakes every 30 seconds automatically
- Runs measurement cycle → POSTs data → enters deep sleep
- Normal operation mode

**2. Button Wake-up (GPIO 7)**
- Button connected between GPIO 7 and GND
- Internal pull-up enabled on GPIO 7
- Press button → ESP32 wakes immediately
- **Behavior after button wake**:
  1. Runs ONE complete measurement cycle
  2. Sends data to backend
  3. Stays **active for 30 seconds** (can interact, monitor serial output)
  4. Then returns to deep sleep

### Hardware Connections

#### Wake-up Button
```
GPIO 7 ----[Button]---- GND
         (internal pull-up enabled)
```

Press button to wake device manually and trigger immediate measurement + data transmission.

## Code Structure

### New Includes
```cpp
#include <esp_sleep.h>  // ESP32 deep sleep API
```

### New Constants
```cpp
static const int WAKE_BUTTON_PIN = 7;
static const uint64_t DEEP_SLEEP_INTERVAL_US = 30000 * 1000ULL;  // 30s in microseconds
static const uint32_t BUTTON_ACTIVE_TIME_MS = 30000;  // Stay active 30s after button wake
```

### Key Functions

#### `enterDeepSleep()`
Configures and enters deep sleep mode:
- Enables timer wake-up (30 seconds)
- Enables external wake-up (button on GPIO 7, LOW level)
- Flushes serial output
- Calls `esp_deep_sleep_start()`

#### Wake-up Logic in `setup()`
```cpp
esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
// Prints wake reason: BUTTON, TIMER, or POWER-ON
```

#### Button Active Mode in `loop()`
- Detects button wake
- Runs measurement cycle
- Stays awake for 30 seconds
- Returns to deep sleep

## Serial Monitor Output

### Wake-up Messages
```
Wake-up: BUTTON pressed on GPIO 7
Wake-up: TIMER (scheduled measurement)
Wake-up: POWER-ON or RESET
```

### JSON Output
```
JSON payload length=551
COMPLETE JSON=
{"ts_ms":12345,"MS8607":{...},"BH1750":{...},"MULTIGAS":{...},...}

MULTIGAS JSON=
{"H2S":{"value":0,"unit":"ppm"},"O2":{"value":20.9,"unit":"%vol"},...}
```

### Deep Sleep Messages
```
Button measurement complete, staying active for 30 seconds...
(or)
Entering deep sleep mode...
Deep sleep configured: wake on timer or button press
```

## Power Consumption Estimates

### Without Deep Sleep (Original)
- Active mode: ~80-240 mA continuously
- 30s interval → average ~150 mA
- **Daily consumption**: ~3.6 Ah (at 5V)

### With Deep Sleep (New)
- Active measurement: ~150 mA for ~8 seconds
- Deep sleep: ~0.15 mA for ~22 seconds
- 30s interval → average ~45 mA
- **Daily consumption**: ~1.08 Ah (at 5V)
- **~70% power savings**

## Testing

### Test Timer Wake-up
1. Upload firmware
2. Monitor serial output
3. Observe automatic wake every 30 seconds
4. Check measurements and backend POSTs

### Test Button Wake-up
1. Press button connected to GPIO 7
2. Device wakes immediately
3. Runs measurement cycle
4. Serial monitor shows: "Button measurement complete, staying active for 30 seconds..."
5. Wait 30 seconds → device sleeps again

## Configuration

### Adjust Sleep Interval
```cpp
static const uint32_t SENSOR_SAMPLE_INTERVAL_MS = 60000; // Change to 60 seconds
```

### Adjust Button Active Time
```cpp
static const uint32_t BUTTON_ACTIVE_TIME_MS = 60000; // Stay active 60s after button wake
```

## Important Notes

1. **RTC Memory**: Deep sleep resets ESP32 (restarts from `setup()`), but wake-up reason is preserved
2. **WiFi Reconnection**: WiFi reconnects on every wake-up (adds ~2-5s to measurement cycle)
3. **Sensor Power**: MOSFET on GPIO 14 still controls sensor 5V rail power cycling
4. **Serial Monitor**: You may miss some serial output if not connected at wake-up time
5. **First Cycle**: First measurement after power-on is still warm-up only (no POST)

## Troubleshooting

### Device doesn't wake from sleep
- Check USB cable (some cables may not support deep sleep)
- Try programming via UART instead of USB-JTAG
- Ensure button is correctly connected to GPIO 7 and GND

### Button doesn't wake device
- Verify internal pull-up is enabled (`pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP)`)
- Check button connection (should short GPIO 7 to GND when pressed)
- Use multimeter to verify button functionality

### Serial monitor shows garbled text after wake
- Set baud rate to 115200
- Add small delay in `setup()` before serial prints
- Try different USB cable/port

## References
- [ESP32 Deep Sleep Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
- [ESP-IDF Sleep API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
