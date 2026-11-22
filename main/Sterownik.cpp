#include "Sterownik.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <vector>

#include "Segmenty.h"
#include "SpotifyPlayer.h"
#include "Pilot.h"
#include "Pt2258.h"
#include "Sprzet.h"
#include "Ustawienia.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace sterownik {

static const char* TAG = "sterownik";

static const char* NAZWY[3] = {"Spotify", "TV", "Phono"};
static constexpr int GLOS_START = 30;        // pierwsze uruchomienie
static constexpr int GLOS_MAX_START = 50;    // po włączeniu nie głośniej niż tyle
static constexpr int KROK_SZYBKI_DB = 3;     // ząbek gałki = 1 dB, przy szybkim kręceniu 3 dB
static constexpr int64_t SZYBKO_MS = 40;     // ząbki częściej niż co tyle ms = szybkie kręcenie
static constexpr int64_t MENU_MS = 8000;     // menu znika po tylu ms bez ruchu
static constexpr int64_t SPOTIFY_PO_MS = 400;    // głośność do telefonu dopiero, gdy gałka stoi
static constexpr int64_t SPOKOJ_MS = 1500;       // po ruchu gałką ignoruj głośność z telefonu
static constexpr int64_t ZAPIS_MS = 2000;        // zapis głośności do flash po tylu ms bez zmian
static constexpr int64_t PRZEWIJANIE_MS = 300;   // przesunięcie tytułu o jeden znak
static constexpr int64_t POKAZ_WEJ_MS = 3000;    // nazwa wejścia po jego zmianie
static constexpr int64_t BEZCZYNNOSC_MS = 60000; // po tylu ms pauzy zamiast tytułu zegar
static constexpr int64_t POKAZ_GLOS_MS = 5000;   // pasek zamiast nazwy wejścia po zmianie głośności
static constexpr int64_t GLOSNIKI_PO_MS = 2000;  // głośniki dopiero po tylu ms od startu / zmiany wejścia
static constexpr int64_t PT_GOTOWY_MS = 1500;    // od pierwszej odpowiedzi PT2258 do wysłania ustawień
static constexpr int64_t WYLACZ_WEJSCIE_MS = 3LL * 3600 * 1000;   // TV/Phono bez akcji tyle ms = odłączone
static constexpr int64_t USYPIANIE_MS = 30000;   // wejście Spotify bez grania tyle ms = czuwanie

static std::recursive_mutex mtx;
static Pt2258 pt;
static Segmenty seg;
static Spotify spotify;
static bool spotify_ok = false;

static int glos = GLOS_START, wejscie = 0;
// stałe tłumienie kanałów PT2258 dla wejść (DAC daje ok. 2 V, TV/gramofon mniej); kolano krzywej z panelu
static constexpr int KOREKTA[3] = {78, 0, 0};
static int kolano = 36;
static bool menu = false;
static int wybor = 0;                         // pozycja menu: 0–2 wejścia, MENU_KONFIG = konfiguracja Wi-Fi
static constexpr int MENU_KONFIG = 3, POZYCJI_MENU = 4;
static std::function<void()> konfiguracja;
static std::string konfig_tekst;              // niepusty = tryb konfiguracji Wi-Fi
static std::string wifi_blad;                 // niepusty = brak połączenia z Wi-Fi (przyczyna)
static int64_t wifi_blad_od = 0;
static int postep_ota = -1;                   // ≥ 0: trwa wgrywanie programu, procent
static constexpr int64_t WIFI_BLAD_MS = 8000;    // komunikat dopiero po tylu ms bez sieci
static int64_t menu_ruch = 0, ost_zmiana = -100000, zmiana_glos = -100000, zmiana_wej = -100000, zapis_glos = 0;
static bool glosniki = false;
static int64_t glosniki_za = 0;               // kiedy włączyć głośniki (0 = nic nie czeka)
static bool wylaczone = false;                // TV/Phono odłączone po 3 h bez akcji (włącza 2/3 na pilocie)
static int64_t ostatnia_akcja = 0;            // gałka, pilot, panel – nie telefon i nie Spotify
static bool czuwanie = false;                 // cisza na wejściu Spotify: głośniki odłączone, zegar
static int64_t cisza_od = 0;                  // od kiedy Spotify (na swoim wejściu) nie gra
static int do_spotify = -1;
static int przed_wyciszeniem = 0;           // głośność do przywrócenia przyciskiem EQ pilota
static int ostatni_ir = -1;                   // ostatni kod z pilota (do /api/system)                   // głośność czekająca na wysłanie do telefonu
static bool gra = false;
static int64_t pauza_od = 0;
static Utwor utwor;
static bool ma_utwor = false;
static std::string siec = "start...";
static std::atomic<uint32_t> pozycja_ms{0};
static std::atomic<int64_t> pozycja_t{0};

