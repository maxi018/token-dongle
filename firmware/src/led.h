#pragma once
// LED RGB del T-Dongle-C5: un APA102 (clock + data), manejado por bit-bang.
// Trama APA102: 32 bits en 0, luego por LED [0xE0|brillo(5b), B, G, R], luego fin.

#include <Arduino.h>

class Apa102Led {
 public:
  Apa102Led(uint8_t dataPin, uint8_t clockPin) : _di(dataPin), _ci(clockPin) {}

  void begin() {
    pinMode(_di, OUTPUT);
    pinMode(_ci, OUTPUT);
    digitalWrite(_di, LOW);
    digitalWrite(_ci, LOW);
    set(0, 0, 0, 0);
  }

  // brightness: 0..31 (registro global de 5 bits del APA102)
  void set(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    for (int i = 0; i < 4; i++) writeByte(0x00);  // start frame
    writeByte(0xE0 | (brightness & 0x1F));
    writeByte(b);
    writeByte(g);
    writeByte(r);
    for (int i = 0; i < 4; i++) writeByte(0xFF);  // end frame
  }

 private:
  uint8_t _di, _ci;

  void writeByte(uint8_t v) {
    for (int bit = 7; bit >= 0; bit--) {
      digitalWrite(_di, (v >> bit) & 1);
      digitalWrite(_ci, HIGH);
      digitalWrite(_ci, LOW);
    }
  }
};
