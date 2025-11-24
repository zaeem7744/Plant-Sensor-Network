#ifndef SOFTWARESERIAL_H
#define SOFTWARESERIAL_H

#include <Arduino.h>

// Minimal stub of SoftwareSerial so libraries that include it
// can compile on platforms (like ESP32-S3) where it is not provided
// by the core. We are not actually using SoftwareSerial on ESP32.
class SoftwareSerial {
public:
  SoftwareSerial(uint8_t rxPin, uint8_t txPin) {}

  void begin(unsigned long baud) {}

  int available() { return 0; }

  int read() { return -1; }

  size_t write(uint8_t b) { (void)b; return 1; }

  // Buffer write overload used by some libraries
  size_t write(const uint8_t *buffer, size_t size) {
    (void)buffer;
    return size;
  }
};

#endif // SOFTWARESERIAL_H