static int64_t teraz_ms() { return esp_timer_get_time() / 1000; }

// ── UTF-8 ────────────────────────────────────────────────────────────────────────────────────────────
static std::u32string u32(const std::string& s) {
  std::u32string out;
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!n || i + n > s.size()) { i++; continue; }
    char32_t cp = n == 1 ? c : c & (0x7F >> n);
    for (int k = 1; k < n; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    out.push_back(cp);
    i += n;
  }
  return out;
}

static std::string json_tekst(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o + "\"";
}

// ── głośność i wejścia (wywoływane pod blokadą) ──────────────────────────────────────────────────────
static void zmien_glos(int nowa, bool z_telefonu) {
  nowa = std::clamp(nowa, 0, 100);
  if (nowa == glos) return;
  ESP_LOGI(TAG, "głośność %d -> %d (%s)", glos, nowa, z_telefonu ? "telefon" : "gałka/WWW");
  glos = nowa;
  pt.glosnosc(glos);
  int64_t t = teraz_ms();
  zmiana_glos = t;
  zapis_glos = t + ZAPIS_MS;
  if (!z_telefonu) {
    ost_zmiana = t;
    ostatnia_akcja = t;
    do_spotify = glos;
  }
}

// głośność o db_krokow poziomów PT2258 w górę/dół (kier = ±1): przy cichym słuchaniu krok to kilka %,
// ale zawsze 1 dB
static int glos_o_db(int od, int kier, int db_krokow) {
  int nowa = od;
  for (int i = 0; i < db_krokow; i++) {
    int db = Pt2258::db_z_procent(nowa);
    while (nowa + kier >= 0 && nowa + kier <= 100) {
      nowa += kier;
      if (Pt2258::db_z_procent(nowa) != db) break;
    }
  }
  return nowa;
}

// zwraca true, gdy trzeba zatrzymać Spotify (wywołujący robi to już bez blokady)
static bool zmien_wejscie(int nr) {
  if (nr < 0 || nr > 2 || (nr == wejscie && !wylaczone)) return false;   // odłączone: to samo wejście włącza
  wejscie = nr;
  wylaczone = false;
  czuwanie = false;
  zmiana_wej = ostatnia_akcja = teraz_ms();
  if (glosniki) {                       // najpierw odłączone głośniki, dopiero potem przełączanie
    sprzet::glosniki(false);
    glosniki = false;
    vTaskDelay(pdMS_TO_TICKS(50));      // styk przekaźnika głośników puszcza
  }
  sprzet::przekaznik(nr);
  pt.kanaly(KOREKTA[nr]);              // poziom nowego wejścia, głośniki jeszcze odłączone
  glosniki_za = teraz_ms() + GLOSNIKI_PO_MS;
  ustawienia::zapisz_wejscie(nr);
  ESP_LOGI(TAG, "wejście %d: %s", nr + 1, NAZWY[nr]);
  return nr != 0 && gra && spotify_ok;
}

// Czuwanie: głośniki odłączone, na wyświetlaczu zegar; PT2258 i część analogowa zostają zasilone.
// Wywoływane pod blokadą.
static void usypiaj() {
  if (glosniki) {
    sprzet::glosniki(false);
    glosniki = false;
  }
  glosniki_za = 0;
  czuwanie = true;
  ESP_LOGI(TAG, "cisza na wejściu Spotify: czuwanie, głośniki odłączone");
}

static void budz() {
  czuwanie = false;
  glosniki_za = teraz_ms() + GLOSNIKI_PO_MS;
  ESP_LOGI(TAG, "koniec czuwania");
}

