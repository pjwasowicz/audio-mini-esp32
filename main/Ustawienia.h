#pragma once
// Ustawienia w pamięci flash (NVS): głośność, wejście, dane logowania Spotify (token, nie hasło).
#include <string>

namespace ustawienia {

void init();                         // nvs_flash_init (z czyszczeniem przy zmianie formatu)
int glosnosc(int domyslna);
void zapisz_glosnosc(int proc);
int wejscie();
void zapisz_wejscie(int nr);
std::string login();                 // JSON LoginBlob (username, authType, authData) albo ""
void zapisz_login(const std::string& json);
// sieć Wi-Fi ustawiona w trybie konfiguracji (pierwszeństwo przed ustawieniami z kompilacji); false = brak
bool wifi(std::string& ssid, std::string& haslo);
void zapisz_wifi(const std::string& ssid, const std::string& haslo);
// tryb konfiguracji Wi-Fi przy następnym starcie (z menu gałki); odczyt kasuje znacznik
void ustaw_tryb_konfig();
int liczba(const char* klucz, int domyslna);         // strojenie (korekta wejść, kolano krzywej)
void zapisz_liczbe(const char* klucz, int v);
bool tryb_konfig();

}  // namespace ustawienia
