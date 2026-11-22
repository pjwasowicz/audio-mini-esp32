#include "Dziennik.h"

#include <cstdio>
#include <cstring>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"

namespace dziennik {

static const char* TAG = "dziennik";
static constexpr uint32_t ZNACZNIK = 0x41554449;   // "AUDI": pamięć RTC przetrwała reset
static constexpr int WPISOW = 10;

// pamięć RTC nie jest czyszczona przy restarcie programu, panice i watchdogu (przy zaniku zasilania tak)
struct Rtc {
  uint32_t znacznik;
  uint32_t dzialal_s;
  char powod[48];
};
static RTC_NOINIT_ATTR Rtc rtc;

struct Wpis {
  uint8_t przyczyna;        // esp_reset_reason_t
  uint32_t dzialal_s;       // 0 = nieznane (zanik zasilania)
  char powod[48];
};
static Wpis wpisy[WPISOW];

static const char* nazwa(int r) {
  switch (r) {
    case ESP_RST_POWERON: return "włączenie zasilania";
    case ESP_RST_EXT: return "przycisk EN";
    case ESP_RST_SW: return "restart programu";
    case ESP_RST_PANIC: return "błąd programu";
    case ESP_RST_INT_WDT: return "watchdog przerwań";
    case ESP_RST_TASK_WDT: return "watchdog zadań";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "spadek napięcia";
    default: return "nieznana";
  }
}

static void licznik(void*) { rtc.dzialal_s = esp_timer_get_time() / 1000000; }

void start() {
  nvs_handle_t h;
  if (nvs_open("dziennik", NVS_READWRITE, &h) == ESP_OK) {
    size_t dl = sizeof wpisy;
    if (nvs_get_blob(h, "wpisy", wpisy, &dl) != ESP_OK || dl != sizeof wpisy) memset(wpisy, 0, sizeof wpisy);
    memmove(&wpisy[1], &wpisy[0], sizeof(Wpis) * (WPISOW - 1));
    Wpis& w = wpisy[0];
    memset(&w, 0, sizeof w);
    w.przyczyna = esp_reset_reason();
    if (rtc.znacznik == ZNACZNIK) {
      w.dzialal_s = rtc.dzialal_s;
      if (w.przyczyna == ESP_RST_SW) strncpy(w.powod, rtc.powod, sizeof w.powod - 1);
    }
    nvs_set_blob(h, "wpisy", wpisy, sizeof wpisy);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "poprzedni reset: %s%s%s, działał %u s", nazwa(w.przyczyna), w.powod[0] ? " – " : "", w.powod,
             (unsigned)w.dzialal_s);
  }
  memset(&rtc, 0, sizeof rtc);
  rtc.znacznik = ZNACZNIK;
  // czas pracy co 2 s do pamięci RTC, żeby po panice/watchdogu było wiadomo, kiedy padło
  esp_timer_create_args_t a = {};
  a.callback = licznik;
  a.name = "dziennik";
  esp_timer_handle_t t;
  if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_periodic(t, 2000000);
}

void powod(const char* opis) {
  strncpy(rtc.powod, opis, sizeof rtc.powod - 1);
  rtc.powod[sizeof rtc.powod - 1] = 0;
  rtc.dzialal_s = esp_timer_get_time() / 1000000;
}

std::string json() {
  std::string o = "[";
  for (int i = 0; i < WPISOW; i++) {
    const Wpis& w = wpisy[i];
    if (!w.przyczyna) break;
    char b[160];
    snprintf(b, sizeof b, "%s{\"przyczyna\":\"%s\",\"powod\":\"%s\",\"dzialal_s\":%u}", i ? "," : "", nazwa(w.przyczyna),
             w.powod, (unsigned)w.dzialal_s);
    o += b;
  }
  return o + "]";
}

}  // namespace dziennik