// po włączeniu zasilania: czekanie, aż PT2258 odpowie i jego zasilanie się ustali, potem pełne ustawienie
static void start_pt() {
  bool jest = false;
  for (int i = 0; i < 20 && !jest; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
    jest = i2c_master_probe(sprzet::i2c(), 0x44, 20) == ESP_OK;
  }
  if (jest) {
    vTaskDelay(pdMS_TO_TICKS(PT_GOTOWY_MS));
    pt.start(sprzet::i2c(), glos);             // czyszczenie rejestrów, sam czeka jeszcze 200 ms
  }
  // bez odpowiedzi pt.ok() zostaje false: nadzór w zadaniu gałki ustawi go, gdy się pojawi
  if (!pt.ok()) ESP_LOGW(TAG, "PT2258 nie odpowiada, głośniki czekają");
}

// ── zadanie gałki: ząbki, przycisk, menu, głośność do telefonu, zapis ─────────────────────────────────
static void zadanie_galki(void*) {
  int64_t ost_zabek = 0, pt_proba = 0, pt_widac_od = 0;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10));
    int z = sprzet::zabki();
    bool klik = sprzet::wcisniety();
    bool pauzuj = false, konfig = false;
    int wyslij = -1;
    {
      std::lock_guard<std::recursive_mutex> l(mtx);
      int64_t t = teraz_ms();
      if (z) {
        if (menu) {
          wybor = ((wybor + z) % POZYCJI_MENU + POZYCJI_MENU) % POZYCJI_MENU;
          menu_ruch = t;
        } else {
          int db_krokow = std::abs(z) * ((t - ost_zabek < SZYBKO_MS || z > 1 || z < -1) ? KROK_SZYBKI_DB : 1);
          zmien_glos(glos_o_db(glos, z > 0 ? 1 : -1, db_krokow), false);
        }
        ost_zabek = t;
      }
      if (klik) {
        if (!menu) {
          menu = true;
          wybor = wejscie;
          menu_ruch = t;
        } else {
          menu = false;
          if (wybor == MENU_KONFIG) konfig = true;
          else pauzuj = zmien_wejscie(wybor);
        }
      }
      if (menu && t - menu_ruch > MENU_MS) menu = false;
      if (z || klik) ostatnia_akcja = t;
      if (wylaczone) {
        // czeka na wybór wejścia (2/3 na pilocie, gałka, panel)
      } else if (wejscie != 0 && t - ostatnia_akcja > WYLACZ_WEJSCIE_MS) {
        if (glosniki) {
          sprzet::glosniki(false);
          glosniki = false;
          vTaskDelay(pdMS_TO_TICKS(50));
        }
        glosniki_za = 0;
        sprzet::przekaznik(-1);
        wylaczone = czuwanie = true;
        ESP_LOGI(TAG, "%s bez akcji przez 3 h: wejście odłączone", NAZWY[wejscie]);
      } else if (wejscie != 0 || gra) {
        cisza_od = 0;
        if (czuwanie) budz();
      } else if (!cisza_od) {
        cisza_od = t;
      } else if (!czuwanie && t - cisza_od > USYPIANIE_MS) {
        usypiaj();
      }
      if (glosniki_za && t >= glosniki_za && pt.ok()) {   // bez działającego PT2258 głośniki czekają
        // przed dołączeniem głośników pełny komplet ustawień PT2258: zapis tuż po przełączeniu przekaźnika
        // mógł dojść przekłamany (zakłócenia od cewki), a wtedy po przełączeniu było bardzo głośno
        // dwa razy (odpowiedź PT2258 + pełny zapis) w odstępie 100 ms; głośniki tylko gdy oba się udały
        bool dobrze = true;
        for (int i = 0; i < 2 && dobrze; i++) {
          if (i) vTaskDelay(pdMS_TO_TICKS(100));
          dobrze = i2c_master_probe(sprzet::i2c(), 0x44, 20) == ESP_OK && pt.odswiez(glos);
        }
        if (dobrze) {
          glosniki_za = 0;
          glosniki = true;
          sprzet::glosniki(true);
          ESP_LOGI(TAG, "głośniki włączone");
        } else {
          glosniki_za = t + 1000;               // ponowna próba za sekundę
          ESP_LOGW(TAG, "PT2258 nie potwierdził ustawień, głośniki czekają");
        }
      }
      if (do_spotify >= 0 && t - ost_zmiana > SPOTIFY_PO_MS && spotify_ok) {
        wyslij = do_spotify;
        do_spotify = -1;
      }
      if (zapis_glos && t > zapis_glos) {
        zapis_glos = 0;
        ustawienia::zapisz_glosnosc(glos);
      }
      // nadzór zasilania analogu (może je odciąć przycisk na panelu): PT2258 odpowiada = jest zasilanie
      if (t > pt_proba) {
        pt_proba = t + 1000;
        bool jest = i2c_master_probe(sprzet::i2c(), 0x44, 20) == ESP_OK;
        if (!jest) pt_widac_od = 0;
        else if (!pt_widac_od) pt_widac_od = t;
        if (!jest && (pt.ok() || glosniki || glosniki_za)) {
          sprzet::glosniki(false);
          glosniki = false;
          glosniki_za = 0;
          pt.bez_zasilania();
          ESP_LOGW(TAG, "PT2258 nie odpowiada (brak zasilania analogu): głośniki odłączone");
        } else if (jest && !pt.ok() && t - pt_widac_od >= PT_GOTOWY_MS) {   // zasilanie wróciło albo błąd zapisu
          // start od zera czyści rejestry (chwilowo 0 dB): głośniki najpierw odłączone
          if (glosniki) {
            sprzet::glosniki(false);
            glosniki = false;
            vTaskDelay(pdMS_TO_TICKS(50));
          }
          if (pt.start(sprzet::i2c(), glos) && !glosniki && !czuwanie) glosniki_za = t + GLOSNIKI_PO_MS;
        }
      }
    }
    // polecenia do Spotify bez blokady (cspot może od razu wywołać nasze zdarzenia)
    if (pauzuj && spotify.pauza) spotify.pauza(true);
    if (konfig && konfiguracja) konfiguracja();   // restart do trybu konfiguracji Wi-Fi
    if (wyslij >= 0 && spotify.glosnosc) spotify.glosnosc(wyslij * 65535 / 100);
  }
}

