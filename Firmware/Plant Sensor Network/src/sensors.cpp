#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MS8607.h>
#include <Adafruit_Sensor.h>
#include <BH1750.h>
#include <DFRobot_Alcohol.h>
#include <DFRobot_MultiGasSensor.h>
#include <DFRobot_MHZ9041A.h>
#include <DFRobot_MLX90614.h>
#include <Adafruit_seesaw.h>
#include "DFRobotHCHOSensor.h"

// WiFi + HTTP JSON backend
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"   // defines WIFI_SSID, WIFI_PASSWORD, BACKEND_URL

// Deep sleep support for ESP32-S3
#include <esp_sleep.h>

// ESP32-S3 custom I2C pins
static const int SDA_PIN = 8;
static const int SCL_PIN = 9;

// MOSFET gate controlling 5V sensor power rail
// HIGH  = sensors powered
// LOW   = sensors off (power saving)
static const int SENSOR_MOSFET_PIN = 14;

// Wake-up button pin (press to wake from deep sleep and send data)
static const int WAKE_BUTTON_PIN = 7;

// Sampling / power-control timing (can be tuned)
// Time between the start of measurement cycles (sensor power ON events)
static const uint32_t SENSOR_SAMPLE_INTERVAL_MS = 30000; // 30 seconds
// Warm-up time after enabling 5V before reading sensors
static const uint32_t SENSOR_POWER_WARMUP_MS   = 3000;  // 5 seconds

// Deep sleep configuration
// Conversion: microseconds = milliseconds * 1000
static const uint64_t DEEP_SLEEP_INTERVAL_US = SENSOR_SAMPLE_INTERVAL_MS * 1000ULL;
// Time to stay awake after button wake (30 seconds)
static const uint32_t BUTTON_ACTIVE_TIME_MS = 30000;

// MultiGas read / offline behavior tuning
static const uint8_t  MULTIGAS_READ_MAX_RETRIES     = 3;    // retries within a single cycle
static const uint16_t MULTIGAS_READ_RETRY_DELAY_MS  = 100;  // delay between retries
static const uint8_t  SENSOR_MAX_FAIL_CYCLES        = 3;    // cycles before declaring sensor offline

// Mux addresses (TCA9548A / PCA9548A) typically 0x70-0x77
static const uint8_t POSSIBLE_MUX_ADDRS[] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77};
static const uint8_t MAX_MUXES = sizeof(POSSIBLE_MUX_ADDRS) / sizeof(POSSIBLE_MUX_ADDRS[0]);

// MS8607 is usually two I2C devices behind the module
static const uint8_t MS8607_HUM_ADDR  = 0x40;
static const uint8_t MS8607_PRES_ADDR = 0x76;

// BH1750 light sensor addresses (depends on ADDR pin)
static const uint8_t BH1750_ADDR_LOW  = 0x23;
static const uint8_t BH1750_ADDR_HIGH = 0x5C;

// DFRobot Alcohol (SEN0376) I2C addresses (set by onboard switches)
// From DFRobot docs: ADDRESS_0=0x72, ADDRESS_1=0x73, ADDRESS_2=0x74, ADDRESS_3=0x75
static const uint8_t ALCOHOL_ADDRS[] = {0x72, 0x73, 0x74, 0x75};
static const uint8_t ALCOHOL_ADDRS_LEN = sizeof(ALCOHOL_ADDRS) / sizeof(ALCOHOL_ADDRS[0]);

// DFRobot MultiGasSensor default I2C address is 0x74 (library default).
// It can also be configured, but we scan a small range to help locate it.
static const uint8_t MULTIGAS_ADDRS[] = {0x74, 0x75};
static const uint8_t MULTIGAS_ADDRS_LEN = sizeof(MULTIGAS_ADDRS) / sizeof(MULTIGAS_ADDRS[0]);

// MH-Z9041A CH4 sensor (DFRobot SEN0654)
// The DFRobot example uses I2C address 0x34, but the library header defaults to 0x75.
// To be robust, we scan both.
static const uint8_t CH4_ADDRS[] = {0x34, 0x75};
static const uint8_t CH4_ADDRS_LEN = sizeof(CH4_ADDRS) / sizeof(CH4_ADDRS[0]);
static const uint8_t CH4_PREFERRED_CH = 5; // your wiring

// MLX90614 IR thermometer (DFRobot SEN0206/SEN0263)
static const uint8_t MLX90614_DEFAULT_ADDR = 0x5A; // default I2C address

// Adafruit STEMMA Soil moisture sensor (Adafruit Seesaw)
static const uint8_t SOIL_CAP_I2C_ADDR = 0x36;     // default seesaw I2C address

static Adafruit_MS8607 ms8607;
static BH1750 bh1750;
// Pass explicit SDA/SCL pins so the MLX90614 library does not try to use
// an invalid default GPIO when toggling the I2C lines for wake-up.
static DFRobot_MLX90614_I2C mlx90614(MLX90614_DEFAULT_ADDR, &Wire, SDA_PIN, SCL_PIN);   // IR temperature sensor
static Adafruit_seesaw soilCapSeesaw;              // capacitive soil moisture (STEMMA Soil)
// Allocate after auto-detection so we can set the correct address
static DFRobot_Alcohol_I2C *alcohol = nullptr;

// MultiGas probes
// - H2S probe on mux_a (0x70)
// - O2, NH3, CO, O3 probes on mux_b (0x71)
static DFRobot_GAS_I2C *multigasH2S = nullptr;
static DFRobot_GAS_I2C *multigasO2 = nullptr;
static DFRobot_GAS_I2C *multigasNH3 = nullptr;
static DFRobot_GAS_I2C *multigasCO  = nullptr;
static DFRobot_GAS_I2C *multigasO3  = nullptr;

// MH-Z9041A CH4 - allocate after detection
static DFRobot_MHZ9041A_I2C *ch4 = nullptr;

// RS485 Soil EC + pH sensor (DFRobot SEN0603)
static const int RS485_RX_PIN = 19;
static const int RS485_TX_PIN = 20;
static HardwareSerial RS485Serial(2);

// Modbus-RTU query frame for this sensor (address 0x01, function 0x03,
// start register 0x0000, length 0x0004, CRC = 0x4409 (low, high)).
static uint8_t soilQueryFrame[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x04, 0x44, 0x09 };

static float soilEcMsPerCm = 0.0f;
static float soilPh = 0.0f;

// DFRobot HCHO sensor (UART, single "S" pin)
static const int HCHO_RX_PIN = 5;   // GPIO receiving data from sensor S
static HardwareSerial HCHO_Serial(1);
static DFRobotHCHOSensor hcho(&HCHO_Serial);
static float hchoPpm = 0.0f;

// --- WiFi helpers ----------------------------------------------------------
static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("Connecting to WiFi"));
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 30000UL) {
    Serial.print('.');
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi connected, IP="));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WiFi connect FAILED (continuing, but no backend POSTs)."));
  }
}

