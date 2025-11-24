#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_MultiGasSensor.h>

// ESP32-S3 custom I2C pins
static const int SDA_PIN = 8;
static const int SCL_PIN = 9;

// I2C multiplexer (e.g. TCA9548A) default address
static const uint8_t I2C_MUX_ADDR = 0x70;

// Multi-gas sensor behind the multiplexer
// NOTE: Set this to your actual I2C address from the DIP switches.
// The example uses 0x77 as default; your board may be 0x74–0x77.
// If your earlier scan saw it at 0x74, keep 0x74 here.
static const uint8_t SENSOR_ADDR    = 0x74;
static const uint8_t SENSOR_CHANNEL = 7;   // mux channel where the probe is connected

// Use the I2C version of the multi-gas sensor driver
DFRobot_GAS_I2C gas(&Wire, SENSOR_ADDR);

// Select a channel (0–7) on the I2C multiplexer
void selectMuxChannel(uint8_t channel) {
  if (channel > 7) return;

  Wire.beginTransmission(I2C_MUX_ADDR);
  Wire.write(1 << channel);  // each bit enables one channel
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=== ESP32-S3 Multi-Gas Sensor via I2C Multiplexer ==="));

  // Initialize I2C on the specified pins
  Wire.begin(SDA_PIN, SCL_PIN);  // SDA, SCL

  // Select the channel where the gas sensor is connected
  selectMuxChannel(SENSOR_CHANNEL);
  delay(5);

  // Sensor init (same idea as example’s while(!gas.begin()) loop)
  while (!gas.begin()) {
    Serial.println(F("NO Devices!"));
    delay(1000);
  }
  Serial.println(F("The device is connected successfully!"));

  // Mode of obtaining data: controller requests data (PASSIVITY)
  gas.changeAcquireMode(DFRobot_GAS::PASSIVITY);
  delay(1000);

  // Turn on temperature compensation
  gas.setTempCompensation(DFRobot_GAS::ON);
}

void loop() {
  // Ensure the correct mux channel is selected before each reading
  selectMuxChannel(SENSOR_CHANNEL);
  delay(2);

  // Query gas type and read concentration
  String gasType = gas.queryGasType();
  float gasConc  = gas.readGasConcentrationPPM();

  Serial.print(F("Ambient "));
  Serial.print(gasType);
  Serial.print(F(" concentration is: "));
  Serial.print(gasConc, 2);

  if (gasType == "O2") {
    Serial.println(F(" %vol"));
  } else {
    Serial.println(F(" PPM"));
  }

  // Also show onboard temperature
  float tempC = gas.readTempC();
  Serial.print(F("Board temperature: "));
  Serial.print(tempC, 2);
  Serial.println(F(" C"));
  Serial.println();

  delay(1000);
}