// ── zadanie wyświetlacza (4 znaki) ────────────────────────────────────────────────────────────────────
// menu: migająca nazwa wybieranego wejścia; po zmianie głośności „V 42”, po zmianie wejścia jego nazwa;
// Spotify gra: przewijany tytuł i wykonawca; poza tym zegar (przyciemniony w czuwaniu i bez PT2258)
static const char* const KROTKIE[POZYCJI_MENU] = {"SPOT", " TV ", "PHON", "CONF"};

static void zadanie_wyswietlacza(void*) {
  std::u32string tasma;                 // przewijany tekst utworu
  std::string utwor_id;
  size_t poz = 0;
  int64_t nast_krok = 0, seg_proba = 0;
  const int64_t wersja_do = teraz_ms() + 3000;   // po starcie numer wersji, np. „ V1.0”
  const std::u32string wersja = u32(std::string(" V") + esp_app_get_description()->version);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(50));
    std::u32string tekst;
    bool migaj = false;
    int jasno = 8;
    {
      std::lock_guard<std::recursive_mutex> l(mtx);
      int64_t t = teraz_ms();
      if (gra || !ma_utwor) pauza_od = 0;
      else if (!pauza_od) pauza_od = t;
      bool bez_analogu = czuwanie || !pt.ok();   // czuwanie albo analog odcięty przyciskiem
      if (bez_analogu) jasno = 1;
      if (t < wersja_do && konfig_tekst.empty()) {
        tekst = wersja;
      } else if (postep_ota >= 0) {
        char b[16];
        snprintf(b, sizeof b, "U%3d", postep_ota);
        tekst = u32(b);
      } else if (menu) {
        tekst = u32(KROTKIE[wybor]);
        migaj = true;
      } else if (t - zmiana_glos < POKAZ_GLOS_MS) {
        char b[8];
        if (glos == 0) snprintf(b, sizeof b, "MUTE");
        else snprintf(b, sizeof b, "V%3d", glos);
        tekst = u32(b);
      } else if (t - zmiana_wej < POKAZ_WEJ_MS) {
        tekst = u32(KROTKIE[wejscie]);
      } else if (!konfig_tekst.empty() || (!wifi_blad.empty() && t - wifi_blad_od > WIFI_BLAD_MS) ||
                 (wejscie == 0 && ma_utwor && gra && !bez_analogu)) {
        // komunikat (konfiguracja, brak Wi-Fi) albo tytuł utworu
        std::string komunikat = !konfig_tekst.empty() ? konfig_tekst
                            : !wifi_blad.empty()  ? "    " + wifi_blad + "    "
                                                  : "";
        std::string id = !komunikat.empty() ? komunikat : utwor.tytul + "\x1f" + utwor.wykonawca;
        if (id != utwor_id) {                 // nowy tekst: przewijanie od początku
          utwor_id = id;
          tasma = !komunikat.empty()
                      ? u32(komunikat)
                      : u32("    " + utwor.tytul + (utwor.wykonawca.empty() ? "" : " - " + utwor.wykonawca) + "    ");
          poz = 0;
          nast_krok = t + PRZEWIJANIE_MS;
        } else if (t >= nast_krok) {
          poz = (poz + 1) % (tasma.size() - 4);
          nast_krok = t + PRZEWIJANIE_MS;
        }
        tekst = tasma.substr(poz, 4);
      } else {
        utwor_id.clear();
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        bool znacznik = wejscie != 0 && !wylaczone;   // TV/Phono: wejście widoczne przy zegarze
        if (znacznik && t % 10000 >= 8000) {
          tekst = u32(KROTKIE[wejscie]);              // co 10 s na 2 s pełna nazwa
        } else if (lt.tm_year + 1900 >= 2025) {
          char b[16];
          snprintf(b, sizeof b, "%2d%02d", lt.tm_hour, lt.tm_min);
          tekst = u32(b);
          // kropka-znacznik na stałe: TV po ostatniej cyfrze, Phono po pierwszej
          if (znacznik && wejscie == 1) tekst.push_back(U'.');
          // kropka po godzinie miga co sekundę jak dwukropek
          if (lt.tm_sec % 2 == 0) tekst.insert(2, 1, U'.');
          if (znacznik && wejscie == 2) tekst.insert(1, 1, U'.');
        } else {
          tekst = u32(wejscie == 0 && !spotify_ok ? "----" : KROTKIE[wejscie]);
        }
      }
      if (!seg.ok() && t > seg_proba) {
        seg_proba = t + 5000;
        seg.start(sprzet::i2c());
      }
    }
    seg.pokaz(tekst);
    seg.jasnosc(jasno);
    seg.miganie(migaj);
  }
}