static bool i2cPing(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

// Forward declarations (used by the presence-check helpers below)
static void muxDisableAll(uint8_t muxAddr);
static void muxSelectChannel(uint8_t muxAddr, uint8_t channel);
static void enterDeepSleep();

static bool ms8607Present(uint8_t muxAddr, uint8_t ch) {
  muxSelectChannel(muxAddr, ch);
  delay(3);
  return i2cPing(MS8607_HUM_ADDR) && i2cPing(MS8607_PRES_ADDR);
}

static bool bh1750Present(uint8_t muxAddr, uint8_t ch, uint8_t bhAddr) {
  muxSelectChannel(muxAddr, ch);
  delay(3);
  return i2cPing(bhAddr);
}

static bool alcoholPresent(uint8_t muxAddr, uint8_t ch, uint8_t addr) {
  muxSelectChannel(muxAddr, ch);
  delay(3);
  return i2cPing(addr);
}

static bool soilCapPresent(uint8_t muxAddr, uint8_t ch, uint8_t addr) {
  muxSelectChannel(muxAddr, ch);
  delay(3);
  return i2cPing(addr);
}

static bool mlx90614Present(uint8_t muxAddr, uint8_t ch) {
  muxSelectChannel(muxAddr, ch);
  delay(3);
  return i2cPing(MLX90614_DEFAULT_ADDR);
}

static bool multigasPresent(uint8_t muxAddr, uint8_t ch, uint8_t addr) {
  muxSelectChannel(muxAddr, ch);
  delay(3);
  return i2cPing(addr);
}

static bool ch4Present(uint8_t muxAddr, uint8_t ch, uint8_t ch4Addr) {
  muxSelectChannel(muxAddr, ch);
  delay(3);
  return i2cPing(ch4Addr);
}

static void muxDisableAll(uint8_t muxAddr) {
  Wire.beginTransmission(muxAddr);
  Wire.write(0x00);
  Wire.endTransmission();
}

static void muxSelectChannel(uint8_t muxAddr, uint8_t channel) {
  if (channel > 7) return;

  // IMPORTANT: Disable all channels on BOTH muxes before enabling one channel.
  // Otherwise, two downstream busses can be connected at the same time and devices
  // with the same I2C address (like MultiGas at 0x74) will collide and cause bad reads.
  muxDisableAll(0x70);
  muxDisableAll(0x71);

  Wire.beginTransmission(muxAddr);
  Wire.write(1 << channel);
  Wire.endTransmission();
}


static bool findMs8607Location(uint8_t *outMuxAddr, uint8_t *outChannel) {
  for (uint8_t i = 0; i < MAX_MUXES; i++) {
    const uint8_t muxAddr = POSSIBLE_MUX_ADDRS[i];
    if (!i2cPing(muxAddr)) {
      continue;
    }

    muxDisableAll(muxAddr);
    delay(2);

    for (uint8_t ch = 0; ch < 8; ch++) {
      muxSelectChannel(muxAddr, ch);
      delay(3);

      // MS8607 module normally exposes both 0x40 and 0x76
      const bool humOk  = i2cPing(MS8607_HUM_ADDR);
      const bool presOk = i2cPing(MS8607_PRES_ADDR);

      if (humOk && presOk) {
        *outMuxAddr = muxAddr;
        *outChannel = ch;
        muxDisableAll(muxAddr);
        return true;
      }
    }

    muxDisableAll(muxAddr);
  }

  return false;
}

static bool findBh1750Location(uint8_t *outMuxAddr, uint8_t *outChannel, uint8_t *outBhAddr) {
  for (uint8_t i = 0; i < MAX_MUXES; i++) {
    const uint8_t muxAddr = POSSIBLE_MUX_ADDRS[i];
    if (!i2cPing(muxAddr)) {
      continue;
    }

    muxDisableAll(muxAddr);
    delay(2);

    for (uint8_t ch = 0; ch < 8; ch++) {
      muxSelectChannel(muxAddr, ch);
      delay(3);

      // BH1750 usually responds on 0x23 or 0x5C
      if (i2cPing(BH1750_ADDR_LOW)) {
        *outMuxAddr = muxAddr;
        *outChannel = ch;
        *outBhAddr  = BH1750_ADDR_LOW;
        muxDisableAll(muxAddr);
        return true;
      }

      if (i2cPing(BH1750_ADDR_HIGH)) {
        *outMuxAddr = muxAddr;
        *outChannel = ch;
        *outBhAddr  = BH1750_ADDR_HIGH;
        muxDisableAll(muxAddr);
        return true;
      }
    }

    muxDisableAll(muxAddr);
  }

  return false;
}

static bool findAlcoholLocation(uint8_t *outMuxAddr, uint8_t *outChannel, uint8_t *outAlcoholAddr) {
  // Your wiring: Alcohol sensor is on channel 7.
  // We search ch7 first to avoid confusing it with other sensors that may share addresses on other channels.
  static const uint8_t PREFERRED_CH = 7;

  for (uint8_t i = 0; i < MAX_MUXES; i++) {
    const uint8_t muxAddr = POSSIBLE_MUX_ADDRS[i];
    if (!i2cPing(muxAddr)) {
      continue;
    }

    // 1) Try preferred channel first
    muxDisableAll(muxAddr);
    delay(2);
    muxSelectChannel(muxAddr, PREFERRED_CH);
    delay(3);

    for (uint8_t ai = 0; ai < ALCOHOL_ADDRS_LEN; ai++) {
      uint8_t addr = ALCOHOL_ADDRS[ai];
      if (i2cPing(addr)) {
        *outMuxAddr = muxAddr;
        *outChannel = PREFERRED_CH;
        *outAlcoholAddr = addr;
        muxDisableAll(muxAddr);
        return true;
      }
    }

    // 2) Fallback: scan all channels
    for (uint8_t ch = 0; ch < 8; ch++) {
      if (ch == PREFERRED_CH) continue;
      muxSelectChannel(muxAddr, ch);
      delay(3);

      for (uint8_t ai = 0; ai < ALCOHOL_ADDRS_LEN; ai++) {
        uint8_t addr = ALCOHOL_ADDRS[ai];
        if (i2cPing(addr)) {
          *outMuxAddr = muxAddr;
          *outChannel = ch;
          *outAlcoholAddr = addr;
          muxDisableAll(muxAddr);
          return true;
        }
      }
    }

    muxDisableAll(muxAddr);
  }

  return false;
}

static bool findCh4Location(uint8_t *outMuxAddr, uint8_t *outChannel, uint8_t *outCh4Addr) {
  // CH4 sensor is on channel 5 (your wiring). Try ch5 first.
  for (uint8_t i = 0; i < MAX_MUXES; i++) {
    const uint8_t muxAddr = POSSIBLE_MUX_ADDRS[i];
    if (!i2cPing(muxAddr)) continue;

    muxDisableAll(muxAddr);
    delay(2);

    // 1) preferred channel
    muxSelectChannel(muxAddr, CH4_PREFERRED_CH);
    delay(3);
    for (uint8_t ai = 0; ai < CH4_ADDRS_LEN; ai++) {
      const uint8_t addr = CH4_ADDRS[ai];
      if (i2cPing(addr)) {
        *outMuxAddr = muxAddr;
        *outChannel = CH4_PREFERRED_CH;
        *outCh4Addr = addr;
        muxDisableAll(muxAddr);
        return true;
      }
    }

    // 2) fallback scan
    for (uint8_t ch = 0; ch < 8; ch++) {
      if (ch == CH4_PREFERRED_CH) continue;
      muxSelectChannel(muxAddr, ch);
      delay(3);
      for (uint8_t ai = 0; ai < CH4_ADDRS_LEN; ai++) {
        const uint8_t addr = CH4_ADDRS[ai];
        if (i2cPing(addr)) {
          *outMuxAddr = muxAddr;
          *outChannel = ch;
          *outCh4Addr = addr;
          muxDisableAll(muxAddr);
          return true;
        }
      }
    }

    muxDisableAll(muxAddr);
  }
  return false;
}

static bool findMultiGasOnMuxForType(uint8_t requiredMuxAddr,
                                      uint8_t preferredChannel,
                                      const char *desiredType,
                                      uint8_t *outChannel,
                                      uint8_t *outAddr,
                                      String *outType) {
  if (!i2cPing(requiredMuxAddr)) {
    return false;
  }

  muxDisableAll(requiredMuxAddr);
  delay(2);

  auto tryChannel = [&](uint8_t ch) -> bool {
    muxSelectChannel(requiredMuxAddr, ch);
    delay(5);

    for (uint8_t ai = 0; ai < MULTIGAS_ADDRS_LEN; ai++) {
      const uint8_t addr = MULTIGAS_ADDRS[ai];
      if (!i2cPing(addr)) continue;

      DFRobot_GAS_I2C candidate(&Wire, addr);
      if (!candidate.begin()) continue;

      String t = candidate.queryGasType();
      if (t.length() == 0) continue;

      // If a specific type is desired, enforce it
      if (desiredType != nullptr && t != desiredType) {
        continue;
      }

      *outChannel = ch;
      *outAddr = addr;
      *outType = t;
      return true;
    }

    return false;
  };

  // 1) Try preferred channel first
  if (preferredChannel <= 7 && tryChannel(preferredChannel)) {
    muxDisableAll(requiredMuxAddr);
    return true;
  }

  // 2) Fallback scan
  for (uint8_t ch = 0; ch < 8; ch++) {
    if (ch == preferredChannel) continue;
    if (tryChannel(ch)) {
      muxDisableAll(requiredMuxAddr);
      return true;
    }
  }

  muxDisableAll(requiredMuxAddr);
  return false;
}

static bool findMlx90614Location(uint8_t requiredMuxAddr,
                                 uint8_t *outChannel) {
  if (!i2cPing(requiredMuxAddr)) {
    return false;
  }

  muxDisableAll(requiredMuxAddr);
  delay(2);

  for (uint8_t ch = 0; ch < 8; ch++) {
    muxSelectChannel(requiredMuxAddr, ch);
    delay(3);
    if (mlx90614Present(requiredMuxAddr, ch)) {
      *outChannel = ch;
      muxDisableAll(requiredMuxAddr);
      return true;
    }
  }

  muxDisableAll(requiredMuxAddr);
  return false;
}

static bool findSoilCapSeesawLocation(uint8_t requiredMuxAddr,
                                      uint8_t preferredChannel,
                                      uint8_t *outChannel,
                                      uint8_t *outAddr) {
  if (!i2cPing(requiredMuxAddr)) {
    return false;
  }

  auto tryChannel = [&](uint8_t ch) -> bool {
    if (ch > 7) return false;
    muxSelectChannel(requiredMuxAddr, ch);
    delay(3);
    if (!soilCapPresent(requiredMuxAddr, ch, SOIL_CAP_I2C_ADDR)) {
      return false;
    }
    *outChannel = ch;
    *outAddr    = SOIL_CAP_I2C_ADDR;
    return true;
  };

  muxDisableAll(requiredMuxAddr);
  delay(2);

  // 1) Try preferred channel first if provided
  if (preferredChannel <= 7 && tryChannel(preferredChannel)) {
    muxDisableAll(requiredMuxAddr);
    return true;
  }

  // 2) Fallback scan across channels
  for (uint8_t ch = 0; ch < 8; ch++) {
    if (ch == preferredChannel) continue;
    if (tryChannel(ch)) {
      muxDisableAll(requiredMuxAddr);
      return true;
    }
  }

  muxDisableAll(requiredMuxAddr);
  return false;
}

static const char *muxLabelFromAddr(uint8_t muxAddr) {
  // Your setup uses mux A=0x70 and mux B=0x71.
  if (muxAddr == 0x70) return "MUX_A";
  if (muxAddr == 0x71) return "MUX_B";
  return "MUX_?";
}

static void chLabel(uint8_t channel, char *out, size_t outLen) {
  // Use uppercase channel labels like CH0, CH1 for professional output.
  snprintf(out, outLen, "CH%u", channel);
}

static void sanitizeGasType(const String &in, char *out, size_t outLen) {
  // Convert spaces to '_' to keep one "word".
  size_t j = 0;
  for (size_t i = 0; i < in.length() && j + 1 < outLen; i++) {
    char c = in[i];
    out[j++] = (c == ' ') ? '_' : c;
  }
  out[j] = '\0';
}

static void printHeaderOnce() {
  Serial.println();
  Serial.println(F("Sensor             Mux    Ch   Values"));
  Serial.println(F("--------------------------------------------------------------"));
}

static void printStatusLine(const char *sensor, uint8_t muxAddr, uint8_t channel, const char *status) {
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s %s\n", sensor, muxLabelFromAddr(muxAddr), chBuf, status);
}

static void printMs8607Line(uint8_t muxAddr, uint8_t channel, float tempC, float rh, float hpa) {
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s T=%6.2fC  RH=%6.2f%%  P=%7.2fhPa\n", "MS8607", muxLabelFromAddr(muxAddr), chBuf, tempC, rh, hpa);
}

static void printBh1750Line(uint8_t muxAddr, uint8_t channel, float lux) {
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s Lux=%7.2flx\n", "BH1750", muxLabelFromAddr(muxAddr), chBuf, lux);
}

static void printAlcoholLine(uint8_t muxAddr, uint8_t channel, float ppm) {
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s Alcohol=%6.2fppm\n", "ALCOHOL", muxLabelFromAddr(muxAddr), chBuf, ppm);
}

static void printCh4Line(uint8_t muxAddr, uint8_t channel, float ch4Lel, float moduleTempC, int err) {
  // NOTE: getTemperature() from MH-Z9041A is the module-reported temperature.
  // The sensor contains temperature control/compensation, so it can be higher than ambient.
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s CH4=%6.2f%%LEL  Tmod=%5.1fC  Err=%d\n",
                "CH4(MHZ9041A)",
                muxLabelFromAddr(muxAddr),
                chBuf,
                ch4Lel,
                moduleTempC,
                err);
}

