#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_MultiGasSensor.h>
#include <Adafruit_MS8607.h>
#include <Adafruit_Sensor.h>
#include <DFRobot_MLX90614.h>
#include <hp_BH1750.h>

// ESP32-S3 custom I2C pins
static const int SDA_PIN = 8;
static const int SCL_PIN = 9;

// I2C multiplexers (e.g. TCA9548A) addresses
static const uint8_t I2C_MUX1_ADDR = 0x70;   // mux 1 (MS8607, MLX90614, MultiGas)
static const uint8_t I2C_MUX2_ADDR = 0x70;   // same physical mux, BH1750 on channel 3

// Logical mux indices just for display
static const uint8_t MUX1_INDEX = 1;
static const uint8_t MUX2_INDEX = 2;

// MS8607 sensor (temp, humidity, pressure) on mux1 channel 0
static const uint8_t MS8607_CHANNEL = 0;

// MLX90614 IR temperature sensor on mux1 channel 6
static const uint8_t MLX90614_CHANNEL = 6;

// Multi-gas (O2) sensor on mux1 channel 7
static const uint8_t GAS_SENSOR_ADDR    = 0x74;
static const uint8_t GAS_SENSOR_CHANNEL = 7;

// BH1750 light sensor on mux2 channel 3
static const uint8_t BH1750_CHANNEL = 3;

// Sensor driver instances
Adafruit_MS8607 ms8607;
DFRobot_MLX90614_I2C mlx90614;              // default I2C addr 0x5A
DFRobot_GAS_I2C gas(&Wire, GAS_SENSOR_ADDR);
// BH1750 address pin to GND / floating -> 0x23 (default on your board)
hp_BH1750 bh1750;

// Select a channel (0–7) on a specific I2C multiplexer
void selectMuxChannel(uint8_t muxAddr, uint8_t channel) {
  if (channel > 7) return;

  Wire.beginTransmission(muxAddr);
  Wire.write(1 << channel);  // each bit enables one channel
  Wire.endTransmission();
}