// ── pilot (21 przycisków, NEC) ────────────────────────────────────────────────────────────────────────
namespace ir {
constexpr uint8_t CH_MINUS = 0x45, CH = 0x46, CH_PLUS = 0x47, POPRZEDNI = 0x44, NASTEPNY = 0x40, PAUZA = 0x43,
                  MINUS = 0x07, PLUS = 0x15, EQ = 0x09, K1 = 0x0C, K2 = 0x18, K3 = 0x5E;
}

static void przycisk_pilota(uint8_t kod, bool powtorzenie) {
  bool pauzuj = false;
  std::string polecenie;
  {
    std::lock_guard<std::recursive_mutex> l(mtx);
    ostatni_ir = kod;
    ostatnia_akcja = teraz_ms();
    if (powtorzenie && kod != ir::PLUS && kod != ir::MINUS) return;   // przytrzymanie tylko dla głośności
    menu = false;
    switch (kod) {
      case ir::PLUS:
      case ir::MINUS:
        zmien_glos(glos_o_db(glos, kod == ir::PLUS ? 1 : -1, 1), false);
        break;
      case ir::EQ:                        // wyciszenie i powrót do poprzedniej głośności
        if (glos > 0) {
          przed_wyciszeniem = glos;
          zmien_glos(0, false);
        } else if (przed_wyciszeniem > 0) {
          zmien_glos(przed_wyciszeniem, false);
        }
        break;
      case ir::CH_MINUS:
      case ir::CH_PLUS:
        pauzuj = zmien_wejscie((wejscie + (kod == ir::CH_PLUS ? 1 : 2)) % 3);
        break;
      case ir::K1: pauzuj = zmien_wejscie(0); break;
      case ir::K2: pauzuj = zmien_wejscie(1); break;
      case ir::K3: pauzuj = zmien_wejscie(2); break;
      case ir::PAUZA: polecenie = "playpause"; break;
      case ir::POPRZEDNI: polecenie = "prev"; break;
      case ir::NASTEPNY: polecenie = "next"; break;
      default: break;
    }
  }
  // polecenia do Spotify bez blokady (jak w zadaniu gałki)
  if (pauzuj && spotify.pauza) spotify.pauza(true);
  if (!polecenie.empty()) odtwarzanie(polecenie);
}

