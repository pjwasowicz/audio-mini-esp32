#include "Aktualizacja.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "BellHTTPServer.h"
#include "Dziennik.h"
#include "Sprzet.h"
#include "Sterownik.h"
#include "civetweb.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace aktualizacja {

static const char* TAG = "aktualizacja";
static constexpr size_t PORCJA = 4096;

// Zapis do flash wyłącza pamięć podręczną, a wątki serwera WWW mają stos w PSRAM (wywrotka) – zapisy robi
// osobne zadanie ze stosem wewnętrznym; wątek WWW tylko czyta dane z sieci i czeka na wynik.
enum class Rodzaj { POCZATEK, ZAPIS, KONIEC, PRZERWIJ };
struct Zlecenie {
  Rodzaj rodzaj;
  const uint8_t* dane;
  size_t dl;
};
static QueueHandle_t zlecenia = nullptr;
static QueueHandle_t wyniki = nullptr;
static SemaphoreHandle_t jedna = nullptr;   // jedna aktualizacja naraz

static void pracownik(void*) {
  esp_ota_handle_t h = 0;
  const esp_partition_t* cel = nullptr;
  Zlecenie z;
  while (true) {
    xQueueReceive(zlecenia, &z, portMAX_DELAY);
    esp_err_t e = ESP_OK;
    switch (z.rodzaj) {
      case Rodzaj::POCZATEK:
        cel = esp_ota_get_next_update_partition(nullptr);
        e = cel ? esp_ota_begin(cel, OTA_WITH_SEQUENTIAL_WRITES, &h) : ESP_ERR_NOT_FOUND;
        break;
      case Rodzaj::ZAPIS:
        e = esp_ota_write(h, z.dane, z.dl);
        break;
      case Rodzaj::KONIEC:
        e = esp_ota_end(h);
        if (e == ESP_OK) e = esp_ota_set_boot_partition(cel);
        break;
      case Rodzaj::PRZERWIJ:
        esp_ota_abort(h);
        break;
    }
    xQueueSend(wyniki, &e, portMAX_DELAY);
  }
}

static esp_err_t zlec(Rodzaj r, const uint8_t* dane = nullptr, size_t dl = 0) {
  Zlecenie z{r, dane, dl};
  xQueueSend(zlecenia, &z, portMAX_DELAY);
  esp_err_t e;
  xQueueReceive(wyniki, &e, portMAX_DELAY);
  return e;
}

static std::string odpowiedz(bool ok, const char* opis) {
  return std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"opis\":\"" + opis + "\"}";
}

void rejestruj(std::shared_ptr<bell::BellHTTPServer> s) {
  zlecenia = xQueueCreate(1, sizeof(Zlecenie));
  wyniki = xQueueCreate(1, sizeof(esp_err_t));
  jedna = xSemaphoreCreateMutex();
  xTaskCreate(pracownik, "ota_zapis", 6144, nullptr, 5, nullptr);

  std::weak_ptr<bell::BellHTTPServer> w = s;
  s->registerPost("/api/aktualizacja", [w](struct mg_connection* c) {
    auto serwer = w.lock();
    const char* haslo = mg_get_header(c, "X-Haslo");
    if (strlen(CONFIG_AUDIO_OTA_HASLO) && (!haslo || strcmp(haslo, CONFIG_AUDIO_OTA_HASLO) != 0))
      return serwer->makeJsonResponse(odpowiedz(false, "złe hasło"), 403);
    long long razem = mg_get_request_info(c)->content_length;
    const esp_partition_t* cel = esp_ota_get_next_update_partition(nullptr);
    if (razem <= 0 || !cel || razem > (long long)cel->size)
      return serwer->makeJsonResponse(odpowiedz(false, "zły rozmiar programu"), 400);
    if (xSemaphoreTake(jedna, 0) != pdTRUE)
      return serwer->makeJsonResponse(odpowiedz(false, "aktualizacja już trwa"), 409);

    ESP_LOGW(TAG, "nowy program: %lld B do %s", razem, cel->label);
    sterownik::postep(0);
    uint8_t* bufor = (uint8_t*)heap_caps_malloc(PORCJA, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    esp_err_t e = bufor ? zlec(Rodzaj::POCZATEK) : ESP_ERR_NO_MEM;
    long long got = 0;
    int ost_proc = -1;
    while (e == ESP_OK && got < razem) {
      int n = mg_read(c, bufor, (size_t)std::min<long long>(PORCJA, razem - got));
      if (n <= 0) {
        e = ESP_FAIL;
        break;
      }
      e = zlec(Rodzaj::ZAPIS, bufor, n);
      got += n;
      int proc = (int)(got * 100 / razem);
      if (proc != ost_proc) sterownik::postep(ost_proc = proc);
    }
    if (e == ESP_OK) e = zlec(Rodzaj::KONIEC);
    else zlec(Rodzaj::PRZERWIJ);
    heap_caps_free(bufor);
    xSemaphoreGive(jedna);

    if (e != ESP_OK) {
      ESP_LOGE(TAG, "aktualizacja nieudana: %s", esp_err_to_name(e));
      sterownik::postep(-1);
      return serwer->makeJsonResponse(odpowiedz(false, esp_err_to_name(e)), 500);
    }
    ESP_LOGW(TAG, "zapisano %lld B, restart do nowej wersji", got);
    xTaskCreate([](void*) {
      vTaskDelay(pdMS_TO_TICKS(1000));   // odpowiedź zdąży dojść
      dziennik::powod("nowy program przez Wi-Fi");
      sprzet::zamroz();
      esp_restart();
    }, "ota_restart", 4096, nullptr, 5, nullptr);
    return serwer->makeJsonResponse(odpowiedz(true, "zapisano, restart"));
  });
}

}  // namespace aktualizacja
