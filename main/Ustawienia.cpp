#include "Ustawienia.h"

#include <functional>

#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace ustawienia {

static const char* TAG = "ustawienia";
static const char* PRZESTRZEN = "audio";

// Dostęp do flash wyłącza pamięć podręczną, a z nią PSRAM – zadanie ze stosem w PSRAM (cspot, serwer WWW)
// kończy się wtedy wywrotką. Z takiego zadania operacja idzie przez krótkie zadanie ze stosem wewnętrznym.
static void we_wewnetrznej(const std::function<void()>& f) {
  int znacznik;
  if (esp_ptr_internal(&znacznik)) {
    f();
    return;
  }
  struct Zlecenie {
    const std::function<void()>* f;
    SemaphoreHandle_t gotowe;
  } z{&f, xSemaphoreCreateBinary()};
  xTaskCreate(
      [](void* a) {
        auto* z = static_cast<Zlecenie*>(a);
        (*z->f)();
        xSemaphoreGive(z->gotowe);
        vTaskDelete(nullptr);
      },
      "nvs", 4096, &z, 6, nullptr);
  xSemaphoreTake(z.gotowe, portMAX_DELAY);
  vSemaphoreDelete(z.gotowe);
}

void init() {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    e = nvs_flash_init();
  }
  ESP_ERROR_CHECK(e);
}

static int czytaj_int(const char* klucz, int domyslna) {
  int32_t v = domyslna;
  we_wewnetrznej([&] {
    nvs_handle_t h;
    if (nvs_open(PRZESTRZEN, NVS_READONLY, &h) == ESP_OK) {
      nvs_get_i32(h, klucz, &v);
      nvs_close(h);
    }
  });
  return v;
}

static void zapisz_int(const char* klucz, int v) {
  we_wewnetrznej([&] {
    nvs_handle_t h;
    if (nvs_open(PRZESTRZEN, NVS_READWRITE, &h) != ESP_OK) return;
    int32_t stara;
    if (nvs_get_i32(h, klucz, &stara) != ESP_OK || stara != v) {   // bez zbędnych zapisów do flash
      nvs_set_i32(h, klucz, v);
      nvs_commit(h);
    }
    nvs_close(h);
  });
}

int glosnosc(int domyslna) { return czytaj_int("glosnosc", domyslna); }
void zapisz_glosnosc(int proc) { zapisz_int("glosnosc", proc); }
int wejscie() { return czytaj_int("wejscie", 0); }
void zapisz_wejscie(int nr) { zapisz_int("wejscie", nr); }

std::string login() {
  std::string wynik;
  we_wewnetrznej([&] {
    nvs_handle_t h;
    if (nvs_open(PRZESTRZEN, NVS_READONLY, &h) != ESP_OK) return;
    size_t dl = 0;
    if (nvs_get_str(h, "login", nullptr, &dl) == ESP_OK && dl > 1) {
      wynik.resize(dl);
      nvs_get_str(h, "login", wynik.data(), &dl);
      wynik.resize(dl - 1);
    }
    nvs_close(h);
  });
  return wynik;
}

void zapisz_login(const std::string& json) {
  we_wewnetrznej([&] {
    nvs_handle_t h;
    if (nvs_open(PRZESTRZEN, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "login", json.c_str());
    nvs_commit(h);
    nvs_close(h);
  });
  ESP_LOGI(TAG, "zapisano dane logowania Spotify (%u B)", (unsigned)json.size());
}

static std::string czytaj_str(const char* klucz) {
  std::string wynik;
  we_wewnetrznej([&] {
    nvs_handle_t h;
    if (nvs_open(PRZESTRZEN, NVS_READONLY, &h) != ESP_OK) return;
    size_t dl = 0;
    if (nvs_get_str(h, klucz, nullptr, &dl) == ESP_OK && dl > 1) {
      wynik.resize(dl);
      nvs_get_str(h, klucz, wynik.data(), &dl);
      wynik.resize(dl - 1);
    }
    nvs_close(h);
  });
  return wynik;
}

static void zapisz_str(const char* klucz, const std::string& v) {
  we_wewnetrznej([&] {
    nvs_handle_t h;
    if (nvs_open(PRZESTRZEN, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, klucz, v.c_str());
    nvs_commit(h);
    nvs_close(h);
  });
}

bool wifi(std::string& ssid, std::string& haslo) {
  ssid = czytaj_str("wifi_ssid");
  haslo = czytaj_str("wifi_haslo");
  return !ssid.empty();
}

void zapisz_wifi(const std::string& ssid, const std::string& haslo) {
  zapisz_str("wifi_ssid", ssid);
  zapisz_str("wifi_haslo", haslo);
  ESP_LOGI(TAG, "zapisano sieć Wi-Fi: %s", ssid.c_str());
}

void ustaw_tryb_konfig() { zapisz_int("konfig", 1); }
int liczba(const char* klucz, int domyslna) { return czytaj_int(klucz, domyslna); }
void zapisz_liczbe(const char* klucz, int v) { zapisz_int(klucz, v); }

bool tryb_konfig() {
  bool t = czytaj_int("konfig", 0) == 1;
  if (t) zapisz_int("konfig", 0);
  return t;
}

}  // namespace ustawienia