static void printMlx90614Line(uint8_t muxAddr, uint8_t channel, float /*ambC*/, float objC) {
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));
  // Only show object temperature; ambient comes from MS8607.
  Serial.printf("%-18s %-6s %-4s Tobj=%6.2fC\n",
                "MLX90614",
                muxLabelFromAddr(muxAddr),
                chBuf,
                objC);
}

static void printMultiGasLine(uint8_t muxAddr, uint8_t channel, const String &gasType, float value, const char *unit) {
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));

  char gt[16];
  sanitizeGasType(gasType, gt, sizeof(gt));

  char sensorName[32];
  snprintf(sensorName, sizeof(sensorName), "MULTIGAS(%s)", (gt[0] == '\0') ? "?" : gt);

  Serial.printf("%-18s %-6s %-4s %s=%6.2f%s\n",
                sensorName,
                muxLabelFromAddr(muxAddr),
                chBuf,
                (gt[0] == '\0') ? "gas" : gt,
                value,
                unit);
}

static void printSoilStatusLine(const char *status) {
  char chBuf[6];
  chLabel(0, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s %s\n",
                "SOIL_EC_PH",
                "uart2",
                chBuf,
                status);
}

static void printSoilECPHLine(float ec, float ph) {
  char chBuf[6];
  chLabel(0, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s EC=%6.2fmS/cm  pH=%4.1f\n",
                "SOIL_EC_PH",
                "uart2",
                chBuf,
                ec,
                ph);
}

static void printHchoStatusLine(const char *status) {
  char chBuf[6];
  chLabel(0, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s %s\n",
                "HCHO_UART",
                "uart1",
                chBuf,
                status);
}

static void printHchoLine(float ppm) {
  char chBuf[6];
  chLabel(0, chBuf, sizeof(chBuf));
  Serial.printf("%-18s %-6s %-4s HCHO=%6.3fppm\n",
                "HCHO_UART",
                "uart1",
                chBuf,
                ppm);
}

static void printSoilCapSeesawLine(uint8_t muxAddr, uint8_t channel, uint16_t moist) {
  char chBuf[6];
  chLabel(channel, chBuf, sizeof(chBuf));
  // Only show moisture; module temperature is not needed on serial.
  Serial.printf("%-18s %-6s %-4s Moist=%5u\n",
                "SOIL_CAP_I2C",
                muxLabelFromAddr(muxAddr),
                chBuf,
                moist);
}

// --- Simple per-cycle sensor status tracking ---
static uint8_t g_sensorTotalCount = 0;
static uint8_t g_sensorOfflineCount = 0;
static String  g_sensorOfflineList;

// Global JSON document for backend payload. Using a global avoids large
// allocations on the loopTask stack (which caused stack overflows when the
// document size was increased).
static StaticJsonDocument<4096> doc;

static void resetSensorCounters() {
  g_sensorTotalCount = 0;
  g_sensorOfflineCount = 0;
  g_sensorOfflineList = "";
}

static void trackSensorStatus(const char *sensorName, bool online) {
  g_sensorTotalCount++;
  if (!online) {
    if (g_sensorOfflineList.length() > 0) {
      g_sensorOfflineList += ", ";
    }
    g_sensorOfflineList += sensorName;
    g_sensorOfflineCount++;
  }
}

static void printSensorSummary() {
  Serial.println();
  Serial.print(F("=== Sensor Summary === total="));
  Serial.print(g_sensorTotalCount);
  Serial.print(F(", offline="));
  Serial.println(g_sensorOfflineCount);

  if (g_sensorOfflineCount > 0) {
    Serial.print(F("Offline: "));
    Serial.println(g_sensorOfflineList);
  }
}

// Helper to get/create the shared MULTIGAS JSON object without overwriting
// previous gas entries. Using .to<JsonObject>() repeatedly would replace the
// existing object each time; this helper ensures we re-use it.
static JsonObject getMultigasObject() {
  JsonVariant v = doc["MULTIGAS"];
  if (v.isNull()) {
    return doc.createNestedObject("MULTIGAS");
  }
  return v.as<JsonObject>();
}

// Helper: robust MultiGas read with a few quick retries inside one cycle.
// This avoids flagging the sensor offline immediately if it is still
// stabilising after power-on.
static bool readMultiGasWithRetries(DFRobot_GAS_I2C *sensor,
                                    uint8_t muxAddr,
                                    uint8_t channel,
                                    uint8_t addr,
                                    String &gasTypeOut,
                                    float &concOut) {
  if (sensor == nullptr) {
    return false;
  }

  bool success = false;
  for (uint8_t attempt = 0; attempt < MULTIGAS_READ_MAX_RETRIES; ++attempt) {
    muxSelectChannel(muxAddr, channel);
    delay(5);

    // If the device does not ACK its address yet, it is probably still
    // powering up or absent. Try again a few times before giving up.
    if (!i2cPing(addr)) {
      delay(MULTIGAS_READ_RETRY_DELAY_MS);
      continue;
    }

    String t = sensor->queryGasType();
    float conc = sensor->readGasConcentrationPPM();

    if (t.length() == 0) {
      delay(MULTIGAS_READ_RETRY_DELAY_MS);
      continue;
    }

    gasTypeOut = t;
    concOut    = conc;
    success    = true;
    break;
  }

  return success;
}

static unsigned int soilCRC16(unsigned char *buf, int len) {
  unsigned int crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (unsigned int)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  // Swap bytes (high/low)
  crc = ((crc & 0x00FF) << 8) | ((crc & 0xFF00) >> 8);
  return crc;
}

