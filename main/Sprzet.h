#pragma once

// Sprzęt płytki input_selector: magistrala I2C (PT2258 0x44, HT16K33 0x70), przekaźniki wejść K1–K3,
// impulsator EC11 (sprzętowy licznik PCNT) i jego przycisk.
#include <string>

#include "driver/i2c_master.h"

namespace sprzet {

// wejscie: przekaźnik włączany od razu przy starcie (po cichym restarcie programu bez przerwy w styku)
// cichy = restart programu (przekaźniki zamrożone włączone, zostają); inaczej wszystkie wyłączone
void init(int wejscie, bool cichy);
i2c_master_bus_handle_t i2c();
const std::string& i2c_adresy();   // adresy znalezione przy starcie

// 0..2 = włączony przekaźnik danego wejścia (pozostałe wyłączone najpierw), -1 = wszystkie wyłączone
void przekaznik(int wejscie);

// przekaźnik głośników (ZAP1)
void glosniki(bool wl);

// pełne ząbki gałki od poprzedniego wywołania (+ = głośniej), z uwzględnieniem CONFIG_ENC_KIERUNEK
int zabki();

// zamrożenie stanu przekaźników (wejścia i głośniki) na czas restartu programu (esp_restart) – bez puszczania styków
void zamroz();

// true raz na każde wciśnięcie przycisku gałki (z odrzuceniem drgań styków)
bool wcisniety();

}  // namespace sprzet
