#pragma once
// Dziennik resetów w NVS: przy każdym starcie przyczyna poprzedniego resetu (zasilanie, spadek napięcia,
// błąd programu, watchdog, restart programu z powodem) i ile program wtedy działał. Ostatnie 10 wpisów.
#include <string>

namespace dziennik {

void start();                        // po ustawienia::init(): dopisuje wpis o poprzednim resecie
void powod(const char* opis);        // przed esp_restart(): powód zapamiętany w pamięci RTC do następnego startu
std::string json();                  // [{"przyczyna":..,"powod":..,"dzialal_s":..}, ...] od najnowszego

}  // namespace dziennik