static bool readSoilSensorOnce(float &ec, float &ph) {
  uint8_t response[13] = {0};

  // Flush any old data from the RX buffer
  while (RS485Serial.available()) {
    RS485Serial.read();
  }

  // Send query frame
  RS485Serial.write(soilQueryFrame, sizeof(soilQueryFrame));
  RS485Serial.flush();

  // Give the sensor time to respond
  delay(250);

  size_t n = RS485Serial.readBytes(response, sizeof(response));
  if (n != sizeof(response)) {
    return false;
  }

  // Basic header checks: address=0x01, func=0x03, byte count=0x08
  if (response[0] != 0x01 || response[1] != 0x03 || response[2] != 0x08) {
    return false;
  }

  // Verify CRC on first 11 bytes
  unsigned int crcCalc = soilCRC16(response, 11);
  unsigned int crcFrame = (response[11] << 8) | response[12];
  if (crcCalc != crcFrame) {
    return false;
  }

  // Parse values:
  //   reg0, reg1: unused (always 0)
  //   reg2: EC (µS/cm)
  //   reg3: pH * 10
  uint16_t ecRaw = (uint16_t)response[7]  << 8 | response[8];
  uint16_t phRaw = (uint16_t)response[9]  << 8 | response[10];

  ec = ecRaw / 1000.0f;  // µS/cm -> mS/cm
  ph = phRaw / 10.0f;

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=== Sensors (start: MS8607 via I2C mux, WiFi backend) ==="));

  // Check wake-up reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  // Configure wake-up button (GPIO 7) with internal pull-up
  // Button should connect GPIO 7 to GND when pressed
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  delay(50);  // Allow pin to stabilize
  
  // Verify button state if wake reason was EXT0
  // If GPIO 7 is HIGH now, it was a false trigger (floating pin or noise)
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    if (digitalRead(WAKE_BUTTON_PIN) == HIGH) {
      // Pin is HIGH (not pressed), this was a false wake-up
      Serial.println(F("Wake-up: FALSE button trigger detected (treating as TIMER)"));
      wakeup_reason = ESP_SLEEP_WAKEUP_TIMER;  // Treat as timer wake
    } else {
      Serial.println(F("Wake-up: BUTTON pressed on GPIO 7"));
    }
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println(F("Wake-up: TIMER (scheduled measurement)"));
  } else {
    Serial.println(F("Wake-up: POWER-ON or RESET"));
  }

  // Configure MOSFET gate that controls the 5V sensor rail.
  pinMode(SENSOR_MOSFET_PIN, OUTPUT);
  digitalWrite(SENSOR_MOSFET_PIN, LOW);  // start with sensors powered off

  // Connect to WiFi so we can POST JSON to the backend from loop().
  connectWiFi();

  Wire.begin(SDA_PIN, SCL_PIN);

  // RS485 soil EC+pH sensor UART
  RS485Serial.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  RS485Serial.setTimeout(500);

  // DFRobot HCHO UART sensor (single S pin)
  HCHO_Serial.begin(9600, SERIAL_8N1, HCHO_RX_PIN, -1);

  printHeaderOnce();
}

