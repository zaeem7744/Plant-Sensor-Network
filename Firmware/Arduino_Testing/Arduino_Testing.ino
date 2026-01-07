  #include <Wire.h>
  #include "DFRobot_MultiGasSensor.h"

  // ESP32 I2C pins
  #define SDA_PIN 8
  #define SCL_PIN 9

  // Optional debug LED
  #define LED_PIN 19

  // Create a single sensor object (we change I2C address per sensor)
  DFRobot_GAS_I2C gas(&Wire, 0x77);

  // Structure to hold each sensor's MUX info
  struct SensorInfo {
    uint8_t muxAddr;
    uint8_t channel;
    uint8_t i2cAddr;
  };

  // List of your sensors (from your scan)
  SensorInfo sensors[] = {
    {0x70, 0, 0x23},
    {0x70, 1, 0x40},
    {0x70, 1, 0x76},
    {0x70, 5, 0x34},
    {0x70, 6, 0x74},
    {0x70, 7, 0x75},
    {0x71, 4, 0x74}, // multigas
    {0x71, 5, 0x74}, // multigas
    {0x71, 6, 0x74}, // multigas
    {0x71, 7, 0x74}  // multigas
  };

  // Select MUX channel
  void selectMuxChannel(uint8_t muxAddr, uint8_t channel) {
    Wire.beginTransmission(muxAddr);
    Wire.write(1 << channel);
    Wire.endTransmission();
    delay(5);
  }

  void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println("=== Multi-Gas Sensor Readings ===");
  }

  void loop() {
    for (uint8_t i = 0; i < sizeof(sensors)/sizeof(sensors[0]); i++) {
      SensorInfo s = sensors[i];

      selectMuxChannel(s.muxAddr, s.channel);
      gas.setI2cAddr(s.i2cAddr);

      if (gas.begin()) {
        String type = gas.queryGasType();
        float ppm = gas.readGasConcentrationPPM();

        Serial.print("Sensor ");
        Serial.print(i);
        Serial.print(" (");
        Serial.print(type);
        Serial.print(") -> ");
        Serial.print(ppm);
        Serial.println(" PPM");
      } else {
        Serial.print("Sensor ");
        Serial.print(i);
        Serial.println(" not detected!");
      }

      delay(50);
    }

    Serial.println("----- End of Readings -----\n");
    delay(2000); // read every 2 seconds
  }