// Debug helper: scan all channels on a mux for a specific I2C address
void scanMuxForAddress(uint8_t muxAddr, uint8_t addr) {
  Serial.print(F("Scanning mux 0x"));
  Serial.print(muxAddr, HEX);
  Serial.print(F(" for device 0x"));
  Serial.println(addr, HEX);

  for (uint8_t ch = 0; ch < 8; ch++) {
    selectMuxChannel(muxAddr, ch);
    delay(2);

    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print(F("  Found at channel "));
      Serial.println(ch);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=== ESP32-S3 Multi-Sensor System via I2C Multiplexers ==="));

  // Initialize I2C on the specified pins
  Wire.begin(SDA_PIN, SCL_PIN);  // SDA, SCL

  // Quick debug: scan muxes to locate BH1750 (0x23) and gas sensor (0x74)
  scanMuxForAddress(I2C_MUX1_ADDR, 0x23);
  scanMuxForAddress(I2C_MUX2_ADDR, 0x23);

  // ========== Initialize MS8607 (Temp/Humidity/Pressure) on mux1 channel 0 ==========
  Serial.println(F("\nInitializing MS8607 sensor (mux1 ch 0)..."));
  selectMuxChannel(I2C_MUX1_ADDR, MS8607_CHANNEL);
  delay(10);

  if (!ms8607.begin()) {
    Serial.println(F("Failed to find MS8607 chip. Check wiring and mux channel."));
  } else {
    Serial.println(F("MS8607 initialized successfully."));
  }

  // ========== Initialize MLX90614 IR temp sensor on mux1 channel 6 ==========
  Serial.println(F("\nInitializing MLX90614 sensor (mux1 ch 6)..."));
  selectMuxChannel(I2C_MUX1_ADDR, MLX90614_CHANNEL);
  delay(10);

  int mlxStatus = mlx90614.begin();
  if (mlxStatus != 0) {
    Serial.print(F("Failed to init MLX90614, status="));
    Serial.println(mlxStatus);
  } else {
    Serial.println(F("MLX90614 initialized successfully."));
  }

  // ========== Initialize BH1750 light sensor on mux2 channel 3 ==========
  Serial.println(F("\nInitializing BH1750 light sensor (mux2 ch 3)..."));
  selectMuxChannel(I2C_MUX2_ADDR, BH1750_CHANNEL);
  delay(10);

  // hp_BH1750: set address & init sensor, then calibrate timings once
  bh1750.begin(BH1750_TO_GROUND);
  bh1750.calibrateTiming();
  Serial.println(F("BH1750 (hp_BH1750) initialized and timings calibrated."));

  // ========== Initialize Multi-Gas (O2) sensor on mux1 channel 7 ==========
  Serial.println(F("\nInitializing Multi-Gas sensor (mux1 ch 7)..."));
  selectMuxChannel(I2C_MUX1_ADDR, GAS_SENSOR_CHANNEL);
  delay(10);

  while (!gas.begin()) {
    Serial.println(F("NO Gas Sensor detected!"));
    delay(1000);
  }
  Serial.println(F("Multi-Gas sensor connected successfully!"));

  // Configure gas sensor
  gas.changeAcquireMode(DFRobot_GAS::PASSIVITY);
  gas.setTempCompensation(DFRobot_GAS::ON);

  Serial.println(F("\n--- Setup complete. Starting measurements ---\n"));
}

void loop() {
  // ========== Read MS8607 (Temp/Humidity/Pressure) from mux1 channel 0 ==========
  selectMuxChannel(I2C_MUX1_ADDR, MS8607_CHANNEL);
  delay(5);

  sensors_event_t temp, pressure, humidity;
  ms8607.getEvent(&pressure, &temp, &humidity);

  // ========== Read MLX90614 IR temperatures from mux1 channel 6 ==========
  selectMuxChannel(I2C_MUX1_ADDR, MLX90614_CHANNEL);
  delay(5);

  float mlxObj = mlx90614.getObjectTempCelsius();

  // ========== Read BH1750 lux from mux2 channel 3 ==========
  selectMuxChannel(I2C_MUX2_ADDR, BH1750_CHANNEL);
  delay(5);
  // Blocking read with hp_BH1750: start measurement, then getLux()
  bh1750.start();
  float lux = bh1750.getLux();

  // ========== Read Multi-Gas (O2) sensor from mux1 channel 7 ==========
  selectMuxChannel(I2C_MUX1_ADDR, GAS_SENSOR_CHANNEL);
  delay(5);

  String gasType = gas.queryGasType();
  float gasConc  = gas.readGasConcentrationPPM();

  // ---- Print as a column/table view ----
  Serial.println(F("Sensor      Mux Ch Parameter    Value        Unit"));
  Serial.println(F("------------------------------------------------"));

  // MS8607 rows
  Serial.printf("%-10s %3d %2d  %-11s %8.2f     %s\n", "MS8607",  MUX1_INDEX, MS8607_CHANNEL,  "Temp",     temp.temperature,        "C");
  Serial.printf("%-10s %3d %2d  %-11s %8.2f     %s\n", "MS8607",  MUX1_INDEX, MS8607_CHANNEL,  "Humidity", humidity.relative_humidity, "%RH");
  Serial.printf("%-10s %3d %2d  %-11s %8.2f     %s\n", "MS8607",  MUX1_INDEX, MS8607_CHANNEL,  "Pressure", pressure.pressure,        "hPa");

  // MLX90614 row (IR object temp only)
  Serial.printf("%-10s %3d %2d  %-11s %8.2f     %s\n", "MLX90614", MUX1_INDEX, MLX90614_CHANNEL, "ObjTemp", mlxObj, "C");

  // BH1750 row (lux)
  Serial.printf("%-10s %3d %2d  %-11s %8.2f     %s\n", "BH1750",  MUX2_INDEX, BH1750_CHANNEL,  "Lux",     lux, "lx");

  // Multi-gas row (e.g. O2)
  const char *unit = (gasType == "O2") ? "%vol" : "PPM";
  Serial.printf("%-10s %3d %2d  %-11s %8.2f     %s\n", "MultiGas", MUX1_INDEX, GAS_SENSOR_CHANNEL, gasType.c_str(), gasConc, unit);

  Serial.println();

  delay(2000);  // read every 2 seconds
}
