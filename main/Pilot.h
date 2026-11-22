#pragma once
// Pilot na podczerwień: odbiornik VS1838 na ZAP2 (GPIO5), protokół NEC, dekodowanie w sprzętowym RMT.
// Zdarzenie: kod przycisku (bajt polecenia NEC) i czy to powtórzenie przytrzymanego przycisku.
#include <cstdint>
#include <functional>

namespace pilot {

using Zdarzenie = std::function<void(uint8_t kod, bool powtorzenie)>;
void start(Zdarzenie z);

}  // namespace pilot
