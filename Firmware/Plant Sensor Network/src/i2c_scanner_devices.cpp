#include <Arduino.h>
#include <Wire.h>

// ESP32-S3 custom I2C pins
static const int SDA_PIN = 8;
static const int SCL_PIN = 9;

// Visual debug LED (you said you're disconnecting USB)
static const int LED_PIN = 19;

static const uint16_t LED_ON_MS  = 180;
static const uint16_t LED_OFF_MS = 180;
static const uint16_t LED_PAUSE_MS_AFTER_CYCLE = 5000;

// TCA9548A / PCA9548A muxes typically live at 0x70-0x77 (A0/A1/A2 strap pins)
static const uint8_t POSSIBLE_MUX_ADDRS[] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77};
static const uint8_t MAX_MUXES = sizeof(POSSIBLE_MUX_ADDRS) / sizeof(POSSIBLE_MUX_ADDRS[0]);

static bool i2cPing(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

static bool isInList(uint8_t value, const uint8_t *list, uint8_t listLen) {
  for (uint8_t i = 0; i < listLen; i++) {
    if (list[i] == value) return true;
  }
  return false;
}

static void muxDisableAll(uint8_t muxAddr) {
  Wire.beginTransmission(muxAddr);
  Wire.write(0x00); // no channel selected
  Wire.endTransmission();
}

static void muxSelectChannel(uint8_t muxAddr, uint8_t channel) {
  if (channel > 7) return;
  Wire.beginTransmission(muxAddr);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

static uint8_t detectMuxes(uint8_t *foundMuxAddrs, uint8_t maxFound) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_MUXES && count < maxFound; i++) {
    uint8_t addr = POSSIBLE_MUX_ADDRS[i];
    if (i2cPing(addr)) {
      foundMuxAddrs[count++] = addr;
    }
  }
  return count;
}

static void blinkCount(uint16_t count) {
  // Blink count times. If count==0, do a single long blink to indicate "zero".
  if (count == 0) {
    digitalWrite(LED_PIN, HIGH);
    delay(800);
    digitalWrite(LED_PIN, LOW);
    return;
  }

  for (uint16_t i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(LED_ON_MS);
    digitalWrite(LED_PIN, LOW);
    delay(LED_OFF_MS);
  }
}

static uint8_t scanDevicesBehindMux(uint8_t muxIndex,
                                   uint8_t muxAddr,
                                   const uint8_t *allMuxAddrs,
                                   uint8_t muxCount) {
  Serial.println();
  Serial.print(F("=== MUX #"));
  Serial.print(muxIndex);
  Serial.print(F(" address 0x"));
  if (muxAddr < 16) Serial.print('0');
  Serial.print(muxAddr, HEX);
  Serial.println(F(" ==="));

  // Count devices as (channel + address).
  // - Same address on DIFFERENT channels counts as DIFFERENT devices (your requirement).
  // - Duplicate hits of same address within the SAME channel are counted once.
  uint8_t deviceCount = 0;

  muxDisableAll(muxAddr);
  delay(3);

  for (uint8_t ch = 0; ch < 8; ch++) {
    muxSelectChannel(muxAddr, ch);
    delay(3);

    bool anyOnChannel = false;
    bool seenOnChannel[128] = {false};

    for (uint8_t address = 1; address < 127; address++) {
      // Skip mux addresses so muxes don't appear as devices
      if (isInList(address, allMuxAddrs, muxCount)) {
        continue;
      }

      Wire.beginTransmission(address);
      uint8_t err = Wire.endTransmission();
      if (err == 0) {
        anyOnChannel = true;

        // Count this (channel,address) only once
        if (!seenOnChannel[address]) {
          seenOnChannel[address] = true;
          deviceCount++;
        }

        Serial.print(F("MUX #"));
        Serial.print(muxIndex);
        Serial.print(F(" (0x"));
        if (muxAddr < 16) Serial.print('0');
        Serial.print(muxAddr, HEX);
        Serial.print(F(") ch "));
        Serial.print(ch);
        Serial.print(F(" -> device 0x"));
        if (address < 16) Serial.print('0');
        Serial.println(address, HEX);
      }

      delay(2);
    }

    if (!anyOnChannel) {
      Serial.print(F("MUX #"));
      Serial.print(muxIndex);
      Serial.print(F(" ch "));
      Serial.print(ch);
      Serial.println(F(" -> (no devices)"));
    }
  }

  muxDisableAll(muxAddr);

  Serial.print(F("MUX #"));
  Serial.print(muxIndex);
  Serial.print(F(" devices (counted per-channel) = "));
  Serial.println(deviceCount);

  return deviceCount;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=== ESP32-S3 I2C Scanner (Mux + Devices) ==="));

  Wire.begin(SDA_PIN, SCL_PIN);
}

void loop() {
  uint8_t muxAddrs[8];
  uint8_t muxCount = detectMuxes(muxAddrs, 8);

  Serial.println();
  Serial.print(F("Detected mux count: "));
  Serial.println(muxCount);

  for (uint8_t i = 0; i < muxCount; i++) {
    Serial.print(F("  MUX #"));
    Serial.print(i + 1);
    Serial.print(F(" address = 0x"));
    if (muxAddrs[i] < 16) Serial.print('0');
    Serial.println(muxAddrs[i], HEX);
  }

  if (muxCount == 0) {
    Serial.println(F("No mux detected at 0x70-0x77."));
    delay(2000);
    return;
  }

  // Helpful warning for your use case (two muxes in parallel)
  if (muxCount == 1) {
    Serial.println(F("NOTE: If you connected TWO mux boards in parallel, they MUST have different I2C addresses (0x70-0x77)."));
  }

  // Ensure all muxes are disabled before scanning
  for (uint8_t i = 0; i < muxCount; i++) {
    muxDisableAll(muxAddrs[i]);
  }

  uint8_t totals[8] = {0};
  for (uint8_t i = 0; i < muxCount; i++) {
    totals[i] = scanDevicesBehindMux(i + 1, muxAddrs[i], muxAddrs, muxCount);
  }

  Serial.println();
  Serial.println(F("=== Summary (devices counted per-channel) ==="));
  uint16_t totalDevicesAllMuxes = 0;
  for (uint8_t i = 0; i < muxCount; i++) {
    Serial.print(F("MUX #"));
    Serial.print(i + 1);
    Serial.print(F(" (0x"));
    if (muxAddrs[i] < 16) Serial.print('0');
    Serial.print(muxAddrs[i], HEX);
    Serial.print(F(") devices = "));
    Serial.println(totals[i]);

    totalDevicesAllMuxes += totals[i];
  }

  // Visual debug:
  // Blink LED_PIN exactly "totalDevicesAllMuxes" times, then keep LED off for 5 seconds.
  // Example from your log: MUX1=5 and MUX2=1 => total=6 blinks.
  blinkCount(totalDevicesAllMuxes);
  digitalWrite(LED_PIN, LOW);

  Serial.println(F("\n=== Scan cycle complete ===\n"));
  delay(LED_PAUSE_MS_AFTER_CYCLE);
}
