#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MS8607.h>
#include <Adafruit_Sensor.h>
#include <BH1750.h>
#include <DFRobot_Alcohol.h>
#include <DFRobot_MultiGasSensor.h>
#include <DFRobot_MHZ9041A.h>

// ESP32-S3 custom I2C pins
static const int SDA_PIN = 8;
static const int SCL_PIN = 9;

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

static Adafruit_MS8607 ms8607;
static BH1750 bh1750;
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

static bool i2cPing(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

// Forward declarations (used by the presence-check helpers below)
static void muxDisableAll(uint8_t muxAddr);
static void muxSelectChannel(uint8_t muxAddr, uint8_t channel);

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

static const char *muxLabelFromAddr(uint8_t muxAddr) {
  // Your setup uses mux A=0x70 and mux B=0x71.
  if (muxAddr == 0x70) return "mux_a";
  if (muxAddr == 0x71) return "mux_b";
  return "mux_?";
}

static void chLabel(uint8_t channel, char *out, size_t outLen) {
  snprintf(out, outLen, "ch%u", channel);
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=== Sensors (start: MS8607 via I2C mux) ==="));

  Wire.begin(SDA_PIN, SCL_PIN);

  printHeaderOnce();
}

void loop() {
  Serial.println();
  Serial.println(F("--- cycle ---"));
  static bool msInitialized = false;
  static bool bhInitialized = false;
  static bool alcoholInitialized = false;
  static bool multigasH2SInitialized = false;
  static bool multigasO2Initialized = false;
  static bool multigasNH3Initialized = false;
  static bool multigasCOInitialized  = false;
  static bool multigasO3Initialized  = false;
  static bool ch4Initialized = false;

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

  // ---- Init MS8607 once (auto-detect) ----
  if (!msInitialized) {
    if (!findMs8607Location(&msMuxAddr, &msMuxChannel)) {
      printStatusLine("MS8607", 0x00, 255, msEverConnected ? "not_connected" : "not_found");
    } else {
      muxSelectChannel(msMuxAddr, msMuxChannel);
      delay(5);

      if (!ms8607.begin()) {
        printStatusLine("MS8607", msMuxAddr, msMuxChannel, "begin_failed");
      } else {
        printStatusLine("MS8607", msMuxAddr, msMuxChannel, "ready");
        msInitialized = true;
        msEverConnected = true;
      }
    }
  }

  // ---- Init BH1750 once (auto-detect) ----
  if (!bhInitialized) {
    if (!findBh1750Location(&bhMuxAddr, &bhMuxChannel, &bhAddr)) {
      printStatusLine("BH1750", 0x00, 255, bhEverConnected ? "not_connected" : "not_found");
    } else {
      muxSelectChannel(bhMuxAddr, bhMuxChannel);
      delay(5);

      // Use continuous high-res mode for stable readings
      if (!bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, bhAddr, &Wire)) {
        printStatusLine("BH1750", bhMuxAddr, bhMuxChannel, "begin_failed");
      } else {
        printStatusLine("BH1750", bhMuxAddr, bhMuxChannel, "ready");
        bhInitialized = true;
        bhEverConnected = true;
      }
    }
  }

  // ---- Init Alcohol sensor once (auto-detect) ----
  if (!alcoholInitialized) {
    if (!findAlcoholLocation(&alcoholMuxAddr, &alcoholMuxChannel, &alcoholAddr)) {
      printStatusLine("ALCOHOL", 0x00, 255, alcoholEverConnected ? "not_connected" : "not_found");
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
      printStatusLine("ALCOHOL", alcoholMuxAddr, alcoholMuxChannel, "ready");
      alcoholInitialized = true;
      alcoholEverConnected = true;
    }
  }

  // ---- Init CH4 sensor once (auto-detect) ----
  if (!ch4Initialized) {
    if (!findCh4Location(&ch4MuxAddr, &ch4MuxChannel, &ch4Addr)) {
      printStatusLine("CH4", 0x00, 255, ch4EverConnected ? "not_connected" : "not_found");
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
      } else {
        ch4->setMode(ePassivityMode);
        printStatusLine("CH4", ch4MuxAddr, ch4MuxChannel, "ready");
        ch4Initialized = true;
        ch4EverConnected = true;
      }
    }
  }

  // ---- Init MultiGas(H2S) on mux_a (0x70) ----
  if (!multigasH2SInitialized) {
    uint8_t ch = 0;
    uint8_t addr = 0;
    String t = "";

    if (!findMultiGasOnMuxForType(gasH2SMuxAddr, 6, "H2S", &ch, &addr, &t)) {
      printStatusLine("MULTIGAS(H2S)", 0x70, 6, multigasH2SEverConnected ? "not_connected" : "not_found");
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
      msInitialized = false;
    } else {
      sensors_event_t temp, pressure, humidity;
      ms8607.getEvent(&pressure, &temp, &humidity);

      printMs8607Line(msMuxAddr, msMuxChannel, temp.temperature, humidity.relative_humidity, pressure.pressure);
    }
  }

  // ---- Read BH1750 ----
  if (bhInitialized) {
    if (!bh1750Present(bhMuxAddr, bhMuxChannel, bhAddr)) {
      printStatusLine("BH1750", bhMuxAddr, bhMuxChannel, "not_connected");
      bhInitialized = false;
    } else {
      float lux = bh1750.readLightLevel();
      printBh1750Line(bhMuxAddr, bhMuxChannel, lux);
    }
  }

  // ---- Read Alcohol sensor ----
  if (alcoholInitialized && alcohol != nullptr) {
    if (!alcoholPresent(alcoholMuxAddr, alcoholMuxChannel, alcoholAddr)) {
      printStatusLine("ALCOHOL", alcoholMuxAddr, alcoholMuxChannel, "not_connected");
      alcoholInitialized = false;
    } else {
      // Default collectNum is 20 in the library, keep it explicit
      float ppm = alcohol->readAlcoholData(20);
      printAlcoholLine(alcoholMuxAddr, alcoholMuxChannel, ppm);
    }
  }

  // ---- Read CH4 sensor ----
  if (ch4Initialized && ch4 != nullptr) {
    if (!ch4Present(ch4MuxAddr, ch4MuxChannel, ch4Addr)) {
      printStatusLine("CH4", ch4MuxAddr, ch4MuxChannel, "not_connected");
      ch4Initialized = false;
    } else {
      muxSelectChannel(ch4MuxAddr, ch4MuxChannel);
      delay(3);
      float lel = ch4->getCH4Concentration();
      float tC = ch4->getTemperature();
      int err = (int)ch4->getErrorMsg();
      printCh4Line(ch4MuxAddr, ch4MuxChannel, lel, tC, err);
    }
  }

  // ---- Read MultiGas(H2S) ----
  if (multigasH2SInitialized && multigasH2S != nullptr) {
    if (!multigasPresent(gasH2SMuxAddr, gasH2SChannel, gasH2SAddr)) {
      printStatusLine("MULTIGAS(H2S)", gasH2SMuxAddr, gasH2SChannel, "not_connected");
      multigasH2SInitialized = false;
    } else {
      String t = multigasH2S->queryGasType();
      float conc = multigasH2S->readGasConcentrationPPM();
      if (t.length() == 0) {
        printStatusLine("MULTIGAS(H2S)", gasH2SMuxAddr, gasH2SChannel, "comm_error");
        multigasH2SInitialized = false;
      } else {
        printMultiGasLine(gasH2SMuxAddr, gasH2SChannel, t, conc, "ppm");
      }
    }
  }

  // ---- Read MultiGas(O2) ----
  if (multigasO2Initialized && multigasO2 != nullptr) {
    if (!multigasPresent(gasO2MuxAddr, gasO2Channel, gasO2Addr)) {
      printStatusLine("MULTIGAS(O2)", gasO2MuxAddr, gasO2Channel, "not_connected");
      multigasO2Initialized = false;
    } else {
      String t = multigasO2->queryGasType();
      float conc = multigasO2->readGasConcentrationPPM();
      if (t.length() == 0) {
        printStatusLine("MULTIGAS(O2)", gasO2MuxAddr, gasO2Channel, "comm_error");
        multigasO2Initialized = false;
      } else {
        const char *unit = (t == "O2") ? "%vol" : "ppm";
        printMultiGasLine(gasO2MuxAddr, gasO2Channel, t, conc, unit);
      }
    }
  }

  // ---- Read MultiGas(NH3) ----
  if (multigasNH3Initialized && multigasNH3 != nullptr) {
    if (!multigasPresent(gasNH3MuxAddr, gasNH3Channel, gasNH3Addr)) {
      printStatusLine("MULTIGAS(NH3)", gasNH3MuxAddr, gasNH3Channel, "not_connected");
      multigasNH3Initialized = false;
    } else {
      String t = multigasNH3->queryGasType();
      float conc = multigasNH3->readGasConcentrationPPM();
      if (t.length() == 0) {
        printStatusLine("MULTIGAS(NH3)", gasNH3MuxAddr, gasNH3Channel, "comm_error");
        multigasNH3Initialized = false;
      } else {
        const char *unit = "ppm";
        printMultiGasLine(gasNH3MuxAddr, gasNH3Channel, t, conc, unit);
      }
    }
  }

  // ---- Read MultiGas(CO) ----
  if (multigasCOInitialized && multigasCO != nullptr) {
    if (!multigasPresent(gasCOMuxAddr, gasCOChannel, gasCOAddr)) {
      printStatusLine("MULTIGAS(CO)", gasCOMuxAddr, gasCOChannel, "not_connected");
      multigasCOInitialized = false;
    } else {
      String t = multigasCO->queryGasType();
      float conc = multigasCO->readGasConcentrationPPM();
      if (t.length() == 0) {
        printStatusLine("MULTIGAS(CO)", gasCOMuxAddr, gasCOChannel, "comm_error");
        multigasCOInitialized = false;
      } else {
        const char *unit = "ppm";
        printMultiGasLine(gasCOMuxAddr, gasCOChannel, t, conc, unit);
      }
    }
  }

  // ---- Read MultiGas(O3) ----
  if (multigasO3Initialized && multigasO3 != nullptr) {
    if (!multigasPresent(gasO3MuxAddr, gasO3Channel, gasO3Addr)) {
      printStatusLine("MULTIGAS(O3)", gasO3MuxAddr, gasO3Channel, "not_connected");
      multigasO3Initialized = false;
    } else {
      String t = multigasO3->queryGasType();
      float conc = multigasO3->readGasConcentrationPPM();
      if (t.length() == 0) {
        printStatusLine("MULTIGAS(O3)", gasO3MuxAddr, gasO3Channel, "comm_error");
        multigasO3Initialized = false;
      } else {
        const char *unit = "ppm";
        printMultiGasLine(gasO3MuxAddr, gasO3Channel, t, conc, unit);
      }
    }
  }

  delay(2000);
}
