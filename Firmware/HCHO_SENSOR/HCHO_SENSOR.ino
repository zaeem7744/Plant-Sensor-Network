#include "DFRobotHCHOSensor.h"

// === ESP32-S3 + DFRobot HCHO (single "S" pin) in UART mode ===
// Board pins (on the sensor):
//   VCC -> 5V
//   GND -> GND
//   S   -> data pin
// The button/jumper on the board selects whether S is DAC (analog voltage)
// or UART (digital serial). Here we use UART mode.
//
// IMPORTANT: the sensor usually runs at 5V, so S will swing to 5V.
// ESP32-S3 GPIO are 3.3V only. Use a 5V->3.3V level shifter or
// a resistor divider between S and the ESP32-S3 RX pin.

#define HCHO_RX_PIN  5   // ESP32-S3 GPIO receiving data from sensor S

// Use a dedicated hardware UART (UART1 here) on the ESP32-S3
HardwareSerial HCHO_Serial(1);

// Create HCHO sensor object using hardware serial
DFRobotHCHOSensor hcho(&HCHO_Serial);

void setup() {
  // USB serial for debug / readings
  Serial.begin(115200);
  delay(1000);

  // Initialize UART used by the HCHO sensor
  // Baud rate for the DFRobot Gravity HCHO sensor is 9600 8N1
  // Only RX pin is actually wired; TX is set to -1 (unused)
  HCHO_Serial.begin(9600, SERIAL_8N1, HCHO_RX_PIN, -1);

  Serial.println("DFRobot HCHO sensor (UART mode) on ESP32-S3 (single S pin)");
  Serial.println("Wire: S -> level shifter/divider -> GPIO5, VCC->5V, GND->GND.");
}

void loop() {
  // Check if a complete frame has been received from the sensor
  if (hcho.available()) {
    float ppm = hcho.uartReadPPM();

    Serial.print("HCHO concentration: ");
    Serial.print(ppm, 3);   // print with 3 decimals
    Serial.println(" ppm");
  }

  // Polling interval – sensor updates about once per second
  delay(200);
}