// ── API ─────────────────────────────────────────────────────────────────────────────────────────────
void init() {
  bool cichy = esp_reset_reason() == ESP_RST_SW;    // restart programu (zmiana konta, wznowienie sieci)
  {
    std::lock_guard<std::recursive_mutex> l(mtx);
    glos = ustawienia::glosnosc(GLOS_START);
    if (!cichy) glos = std::min(glos, GLOS_MAX_START);   // limit tylko po włączeniu zasilania
    wejscie = std::clamp(ustawienia::wejscie(), 0, 2);
    kolano = ustawienia::liczba("kolano", kolano);
    Pt2258::ustaw_kolano(kolano);
    pt.kanaly(KOREKTA[wejscie]);          // układ jeszcze nie ustawiony: wartość dla startu
  }
  // po cichym restarcie wszystko zostaje włączone (przekaźniki były zamrożone), PT2258 pamięta ustawienia
  glosniki = cichy;
  sprzet::init(wejscie, cichy);
  seg.start(sprzet::i2c());
  seg.pokaz(u32(cichy ? "...." : "----"));
  if (cichy) {
    pt.start(sprzet::i2c(), glos, false);
  } else {
    // po włączeniu: chwila na ustalenie zasilania, potem po kolei PT2258 (z czekaniem na odpowiedź),
    // przekaźnik wejścia, a głośniki po GLOSNIKI_PO_MS – mniejsze skoki prądu przy starcie
    vTaskDelay(pdMS_TO_TICKS(1000));
    start_pt();
    vTaskDelay(pdMS_TO_TICKS(200));
    sprzet::przekaznik(wejscie);
    glosniki_za = teraz_ms() + GLOSNIKI_PO_MS;
  }
  ESP_LOGI(TAG, "wejście %d (%s), głośność %d %%", wejscie + 1, NAZWY[wejscie], glos);
  pilot::start(przycisk_pilota);
  xTaskCreatePinnedToCore(zadanie_galki, "galka", 6144, nullptr, 6, nullptr, 0);
  xTaskCreatePinnedToCore(zadanie_wyswietlacza, "wyswietlacz", 4096, nullptr, 3, nullptr, 0);
}

void na_konfiguracje(std::function<void()> f) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  konfiguracja = std::move(f);
}

void tryb_konfiguracji(const std::string& tekst) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  konfig_tekst = tekst;
}

void postep(int proc) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  postep_ota = proc;
}

void blad_wifi(const std::string& opis) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (opis.empty()) wifi_blad.clear();
  else {
    if (wifi_blad.empty()) wifi_blad_od = teraz_ms();
    wifi_blad = opis;
  }
}

void stan_sieci(const std::string& opis) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  siec = opis;
}

void spotify_polaczony(const Spotify& s) {
  int v;
  {
    std::lock_guard<std::recursive_mutex> l(mtx);
    spotify = s;
    spotify_ok = true;
    v = glos;
  }
  if (spotify.glosnosc) spotify.glosnosc(v * 65535 / 100);   // telefon pokazuje naszą głośność
}

void spotify_glosnosc(int v) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (teraz_ms() - ost_zmiana < SPOKOJ_MS) return;            // to echo naszej zmiany z gałki
  zmien_glos((v * 100 + 32767) / 65535, true);
}

void spotify_gra(bool g) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  if (g && !gra && wejscie != 0) {
    ESP_LOGI(TAG, "Spotify zaczął grać -> wejście 1");
    menu = false;
    zmien_wejscie(0);
  }
  gra = g;
  pozycja_t = teraz_ms();
}

void spotify_utwor(const Utwor& u) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  utwor = u;
  ma_utwor = !u.tytul.empty();
}

void spotify_pozycja(uint32_t ms) {
  pozycja_ms = ms;
  pozycja_t = teraz_ms();
}

