#include "Pilot.h"

#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace pilot {

static const char* TAG = "pilot";
static constexpr gpio_num_t PIN = GPIO_NUM_5;   // ZAP2 (J38)
static constexpr int64_t POWTORZ_MS = 200;      // powtórzenie ważne tyle po poprzednim kodzie
static constexpr int POMIN_POWTORZEN = 2;       // pierwsze powtórzenia (ok. 0,3 s) pominięte jak w klawiaturze

static rmt_channel_handle_t kanal = nullptr;
static QueueHandle_t kolejka = nullptr;
static rmt_symbol_word_t bufor[64];
static Zdarzenie zdarzenie;

static bool IRAM_ATTR odebrano(rmt_channel_handle_t, const rmt_rx_done_event_data_t* d, void*) {
  BaseType_t obudz = pdFALSE;
  xQueueSendFromISR(kolejka, d, &obudz);
  return obudz == pdTRUE;
}

// czas w µs w granicach ±30 % wzorca
static bool okolo(uint32_t us, uint32_t wzor) { return us > wzor * 7 / 10 && us < wzor * 13 / 10; }

static void zadanie(void*) {
  rmt_receive_config_t odbior = {};
  odbior.signal_range_min_ns = 1250;            // krótsze zakłócenia odrzucone
  odbior.signal_range_max_ns = 12 * 1000 * 1000;   // dłuższa przerwa = koniec ramki
  uint8_t ostatni = 0;
  int powtorzen = 0;
  int64_t ostatni_t = -1000000;
  rmt_rx_done_event_data_t d;
  ESP_ERROR_CHECK(rmt_receive(kanal, bufor, sizeof bufor, &odbior));
  while (true) {
    if (xQueueReceive(kolejka, &d, portMAX_DELAY) != pdTRUE) continue;
    const rmt_symbol_word_t* s = d.received_symbols;
    size_t n = d.num_symbols;
    int64_t t = esp_timer_get_time() / 1000;
    if (n >= 1 && okolo(s[0].duration0, 9000) && okolo(s[0].duration1, 2250)) {
      if (t - ostatni_t < POWTORZ_MS && ++powtorzen > POMIN_POWTORZEN && zdarzenie)   // przytrzymany przycisk
        zdarzenie(ostatni, true);
      ostatni_t = t;
    } else if (n >= 33 && okolo(s[0].duration0, 9000) && okolo(s[0].duration1, 4500)) {
      uint32_t bity = 0;
      bool dobre = true;
      for (int i = 0; i < 32 && dobre; i++) {
        const rmt_symbol_word_t& b = s[1 + i];
        if (!okolo(b.duration0, 560)) dobre = false;
        else if (okolo(b.duration1, 1690)) bity |= 1u << i;
        else if (!okolo(b.duration1, 560)) dobre = false;
      }
      uint8_t adres = bity & 0xFF, kod = (bity >> 16) & 0xFF, nkod = bity >> 24;
      if (dobre && (kod ^ nkod) == 0xFF) {
        ESP_LOGI(TAG, "adres 0x%02X, kod 0x%02X", adres, kod);
        ostatni = kod;
        powtorzen = 0;
        ostatni_t = t;
        if (zdarzenie) zdarzenie(kod, false);
      } else {
        ESP_LOGW(TAG, "ramka NEC z błędem (%u symboli)", (unsigned)n);
      }
    } else if (n > 0) {   // coś innego niż NEC: czasy pierwszych impulsów do rozpoznania pilota
      ESP_LOGW(TAG, "nieznana ramka: %u symboli, %u/%u %u/%u µs", (unsigned)n, s[0].duration0, s[0].duration1,
               n > 1 ? s[1].duration0 : 0, n > 1 ? s[1].duration1 : 0);
    }
    ESP_ERROR_CHECK(rmt_receive(kanal, bufor, sizeof bufor, &odbior));
  }
}

void start(Zdarzenie z) {
  zdarzenie = std::move(z);
  rmt_rx_channel_config_t cfg = {};
  cfg.gpio_num = PIN;
  cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  cfg.resolution_hz = 1000000;                  // 1 tik = 1 µs
  cfg.mem_block_symbols = 64;
  if (rmt_new_rx_channel(&cfg, &kanal) != ESP_OK) {
    ESP_LOGE(TAG, "RMT niedostępny, pilot wyłączony");
    return;
  }
  kolejka = xQueueCreate(4, sizeof(rmt_rx_done_event_data_t));
  rmt_rx_event_callbacks_t cb = {};
  cb.on_recv_done = odebrano;
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(kanal, &cb, nullptr));
  ESP_ERROR_CHECK(rmt_enable(kanal));
  xTaskCreatePinnedToCore(zadanie, "pilot", 6144, nullptr, 5, nullptr, 0);
  ESP_LOGI(TAG, "odbiornik podczerwieni na GPIO%d", PIN);
}

}  // namespace pilot