void loop() {
  // Track measurement cycle: 0 = not started, 1 = first cycle done, 2 = second cycle done
  static uint8_t measurementCycle = 0;
  static uint32_t lastCycleMs = 0;
  
  // Check wake-up reason to determine behavior
  static esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  static uint32_t buttonWakeStartMs = 0;
  static bool isButtonActiveMode = false;
  static bool buttonMeasurementDone = false;
  static bool buttonMessagePrinted = false;  // Prevent repeated message

  // If woken by button and haven't done measurement yet, proceed with measurement
  // After measurement completes, enter "active mode" for 30 seconds
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 && !buttonMeasurementDone && !buttonMessagePrinted) {
    Serial.println(F("Button wake: running measurement cycles"));
    buttonMessagePrinted = true;
    // Fall through to run measurement cycle below
  }

  // In button active mode (after measurement), check if 30 seconds have elapsed
  if (isButtonActiveMode) {
    if (millis() - buttonWakeStartMs >= BUTTON_ACTIVE_TIME_MS) {
      Serial.println(F("Button active period ended, entering deep sleep"));
      // Reset flags for next wake
      isButtonActiveMode = false;
      buttonMeasurementDone = false;
      measurementCycle = 0;  // Reset cycle counter
      enterDeepSleep();
      return;  // Never reached, but explicit
    }
    // Stay awake, delay and loop
    delay(100);
    return;
  }
  
  // Check if we need to wait between cycles (for double measurement)
  if (measurementCycle == 1) {
    // Wait 2 seconds between first and second cycle
    if (millis() - lastCycleMs < 2000) {
      delay(100);
      return;
    }
    // Ready for second cycle
    Serial.println();
    Serial.println(F("========== SECOND CYCLE (POSTING TO BACKEND) =========="));
  }

  // Indicate which cycle this is
  if (measurementCycle == 0) {
    Serial.println();
    Serial.println(F("========== FIRST CYCLE (STATUS CHECK ONLY) =========="));
  }
  
  // Power ON all sensors via MOSFET gate and allow them to stabilise.
  digitalWrite(SENSOR_MOSFET_PIN, HIGH);
  delay(SENSOR_POWER_WARMUP_MS);

  Serial.println();
  Serial.println(F("--- cycle ---"));

  // Reset per-cycle sensor counters
  resetSensorCounters();

  // JSON payload for backend ingest
  // Use global StaticJsonDocument to keep it off the stack. Clear it at the
  // start of each cycle.
  doc.clear();
  doc["ts_ms"] = millis();

  static bool msInitialized = false;
  static bool bhInitialized = false;
  static bool alcoholInitialized = false;
  static bool multigasH2SInitialized = false;
  static bool multigasO2Initialized = false;
  static bool multigasNH3Initialized = false;
  static bool multigasCOInitialized  = false;
  static bool multigasO3Initialized  = false;
  static bool ch4Initialized = false;
  static bool mlxInitialized = false;
  static bool soilCapInitialized = false;
  static bool soilInitialized = false;
  static bool hchoInitialized = false;

  // Because the 5V sensor rail is power-cycled each measurement cycle via
  // SENSOR_MOSFET_PIN, force re-initialisation of all sensors on each cycle
  // after power-on. This makes sure I2C/UART configuration is restored after
  // the modules have lost power.
  msInitialized = false;
  bhInitialized = false;
  alcoholInitialized = false;
  multigasH2SInitialized = false;
  multigasO2Initialized = false;
  multigasNH3Initialized = false;
  multigasCOInitialized  = false;
  multigasO3Initialized  = false;
  ch4Initialized = false;
  mlxInitialized = false;
  soilCapInitialized = false;
  soilInitialized = false;
  hchoInitialized = false;

  // Track whether a sensor was ever connected before (to distinguish "not_found" vs "not_connected")
  static bool msEverConnected = false;
  static bool bhEverConnected = false;
  static bool alcoholEverConnected = false;
  static bool multigasH2SEverConnected = false;
  static bool multigasO2EverConnected = false;
  static bool multigasNH3EverConnected = false;
  static bool multigasCOEverConnected  = false;
  static bool multigasO3EverConnected  = false;
  static bool ch4EverConnected = false;
  static bool mlxEverConnected = false;
  static bool soilCapEverConnected = false;
  static bool soilEverConnected = false;
  static bool hchoEverConnected = false;

  // Track consecutive read failures per MultiGas sensor. We only mark the
  // sensor truly offline if it fails SENSOR_MAX_FAIL_CYCLES times in a row.
  static uint8_t multigasH2SFailCycles = 0;
  static uint8_t multigasO2FailCycles  = 0;
  static uint8_t multigasNH3FailCycles = 0;
  static uint8_t multigasCOFailCycles  = 0;
  static uint8_t multigasO3FailCycles  = 0;

  static uint8_t msMuxAddr = 0;
  static uint8_t msMuxChannel = 0;

  static uint8_t bhMuxAddr = 0;
  static uint8_t bhMuxChannel = 0;
  static uint8_t bhAddr = 0;

  static uint8_t alcoholMuxAddr = 0;
  static uint8_t alcoholMuxChannel = 0;
  static uint8_t alcoholAddr = 0;

  // MultiGas H2S on mux_a (0x70), preferred ch6
  static uint8_t gasH2SMuxAddr = 0x70;
  static uint8_t gasH2SChannel = 6;
  static uint8_t gasH2SAddr = 0;

  // MultiGas gases on mux_b (0x71)
  // From your wiring / scan:
  //   ch4 -> O2, ch5 -> NH3, ch6 -> CO, ch7 -> O3 (all at 0x74)
  static uint8_t gasO2MuxAddr  = 0x71;
  static uint8_t gasO2Channel  = 4;
  static uint8_t gasO2Addr     = 0;

  static uint8_t gasNH3MuxAddr = 0x71;
  static uint8_t gasNH3Channel = 5;
  static uint8_t gasNH3Addr    = 0;

  static uint8_t gasCOMuxAddr  = 0x71;
  static uint8_t gasCOChannel  = 6;
  static uint8_t gasCOAddr     = 0;

  static uint8_t gasO3MuxAddr  = 0x71;
  static uint8_t gasO3Channel  = 7;
  static uint8_t gasO3Addr     = 0;

  static uint8_t ch4MuxAddr = 0;
  static uint8_t ch4MuxChannel = 0;
  static uint8_t ch4Addr = 0;

  // MLX90614 IR thermometer on mux_b (0x71), auto-detected channel
  static uint8_t mlxMuxAddr   = 0x71;
  static uint8_t mlxChannel   = 0;

  // STEMMA Soil capacitive moisture sensor (Adafruit Seesaw) on mux_b (0x71)
  static uint8_t soilCapMuxAddr   = 0x71;
  static uint8_t soilCapChannel   = 1;   // preferred channel, auto-adjusted after scan
  static uint8_t soilCapAddr      = SOIL_CAP_I2C_ADDR;

  // Measurement cycle tracking is now handled by measurementCycle variable
  // - Cycle 0: First measurement (status check only)
  // - Cycle 1: Second measurement (POST to backend)

  // ---- Init MS8607 once (auto-detect) ----
  if (!msInitialized) {
    if (!findMs8607Location(&msMuxAddr, &msMuxChannel)) {
      printStatusLine("MS8607", 0x00, 255, msEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("MS8607", false);
    } else {
      muxSelectChannel(msMuxAddr, msMuxChannel);
      delay(5);

      if (!ms8607.begin()) {
        printStatusLine("MS8607", msMuxAddr, msMuxChannel, "begin_failed");
        trackSensorStatus("MS8607", false);
      } else {
        msInitialized = true;
        msEverConnected = true;
      }
    }
  }

  // ---- Init BH1750 once (auto-detect) ----
  if (!bhInitialized) {
    if (!findBh1750Location(&bhMuxAddr, &bhMuxChannel, &bhAddr)) {
      printStatusLine("BH1750", 0x00, 255, bhEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("BH1750", false);
    } else {
      muxSelectChannel(bhMuxAddr, bhMuxChannel);
      delay(5);

      // Use continuous high-res mode for stable readings
      if (!bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, bhAddr, &Wire)) {
        printStatusLine("BH1750", bhMuxAddr, bhMuxChannel, "begin_failed");
        trackSensorStatus("BH1750", false);
      } else {
        bhInitialized = true;
        bhEverConnected = true;
      }
    }
  }

  // ---- Init Alcohol sensor once (auto-detect) ----
  if (!alcoholInitialized) {
    if (!findAlcoholLocation(&alcoholMuxAddr, &alcoholMuxChannel, &alcoholAddr)) {
      printStatusLine("ALCOHOL", 0x00, 255, alcoholEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("ALCOHOL", false);
    } else {
      muxSelectChannel(alcoholMuxAddr, alcoholMuxChannel);
      delay(5);

      // Recreate driver if address changed
      if (alcohol != nullptr) {
        delete alcohol;
        alcohol = nullptr;
      }
      alcohol = new DFRobot_Alcohol_I2C(&Wire, alcoholAddr);

      // Active measurement mode
      alcohol->setModes(MEASURE_MODE_AUTOMATIC);
      alcoholInitialized = true;
      alcoholEverConnected = true;
    }
  }

  // ---- Init CH4 sensor once (auto-detect) ----
  if (!ch4Initialized) {
    if (!findCh4Location(&ch4MuxAddr, &ch4MuxChannel, &ch4Addr)) {
      printStatusLine("CH4", 0x00, 255, ch4EverConnected ? "not_connected" : "not_found");
      trackSensorStatus("CH4", false);
    } else {
      muxSelectChannel(ch4MuxAddr, ch4MuxChannel);
      delay(5);

      // Recreate object if address changes
      if (ch4 != nullptr) {
        delete ch4;
        ch4 = nullptr;
      }
      ch4 = new DFRobot_MHZ9041A_I2C(&Wire, ch4Addr);

      if (!ch4->begin()) {
        printStatusLine("CH4", ch4MuxAddr, ch4MuxChannel, "begin_failed");
        trackSensorStatus("CH4", false);
      } else {
        ch4->setMode(ePassivityMode);
        ch4Initialized = true;
        ch4EverConnected = true;
      }
    }
  }

  // ---- Init MLX90614 IR thermometer (mux_b, 0x71) ----
  if (!mlxInitialized) {
    uint8_t ch = 0;

    if (!findMlx90614Location(mlxMuxAddr, &ch)) {
      printStatusLine("MLX90614", mlxMuxAddr, 255, mlxEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("MLX90614", false);
    } else {
      mlxChannel = ch;
      muxSelectChannel(mlxMuxAddr, mlxChannel);
      delay(5);

      int status = mlx90614.begin();
      if (status != 0) {
        printStatusLine("MLX90614", mlxMuxAddr, mlxChannel, "begin_failed");
        trackSensorStatus("MLX90614", false);
      } else {
        mlxInitialized = true;
        mlxEverConnected = true;
      }
    }
  }

  // ---- Init STEMMA Soil capacitive sensor (Adafruit Seesaw) ----
  if (!soilCapInitialized) {
    uint8_t ch = soilCapChannel;
    uint8_t addr = soilCapAddr;

    if (!findSoilCapSeesawLocation(soilCapMuxAddr, ch, &ch, &addr)) {
      printStatusLine("SOIL_CAP_I2C", soilCapMuxAddr, 255, soilCapEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("SOIL_CAP_I2C", false);
    } else {
      soilCapChannel = ch;
      soilCapAddr    = addr;

      muxSelectChannel(soilCapMuxAddr, soilCapChannel);
      delay(5);

      if (!soilCapSeesaw.begin(soilCapAddr)) {
        printStatusLine("SOIL_CAP_I2C", soilCapMuxAddr, soilCapChannel, "begin_failed");
        trackSensorStatus("SOIL_CAP_I2C", false);
      } else {
        soilCapInitialized = true;
        soilCapEverConnected = true;
      }
    }
  }

  // ---- Init RS485 Soil EC+pH sensor (DFRobot SEN0603) ----
  if (!soilInitialized) {
    float ecTest = 0.0f;
    float phTest = 0.0f;
    if (!readSoilSensorOnce(ecTest, phTest)) {
      printSoilStatusLine(soilEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("SOIL_EC_PH", false);
    } else {
      soilEcMsPerCm = ecTest;
      soilPh = phTest;
      soilInitialized = true;
      soilEverConnected = true;
    }
  }

  // ---- Init HCHO UART sensor ----
  if (!hchoInitialized) {
    bool haveFrame = false;
    // The DFRobot library's available() is designed to be called often;
    // here we call it in a small loop so we actually consume the pending
    // UART bytes and allow its sliding window to find a valid frame.
    for (int i = 0; i < 64; ++i) {
      if (hcho.available()) {
        haveFrame = true;
        break;
      }
      if (HCHO_Serial.available() == 0) {
        break;
      }
    }

    if (!haveFrame) {
      printHchoStatusLine(hchoEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("HCHO_UART", false);
    } else {
      hchoPpm = hcho.uartReadPPM();
      hchoInitialized = true;
      hchoEverConnected = true;
    }
  }

  // ---- Init MultiGas(H2S) on mux_a (0x70) ----
  if (!multigasH2SInitialized) {
    uint8_t ch = 0;
    uint8_t addr = 0;
    String t = "";

    if (!findMultiGasOnMuxForType(gasH2SMuxAddr, 6, "H2S", &ch, &addr, &t)) {
      printStatusLine("MULTIGAS(H2S)", 0x70, 6, multigasH2SEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("MULTIGAS(H2S)", false);
    } else {
      gasH2SChannel = ch;
      gasH2SAddr = addr;

      muxSelectChannel(gasH2SMuxAddr, gasH2SChannel);
      delay(5);

      if (multigasH2S == nullptr) {
        multigasH2S = new DFRobot_GAS_I2C(&Wire, gasH2SAddr);
      } else {
        multigasH2S->setI2cAddr(gasH2SAddr);
      }

      if (!multigasH2S->begin()) {
        printStatusLine("MULTIGAS(H2S)", gasH2SMuxAddr, gasH2SChannel, "begin_failed");
        trackSensorStatus("MULTIGAS(H2S)", false);
      } else {
        multigasH2S->changeAcquireMode(DFRobot_GAS::PASSIVITY);
        multigasH2S->setTempCompensation(DFRobot_GAS::ON);
        multigasH2SInitialized = true;
        multigasH2SEverConnected = true;
      }
    }
  }

  // ---- Init MultiGas(O2) on mux_b (0x71), channel 4 ----
  if (!multigasO2Initialized) {
    uint8_t ch = 0;
    uint8_t addr = 0;
    String t = "";

    if (!findMultiGasOnMuxForType(gasO2MuxAddr, gasO2Channel, "O2", &ch, &addr, &t)) {
      printStatusLine("MULTIGAS(O2)", gasO2MuxAddr, gasO2Channel, multigasO2EverConnected ? "not_connected" : "not_found");
      trackSensorStatus("MULTIGAS(O2)", false);
    } else {
      gasO2Channel = ch;
      gasO2Addr = addr;

      muxSelectChannel(gasO2MuxAddr, gasO2Channel);
      delay(5);

      if (multigasO2 == nullptr) {
        multigasO2 = new DFRobot_GAS_I2C(&Wire, gasO2Addr);
      } else {
        multigasO2->setI2cAddr(gasO2Addr);
      }

      if (!multigasO2->begin()) {
        printStatusLine("MULTIGAS(O2)", gasO2MuxAddr, gasO2Channel, "begin_failed");
        trackSensorStatus("MULTIGAS(O2)", false);
      } else {
        multigasO2->changeAcquireMode(DFRobot_GAS::PASSIVITY);
        multigasO2->setTempCompensation(DFRobot_GAS::ON);
        multigasO2Initialized = true;
        multigasO2EverConnected = true;
      }
    }
  }

  // ---- Init MultiGas(NH3) on mux_b (0x71), channel 5 ----
  if (!multigasNH3Initialized) {
    uint8_t ch = 0;
    uint8_t addr = 0;
    String t = "";

    if (!findMultiGasOnMuxForType(gasNH3MuxAddr, gasNH3Channel, "NH3", &ch, &addr, &t)) {
      printStatusLine("MULTIGAS(NH3)", gasNH3MuxAddr, gasNH3Channel, multigasNH3EverConnected ? "not_connected" : "not_found");
      trackSensorStatus("MULTIGAS(NH3)", false);
    } else {
      gasNH3Channel = ch;
      gasNH3Addr = addr;

      muxSelectChannel(gasNH3MuxAddr, gasNH3Channel);
      delay(5);

      if (multigasNH3 == nullptr) {
        multigasNH3 = new DFRobot_GAS_I2C(&Wire, gasNH3Addr);
      } else {
        multigasNH3->setI2cAddr(gasNH3Addr);
      }

      if (!multigasNH3->begin()) {
        printStatusLine("MULTIGAS(NH3)", gasNH3MuxAddr, gasNH3Channel, "begin_failed");
        trackSensorStatus("MULTIGAS(NH3)", false);
      } else {
        multigasNH3->changeAcquireMode(DFRobot_GAS::PASSIVITY);
        multigasNH3->setTempCompensation(DFRobot_GAS::ON);
        multigasNH3Initialized = true;
        multigasNH3EverConnected = true;
      }
    }
  }

  // ---- Init MultiGas(CO) on mux_b (0x71), channel 6 ----
  if (!multigasCOInitialized) {
    uint8_t ch = 0;
    uint8_t addr = 0;
    String t = "";

    if (!findMultiGasOnMuxForType(gasCOMuxAddr, gasCOChannel, "CO", &ch, &addr, &t)) {
      printStatusLine("MULTIGAS(CO)", gasCOMuxAddr, gasCOChannel, multigasCOEverConnected ? "not_connected" : "not_found");
      trackSensorStatus("MULTIGAS(CO)", false);
    } else {
      gasCOChannel = ch;
      gasCOAddr = addr;

      muxSelectChannel(gasCOMuxAddr, gasCOChannel);
      delay(5);

      if (multigasCO == nullptr) {
        multigasCO = new DFRobot_GAS_I2C(&Wire, gasCOAddr);
      } else {
        multigasCO->setI2cAddr(gasCOAddr);
      }

      if (!multigasCO->begin()) {
        printStatusLine("MULTIGAS(CO)", gasCOMuxAddr, gasCOChannel, "begin_failed");
        trackSensorStatus("MULTIGAS(CO)", false);
      } else {
        multigasCO->changeAcquireMode(DFRobot_GAS::PASSIVITY);
        multigasCO->setTempCompensation(DFRobot_GAS::ON);
        multigasCOInitialized = true;
        multigasCOEverConnected = true;
      }
    }
  }

  // ---- Init MultiGas(O3) on mux_b (0x71), channel 7 ----
  if (!multigasO3Initialized) {
    uint8_t ch = 0;
    uint8_t addr = 0;
    String t = "";

    if (!findMultiGasOnMuxForType(gasO3MuxAddr, gasO3Channel, "O3", &ch, &addr, &t)) {
      printStatusLine("MULTIGAS(O3)", gasO3MuxAddr, gasO3Channel, multigasO3EverConnected ? "not_connected" : "not_found");
      trackSensorStatus("MULTIGAS(O3)", false);
    } else {
      gasO3Channel = ch;
      gasO3Addr = addr;

      muxSelectChannel(gasO3MuxAddr, gasO3Channel);
      delay(5);

      if (multigasO3 == nullptr) {
        multigasO3 = new DFRobot_GAS_I2C(&Wire, gasO3Addr);
      } else {
        multigasO3->setI2cAddr(gasO3Addr);
      }

      if (!multigasO3->begin()) {
        printStatusLine("MULTIGAS(O3)", gasO3MuxAddr, gasO3Channel, "begin_failed");
        trackSensorStatus("MULTIGAS(O3)", false);
      } else {
        multigasO3->changeAcquireMode(DFRobot_GAS::PASSIVITY);
        multigasO3->setTempCompensation(DFRobot_GAS::ON);
        multigasO3Initialized = true;
        multigasO3EverConnected = true;
      }
    }
  }

  // ---- Read MS8607 ----
  if (msInitialized) {
    if (!ms8607Present(msMuxAddr, msMuxChannel)) {
      printStatusLine("MS8607", msMuxAddr, msMuxChannel, "not_connected");
      trackSensorStatus("MS8607", false);
      msInitialized = false;
      
      JsonObject msObj = doc.createNestedObject("MS8607");
      msObj["status"] = "offline";
    } else {
      sensors_event_t temp, pressure, humidity;
      ms8607.getEvent(&pressure, &temp, &humidity);

      printMs8607Line(msMuxAddr, msMuxChannel, temp.temperature, humidity.relative_humidity, pressure.pressure);
      trackSensorStatus("MS8607", true);

      JsonObject msObj = doc.createNestedObject("MS8607");
      msObj["status"] = "online";
      msObj["T"]  = temp.temperature;
      msObj["RH"] = humidity.relative_humidity;
      msObj["P"]  = pressure.pressure;
    }
  } else {
    JsonObject msObj = doc.createNestedObject("MS8607");
    msObj["status"] = "offline";
  }

  // ---- Read BH1750 ----
  if (bhInitialized) {
    if (!bh1750Present(bhMuxAddr, bhMuxChannel, bhAddr)) {
      printStatusLine("BH1750", bhMuxAddr, bhMuxChannel, "not_connected");
      trackSensorStatus("BH1750", false);
      bhInitialized = false;
      
      JsonObject bhObj = doc.createNestedObject("BH1750");
      bhObj["status"] = "offline";
    } else {
      float lux = bh1750.readLightLevel();
      printBh1750Line(bhMuxAddr, bhMuxChannel, lux);
      trackSensorStatus("BH1750", true);

      JsonObject bhObj = doc.createNestedObject("BH1750");
      bhObj["status"] = "online";
      bhObj["lux"] = lux;
    }
  } else {
    JsonObject bhObj = doc.createNestedObject("BH1750");
    bhObj["status"] = "offline";
  }

  // ---- Read MLX90614 IR thermometer ----
  if (mlxInitialized) {
    if (!mlx90614Present(mlxMuxAddr, mlxChannel)) {
      printStatusLine("MLX90614", mlxMuxAddr, mlxChannel, "not_connected");
      trackSensorStatus("MLX90614", false);
      mlxInitialized = false;
      
      JsonObject mlxObj = doc.createNestedObject("MLX90614");
      mlxObj["status"] = "offline";
    } else {
      // Only use the object temperature here; ambient comes from MS8607.
      float objC = mlx90614.getObjectTempCelsius();
      printMlx90614Line(mlxMuxAddr, mlxChannel, 0.0f, objC);
      trackSensorStatus("MLX90614", true);

      JsonObject mlxObj = doc.createNestedObject("MLX90614");
      mlxObj["status"] = "online";
      mlxObj["T_object"] = objC;
    }
  } else {
    JsonObject mlxObj = doc.createNestedObject("MLX90614");
    mlxObj["status"] = "offline";
  }

  // ---- Read STEMMA Soil capacitive sensor ----
  if (soilCapInitialized) {
    if (!soilCapPresent(soilCapMuxAddr, soilCapChannel, soilCapAddr)) {
      printStatusLine("SOIL_CAP_I2C", soilCapMuxAddr, soilCapChannel, "not_connected");
      trackSensorStatus("SOIL_CAP_I2C", false);
      soilCapInitialized = false;
      
      JsonObject capObj = doc.createNestedObject("SOIL_CAP_I2C");
      capObj["status"] = "offline";
    } else {
      muxSelectChannel(soilCapMuxAddr, soilCapChannel);
      delay(3);
      uint16_t moist = soilCapSeesaw.touchRead(0);   // channel 0 on STEMMA Soil
      printSoilCapSeesawLine(soilCapMuxAddr, soilCapChannel, moist);
      trackSensorStatus("SOIL_CAP_I2C", true);

      JsonObject capObj = doc.createNestedObject("SOIL_CAP_I2C");
      capObj["status"] = "online";
      capObj["moisture"] = moist;
    }
  } else {
    JsonObject capObj = doc.createNestedObject("SOIL_CAP_I2C");
    capObj["status"] = "offline";
  }

  // ---- Read Alcohol sensor ----
  if (alcoholInitialized && alcohol != nullptr) {
    if (!alcoholPresent(alcoholMuxAddr, alcoholMuxChannel, alcoholAddr)) {
      printStatusLine("ALCOHOL", alcoholMuxAddr, alcoholMuxChannel, "not_connected");
      trackSensorStatus("ALCOHOL", false);
      alcoholInitialized = false;
      
      JsonObject alcObj = doc.createNestedObject("ALCOHOL");
      alcObj["status"] = "offline";
    } else {
      // Default collectNum is 20 in the library, keep it explicit
      float ppm = alcohol->readAlcoholData(20);
      printAlcoholLine(alcoholMuxAddr, alcoholMuxChannel, ppm);
      trackSensorStatus("ALCOHOL", true);

      JsonObject alcObj = doc.createNestedObject("ALCOHOL");
      alcObj["status"] = "online";
      alcObj["ppm"] = ppm;
    }
  } else {
    JsonObject alcObj = doc.createNestedObject("ALCOHOL");
    alcObj["status"] = "offline";
  }

  // ---- Read CH4 sensor ----
  if (ch4Initialized && ch4 != nullptr) {
    if (!ch4Present(ch4MuxAddr, ch4MuxChannel, ch4Addr)) {
      printStatusLine("CH4", ch4MuxAddr, ch4MuxChannel, "not_connected");
      trackSensorStatus("CH4", false);
      ch4Initialized = false;
      
      JsonObject ch4Obj = doc.createNestedObject("CH4");
      ch4Obj["status"] = "offline";
    } else {
      muxSelectChannel(ch4MuxAddr, ch4MuxChannel);
      delay(3);
      float lel = ch4->getCH4Concentration();
      float tC = ch4->getTemperature();
      int err = (int)ch4->getErrorMsg();
      printCh4Line(ch4MuxAddr, ch4MuxChannel, lel, tC, err);
      trackSensorStatus("CH4", true);

      JsonObject ch4Obj = doc.createNestedObject("CH4");
      ch4Obj["status"] = "online";
      ch4Obj["lel_pct"] = lel;
      ch4Obj["tempC"]  = tC;
      ch4Obj["err"]    = err;
    }
  } else {
    JsonObject ch4Obj = doc.createNestedObject("CH4");
    ch4Obj["status"] = "offline";
  }

  // ---- Read RS485 Soil EC+pH sensor ----
  if (soilInitialized) {
    float ecNow = 0.0f;
    float phNow = 0.0f;
    if (!readSoilSensorOnce(ecNow, phNow)) {
      printSoilStatusLine("comm_error");
      trackSensorStatus("SOIL_EC_PH", false);
      soilInitialized = false;
      
      JsonObject soilObj = doc.createNestedObject("SOIL_EC_PH");
      soilObj["status"] = "offline";
    } else {
      soilEcMsPerCm = ecNow;
      soilPh = phNow;
      printSoilECPHLine(soilEcMsPerCm, soilPh);
      trackSensorStatus("SOIL_EC_PH", true);

      JsonObject soilObj = doc.createNestedObject("SOIL_EC_PH");
      soilObj["status"] = "online";
      soilObj["ec_mS_per_cm"] = soilEcMsPerCm;
      soilObj["pH"]           = soilPh;
    }
  } else {
    JsonObject soilObj = doc.createNestedObject("SOIL_EC_PH");
    soilObj["status"] = "offline";
  }

  // ---- Read HCHO UART sensor ----
  if (hchoInitialized) {
    bool haveFrame = false;
    for (int i = 0; i < 64; ++i) {
      if (hcho.available()) {
        haveFrame = true;
        break;
      }
      if (HCHO_Serial.available() == 0) {
        break;
      }
    }

    if (!haveFrame) {
      printHchoStatusLine("no_data");
      trackSensorStatus("HCHO_UART", false);
      
      JsonObject hchoObj = doc.createNestedObject("HCHO_UART");
      hchoObj["status"] = "offline";
    } else {
      hchoPpm = hcho.uartReadPPM();
      printHchoLine(hchoPpm);
      trackSensorStatus("HCHO_UART", true);

      JsonObject hchoObj = doc.createNestedObject("HCHO_UART");
      hchoObj["status"] = "online";
      hchoObj["ppm"] = hchoPpm;
    }
  } else {
    JsonObject hchoObj = doc.createNestedObject("HCHO_UART");
    hchoObj["status"] = "offline";
  }

  // ---- Read MultiGas(H2S) ----
  if (multigasH2SInitialized && multigasH2S != nullptr) {
    String t;
    float conc = 0.0f;
    bool ok = readMultiGasWithRetries(multigasH2S,
                                      gasH2SMuxAddr,
                                      gasH2SChannel,
                                      gasH2SAddr,
                                      t,
                                      conc);

    if (!ok) {
      multigasH2SFailCycles++;
      if (multigasH2SFailCycles < SENSOR_MAX_FAIL_CYCLES) {
        // Still warming up / stabilising: don't mark offline yet.
        printStatusLine("MULTIGAS(H2S)", gasH2SMuxAddr, gasH2SChannel, "warming_up");
        trackSensorStatus("MULTIGAS(H2S)", true);
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("H2S");
        gas["status"] = "warming_up";
        gas["unit"]  = "ppm";
      } else {
        printStatusLine("MULTIGAS(H2S)", gasH2SMuxAddr, gasH2SChannel, "comm_error");
        trackSensorStatus("MULTIGAS(H2S)", false);
        multigasH2SInitialized = false;
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("H2S");
        gas["status"] = "offline";
      }
    } else {
      multigasH2SFailCycles = 0;
      printMultiGasLine(gasH2SMuxAddr, gasH2SChannel, t, conc, "ppm");
      trackSensorStatus("MULTIGAS(H2S)", true);

      JsonObject mg = getMultigasObject();
      // Use a fixed key so the backend can reliably map this block.
      JsonObject gas = mg.createNestedObject("H2S");
      gas["status"] = "online";
      gas["value"] = conc;
      gas["unit"]  = "ppm";
    }
  } else {
    JsonObject mg = getMultigasObject();
    JsonObject gas = mg.createNestedObject("H2S");
    gas["status"] = "offline";
  }

  // ---- Read MultiGas(O2) ----
  if (multigasO2Initialized && multigasO2 != nullptr) {
    String t;
    float conc = 0.0f;
    bool ok = readMultiGasWithRetries(multigasO2,
                                      gasO2MuxAddr,
                                      gasO2Channel,
                                      gasO2Addr,
                                      t,
                                      conc);

    if (!ok) {
      multigasO2FailCycles++;
      if (multigasO2FailCycles < SENSOR_MAX_FAIL_CYCLES) {
        printStatusLine("MULTIGAS(O2)", gasO2MuxAddr, gasO2Channel, "warming_up");
        trackSensorStatus("MULTIGAS(O2)", true);
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("O2");
        gas["status"] = "warming_up";
        gas["unit"]  = "%vol";
      } else {
        printStatusLine("MULTIGAS(O2)", gasO2MuxAddr, gasO2Channel, "comm_error");
        trackSensorStatus("MULTIGAS(O2)", false);
        multigasO2Initialized = false;
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("O2");
        gas["status"] = "offline";
      }
    } else {
      multigasO2FailCycles = 0;
      const char *unit = (t == "O2") ? "%vol" : "ppm";
      printMultiGasLine(gasO2MuxAddr, gasO2Channel, t, conc, unit);
      trackSensorStatus("MULTIGAS(O2)", true);

      JsonObject mg = getMultigasObject();
      // Use a fixed key so the backend can reliably map this block.
      JsonObject gas = mg.createNestedObject("O2");
      gas["status"] = "online";
      gas["value"] = conc;
      gas["unit"]  = unit;
    }
  } else {
    JsonObject mg = getMultigasObject();
    JsonObject gas = mg.createNestedObject("O2");
    gas["status"] = "offline";
  }

  // ---- Read MultiGas(NH3) ----
  if (multigasNH3Initialized && multigasNH3 != nullptr) {
    String t;
    float conc = 0.0f;
    bool ok = readMultiGasWithRetries(multigasNH3,
                                      gasNH3MuxAddr,
                                      gasNH3Channel,
                                      gasNH3Addr,
                                      t,
                                      conc);

    if (!ok) {
      multigasNH3FailCycles++;
      if (multigasNH3FailCycles < SENSOR_MAX_FAIL_CYCLES) {
        printStatusLine("MULTIGAS(NH3)", gasNH3MuxAddr, gasNH3Channel, "warming_up");
        trackSensorStatus("MULTIGAS(NH3)", true);
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("NH3");
        gas["status"] = "warming_up";
        gas["unit"]  = "ppm";
      } else {
        printStatusLine("MULTIGAS(NH3)", gasNH3MuxAddr, gasNH3Channel, "comm_error");
        trackSensorStatus("MULTIGAS(NH3)", false);
        multigasNH3Initialized = false;
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("NH3");
        gas["status"] = "offline";
      }
    } else {
      multigasNH3FailCycles = 0;
      const char *unit = "ppm";
      printMultiGasLine(gasNH3MuxAddr, gasNH3Channel, t, conc, unit);
      trackSensorStatus("MULTIGAS(NH3)", true);

      JsonObject mg = getMultigasObject();
      JsonObject gas = mg.createNestedObject("NH3");
      gas["status"] = "online";
      gas["value"] = conc;
      gas["unit"]  = unit;
    }
  } else {
    JsonObject mg = getMultigasObject();
    JsonObject gas = mg.createNestedObject("NH3");
    gas["status"] = "offline";
  }

  // ---- Read MultiGas(CO) ----
  if (multigasCOInitialized && multigasCO != nullptr) {
    String t;
    float conc = 0.0f;
    bool ok = readMultiGasWithRetries(multigasCO,
                                      gasCOMuxAddr,
                                      gasCOChannel,
                                      gasCOAddr,
                                      t,
                                      conc);

    if (!ok) {
      multigasCOFailCycles++;
      if (multigasCOFailCycles < SENSOR_MAX_FAIL_CYCLES) {
        printStatusLine("MULTIGAS(CO)", gasCOMuxAddr, gasCOChannel, "warming_up");
        trackSensorStatus("MULTIGAS(CO)", true);
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("CO");
        gas["status"] = "warming_up";
        gas["unit"]  = "ppm";
      } else {
        printStatusLine("MULTIGAS(CO)", gasCOMuxAddr, gasCOChannel, "comm_error");
        trackSensorStatus("MULTIGAS(CO)", false);
        multigasCOInitialized = false;
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("CO");
        gas["status"] = "offline";
      }
    } else {
      multigasCOFailCycles = 0;
      const char *unit = "ppm";
      printMultiGasLine(gasCOMuxAddr, gasCOChannel, t, conc, unit);
      trackSensorStatus("MULTIGAS(CO)", true);

      JsonObject mg = getMultigasObject();
      JsonObject gas = mg.createNestedObject("CO");
      gas["status"] = "online";
      gas["value"] = conc;
      gas["unit"]  = unit;
    }
  } else {
    JsonObject mg = getMultigasObject();
    JsonObject gas = mg.createNestedObject("CO");
    gas["status"] = "offline";
  }

  // ---- Read MultiGas(O3) ----
  if (multigasO3Initialized && multigasO3 != nullptr) {
    String t;
    float conc = 0.0f;
    bool ok = readMultiGasWithRetries(multigasO3,
                                      gasO3MuxAddr,
                                      gasO3Channel,
                                      gasO3Addr,
                                      t,
                                      conc);

    if (!ok) {
      multigasO3FailCycles++;
      if (multigasO3FailCycles < SENSOR_MAX_FAIL_CYCLES) {
        printStatusLine("MULTIGAS(O3)", gasO3MuxAddr, gasO3Channel, "warming_up");
        trackSensorStatus("MULTIGAS(O3)", true);
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("O3");
        gas["status"] = "warming_up";
        gas["unit"]  = "ppm";
      } else {
        printStatusLine("MULTIGAS(O3)", gasO3MuxAddr, gasO3Channel, "comm_error");
        trackSensorStatus("MULTIGAS(O3)", false);
        multigasO3Initialized = false;
        
        JsonObject mg = getMultigasObject();
        JsonObject gas = mg.createNestedObject("O3");
        gas["status"] = "offline";
      }
    } else {
      multigasO3FailCycles = 0;
      const char *unit = "ppm";
      printMultiGasLine(gasO3MuxAddr, gasO3Channel, t, conc, unit);
      trackSensorStatus("MULTIGAS(O3)", true);

      JsonObject mg = getMultigasObject();
      JsonObject gas = mg.createNestedObject("O3");
      gas["status"] = "online";
      gas["value"] = conc;
      gas["unit"]  = unit;
    }
  } else {
    JsonObject mg = getMultigasObject();
    JsonObject gas = mg.createNestedObject("O3");
    gas["status"] = "offline";
  }

  // Add per-cycle summary into JSON as well
  JsonObject summary = doc.createNestedObject("summary");
  summary["sensor_total"]  = g_sensorTotalCount;
  summary["offline_count"] = g_sensorOfflineCount;
  summary["offline_list"]  = g_sensorOfflineList;

  // Serialize JSON payload
  String payload;
  serializeJson(doc, payload);
  
  // Print COMPLETE JSON only on SECOND cycle (when posting to backend)
  if (measurementCycle == 1) {
    Serial.print(F("JSON payload length="));
    Serial.println(payload.length());
    if (doc.overflowed()) {
      Serial.println(F("WARNING: JSON document overflowed"));
    }
    
    // Print COMPLETE JSON payload to serial monitor
    Serial.println(F("COMPLETE JSON="));
    serializeJson(doc, Serial);
    Serial.println();
    
    // Also print MULTIGAS section separately for debugging
    Serial.print(F("MULTIGAS JSON="));
    serializeJson(doc["MULTIGAS"], Serial);
    Serial.println();
  }

  // Decide whether to POST this cycle:
  // - Cycle 0 (first): Status check only, no POST
  // - Cycle 1 (second): Real measurement, POST to backend
  bool shouldPostThisCycle = (measurementCycle == 1);
  
  if (measurementCycle == 0) {
    Serial.println(F("[CYCLE 1] Status check complete - NOT posting to backend"));
  } else if (measurementCycle == 1) {
    Serial.println(F("[CYCLE 2] Real measurement - POSTING to backend"));
  }

  // Send JSON to backend over WiFi (only on second cycle)
  if (shouldPostThisCycle) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;

      if (!http.begin(client, BACKEND_URL)) {
        Serial.println(F("HTTP begin() failed"));
      } else {
        http.addHeader("Content-Type", "application/json");
        int httpCode = http.POST(payload);
        Serial.print(F("HTTP POST to backend, code="));
        Serial.println(httpCode);
        Serial.print(F("HTTP error string: "));
        Serial.println(http.errorToString(httpCode));
        http.end();
      }
    } else {
      Serial.println(F("WiFi not connected, skipping backend POST"));
    }
  }

  // Power OFF sensors to save energy until the next sampling interval.
  digitalWrite(SENSOR_MOSFET_PIN, LOW);

  printSensorSummary();

  // Increment cycle counter
  measurementCycle++;
  lastCycleMs = millis();
  
  // If we've only done first cycle, return to loop for second cycle
  if (measurementCycle == 1) {
    Serial.println(F("First cycle complete. Preparing for second cycle..."));
    return;
  }
  
  // Both cycles complete, reset counter
  measurementCycle = 0;
  
  // If this was a button wake, enter active mode for 30 seconds before sleeping
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 && !buttonMeasurementDone) {
    // Mark measurement as done and enter active mode
    buttonMeasurementDone = true;
    isButtonActiveMode = true;
    buttonWakeStartMs = millis();
    Serial.println(F("Button measurement complete, staying active for 30 seconds..."));
    return;  // Don't sleep yet, stay in active mode
  }

  // Enter deep sleep mode to conserve power
  Serial.println(F("Both cycles complete. Entering deep sleep mode..."));
  Serial.flush();  // Wait for serial output to complete
  enterDeepSleep();
}

// Helper function to configure and enter deep sleep
void enterDeepSleep() {
  // Configure wake-up sources:
  // 1. Timer wake-up (every 30 seconds for scheduled measurements)
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_INTERVAL_US);
  
  // 2. External wake-up (button on GPIO 7, wake on LOW when button pressed to GND)
  // DISABLED: Uncomment below if you have a button connected to GPIO 7
  // esp_sleep_enable_ext0_wakeup(GPIO_NUM_7, 0);  // 0 = LOW level triggers wake
  
  Serial.println(F("Deep sleep configured: wake on timer only"));
  Serial.flush();
  
  // Enter deep sleep (ESP32 will restart from setup() on wake)
  esp_deep_sleep_start();
  
  // Code never reaches here
}
