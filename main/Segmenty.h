#pragma once
// Wyświetlacz 4 znaki 14-segmentowe LED na HT16K33 (I2C 0x70, moduł zgodny z Adafruit 0.54" alfanumerycznym).
// Litery tylko wielkie; polskie i inne akcenty -> litera bazowa. Kropka po znaku zapala kropkę tego znaku
// ("12.34" = 4 znaki), więc tekst może mieć więcej niż 4 znaki UTF-8.
#include <string>

#include "driver/i2c_master.h"

class Segmenty {
 public:
  bool start(i2c_master_bus_handle_t bus);
  void pokaz(const std::u32string& tekst);   // pierwsze 4 znaki (z kropkami), reszta pominięta
  void jasnosc(int poziom);                  // 0–15
  void miganie(bool wl);                     // całym wyświetlaczem, 1 Hz
  bool ok() const { return ok_; }

 private:
  bool komenda(uint8_t c);

  i2c_master_dev_handle_t dev_ = nullptr;
  bool ok_ = false;
  uint16_t poprzednie_[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};   // ostatnio wysłane (bez zmian = bez transmisji)
  int jasnosc_ = -1;
  int miganie_ = -1;
};