void spotify_nieaktywny() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  gra = false;
  ma_utwor = false;
}

std::string json_status() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  uint32_t poz = pozycja_ms;
  char b[160];
  std::string j = "{";
  snprintf(b, sizeof b, "\"paused\":%s,\"stopped\":%s,\"volume\":%d,\"volume_steps\":100,\"polaczony\":%s,",
           gra ? "false" : "true", ma_utwor ? "false" : "true", glos, spotify_ok ? "true" : "false");
  j += b;
  j += "\"context_name\":\"\",\"shuffle_context\":false,\"repeat_context\":false";
  if (ma_utwor) {
    j += ",\"track\":{\"name\":" + json_tekst(utwor.tytul) + ",\"artist_names\":[" + json_tekst(utwor.wykonawca) +
         "],\"album_name\":" + json_tekst(utwor.album) + ",\"album_cover_url\":" + json_tekst(utwor.okladka);
    snprintf(b, sizeof b, ",\"position\":%u,\"duration\":%u}", (unsigned)poz, (unsigned)utwor.dlugosc_ms);
    j += b;
  }
  return j + "}";
}

std::string json_wejscie() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  char b[200];
  snprintf(b, sizeof b,
           "{\"menu\":%s,\"wybor\":%d,\"wejscie\":%d,\"glosnosc\":%d,\"nazwy\":[\"%s\",\"%s\",\"%s\"]}",
           menu ? "true" : "false", wybor, wylaczone ? -1 : wejscie, glos, NAZWY[0], NAZWY[1], NAZWY[2]);
  return b;
}

std::string json_system() {
  wifi_ap_record_t ap = {};
  int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
  char b[400];
  snprintf(b, sizeof b,
           "{\"uptime_s\":%lld,\"rssi\":%d,\"heap\":%u,\"psram\":%u,\"pt2258\":%s,\"wyswietlacz\":%s,\"czuwanie\":%s,\"spotify\":%s,"
           "\"i2c\":\"%s\",\"reset\":%d,\"ir\":%d,\"wersja\":\"%s\",\"bufor\":%d,\"przerwy\":%u}",
           (long long)(esp_timer_get_time() / 1000000), rssi,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           pt.ok() ? "true" : "false", seg.ok() ? "true" : "false", czuwanie ? "true" : "false", spotify_ok ? "true" : "false",
           sprzet::i2c_adresy().c_str(), (int)esp_reset_reason(), ostatni_ir,
           esp_app_get_description()->version, odtwarzacz::bufor_proc.load(),
           (unsigned)odtwarzacz::przerwy.load());   // 1 zasilanie, 3 program, 4 błąd, 9 spadek napięcia
  return b;
}

std::string json_strojenie() {
  std::lock_guard<std::recursive_mutex> l(mtx);
  char b[40];
  snprintf(b, sizeof b, "{\"kolano\":%d}", kolano);
  return b;
}

void ustaw_kolano(int db) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  kolano = std::clamp(db, 20, 55);
  ustawienia::zapisz_liczbe("kolano", kolano);
  Pt2258::ustaw_kolano(kolano);
  pt.glosnosc(glos);                    // ten sam procent, nowy poziom
}

void ustaw_glosnosc(int proc) {
  std::lock_guard<std::recursive_mutex> l(mtx);
  zmien_glos(proc, false);
}

void ustaw_wejscie(int nr) {
  bool pauzuj;
  {
    std::lock_guard<std::recursive_mutex> l(mtx);
    menu = false;
    pauzuj = zmien_wejscie(nr);
  }
  if (pauzuj && spotify.pauza) spotify.pauza(true);
}

void odtwarzanie(const std::string& p) {
  Spotify s;
  bool g;
  {
    std::lock_guard<std::recursive_mutex> l(mtx);
    if (!spotify_ok) return;
    s = spotify;
    g = gra;
  }
  if (p == "playpause" && s.pauza) s.pauza(g);
  else if (p == "play" && s.pauza) s.pauza(false);
  else if (p == "pause" && s.pauza) s.pauza(true);
  else if (p == "next" && s.nastepny) s.nastepny();
  else if (p == "prev" && s.poprzedni) s.poprzedni();
}

}  // namespace sterownik
