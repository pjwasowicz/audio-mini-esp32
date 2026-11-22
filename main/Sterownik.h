#pragma once
// Sterownik całości – odpowiednik enkoder.py + wyswietlacz.py z Pi:
//  - głośność PT2258 wspólna z telefonem (Spotify) i panelem WWW, zapamiętana (po starcie najwyżej 50 %),
//  - wejścia Spotify/TV/Phono na przekaźnikach, menu wejść na gałce (plus „CONF” – konfiguracja Wi-Fi),
//    start grania Spotify -> wejście 1,
//    wybór innego wejścia -> pauza Spotify,
//  - wyświetlacz 4 znaki 14-seg: przewijany tytuł i wykonawca, „V 42” przy zmianie głośności, zegar w spoczynku.
#include <cstdint>
#include <functional>
#include <string>

namespace sterownik {

struct Utwor {
  std::string tytul, wykonawca, album, okladka;
  uint32_t dlugosc_ms = 0;
};

// polecenia do odtwarzacza Spotify (ustawiane po zalogowaniu)
struct Spotify {
  std::function<void(bool)> pauza;      // true = pauza
  std::function<void()> nastepny, poprzedni;
  std::function<void(int)> glosnosc;    // 0..65535
};

void init();                                        // sprzęt, PT2258, wyświetlacz, zapisane ustawienia, zadania
void stan_sieci(const std::string& opis);           // np. „łączę z Wi-Fi” – pokazywane w spoczynku
void na_konfiguracje(std::function<void()> f);      // wybór „CONF” w menu gałki
void tryb_konfiguracji(const std::string& tekst);   // wyświetlacz przewija tylko ten tekst
void blad_wifi(const std::string& opis);
void postep(int proc);                              // wgrywanie programu przez Wi-Fi: „U 45”; −1 = koniec            // np. „BAD WIFI PASSWORD”; "" = połączone

// Spotify -> sterownik
void spotify_polaczony(const Spotify& s);
void spotify_glosnosc(int v);                        // 0..65535 (z telefonu)
void spotify_gra(bool gra);
void spotify_utwor(const Utwor& u);
void spotify_pozycja(uint32_t ms);                   // pozycja odtwarzania (z odtwarzacza)
void spotify_nieaktywny();                           // inne urządzenie przejęło odtwarzanie

// panel WWW
std::string json_status();
std::string json_wejscie();
std::string json_system();
void ustaw_glosnosc(int proc);
std::string json_strojenie();                        // kolano krzywej
void ustaw_kolano(int db);                           // 20–55 dB
void ustaw_wejscie(int nr);
void odtwarzanie(const std::string& polecenie);      // playpause, next, prev

}  // namespace sterownik
