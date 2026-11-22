#include "Sprzet.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace sprzet {

static const char* TAG = "sprzet";
static i2c_master_bus_handle_t bus = nullptr;
static pcnt_unit_handle_t enkoder = nullptr;
static int ostatni_licznik = 0;   // stan licznika przy poprzednim odczycie
static int reszta = 0;            // impulsy, które nie złożyły się jeszcze w pełny ząbek
static std::string znalezione;    // adresy I2C odpowiadające przy starcie, np. "0x3e 0x44"
static const gpio_num_t GLOSNIKI = GPIO_NUM_13;   // przekaźnik głośników na złączu ZAP1 (J37)
static constexpr int GLOSNIKI_POZIOM_WL = 1;      // 0, jeśli moduł przekaźnika włącza się stanem niskim
static const gpio_num_t PRZEKAZNIKI[3] = {(gpio_num_t)CONFIG_PIN_T1, (gpio_num_t)CONFIG_PIN_T2,
                                          (gpio_num_t)CONFIG_PIN_T3};

void init(int wejscie, bool cichy) {
  // Po cichym restarcie piny były zamrożone (gpio_hold) we włączonym stanie: ustawiamy ten sam stan i dopiero
  // potem zwalniamy zamrożenie – styki nie puszczają. Po włączeniu zasilania wszystko wyłączone: wejście
  // i głośniki włącza po kolei sterownik, kiedy zasilanie się ustali.
  for (int i = 0; i < 3; i++) {
    gpio_set_direction(PRZEKAZNIKI[i], GPIO_MODE_OUTPUT);
    gpio_set_level(PRZEKAZNIKI[i], cichy && i == wejscie);
    gpio_hold_dis(PRZEKAZNIKI[i]);
  }
  gpio_set_direction(GLOSNIKI, GPIO_MODE_OUTPUT);
  glosniki(cichy);
  gpio_hold_dis(GLOSNIKI);

  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = (gpio_num_t)CONFIG_PIN_I2C_SDA;
  cfg.scl_io_num = (gpio_num_t)CONFIG_PIN_I2C_SCL;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;   // na płytce są też R11/R12 4,7k
  ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
  for (int a = 0x08; a < 0x78; a++) {
    if (i2c_master_probe(bus, a, 20) != ESP_OK) continue;
    char t[6];
    snprintf(t, sizeof t, "0x%02x", a);
    znalezione += (znalezione.empty() ? "" : " ") + std::string(t);
  }
  ESP_LOGI(TAG, "I2C: %s", znalezione.empty() ? "nic nie odpowiada" : znalezione.c_str());

  // impulsator: dekodowanie kwadraturowe x4 w liczniku PCNT, filtr zakłóceń 1 µs
  pcnt_unit_config_t ucfg = {};
  ucfg.low_limit = -1000;
  ucfg.high_limit = 1000;
  ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &enkoder));
  pcnt_glitch_filter_config_t filtr = {.max_glitch_ns = 1000};
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(enkoder, &filtr));
  pcnt_chan_config_t ca = {};
  ca.edge_gpio_num = CONFIG_PIN_ENC_A;
  ca.level_gpio_num = CONFIG_PIN_ENC_B;
  pcnt_chan_config_t cb = {};
  cb.edge_gpio_num = CONFIG_PIN_ENC_B;
  cb.level_gpio_num = CONFIG_PIN_ENC_A;
  pcnt_channel_handle_t kanal_a, kanal_b;
  ESP_ERROR_CHECK(pcnt_new_channel(enkoder, &ca, &kanal_a));
  ESP_ERROR_CHECK(pcnt_new_channel(enkoder, &cb, &kanal_b));
  pcnt_channel_set_edge_action(kanal_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(kanal_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(kanal_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(kanal_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  // styki gałki zwierają do masy: wewnętrzne podciąganie do 3,3 V
  gpio_pullup_en((gpio_num_t)CONFIG_PIN_ENC_A);
  gpio_pullup_en((gpio_num_t)CONFIG_PIN_ENC_B);
  ESP_ERROR_CHECK(pcnt_unit_enable(enkoder));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(enkoder));
  ESP_ERROR_CHECK(pcnt_unit_start(enkoder));

  gpio_config_t przycisk = {};
  przycisk.pin_bit_mask = 1ULL << CONFIG_PIN_ENC_SW;
  przycisk.mode = GPIO_MODE_INPUT;
  przycisk.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&przycisk);
  ESP_LOGI(TAG, "I2C SDA=%d SCL=%d, gałka A=%d B=%d SW=%d", CONFIG_PIN_I2C_SDA, CONFIG_PIN_I2C_SCL,
           CONFIG_PIN_ENC_A, CONFIG_PIN_ENC_B, CONFIG_PIN_ENC_SW);
}

i2c_master_bus_handle_t i2c() { return bus; }
const std::string& i2c_adresy() { return znalezione; }

void przekaznik(int wejscie) {
  // najpierw wszystkie wyłączone, potem wybrany – dwa źródła nigdy nie są połączone naraz
  for (auto p : PRZEKAZNIKI) gpio_set_level(p, 0);
  if (wejscie < 0 || wejscie > 2) return;
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(PRZEKAZNIKI[wejscie], 1);
}

void glosniki(bool wl) { gpio_set_level(GLOSNIKI, wl ? GLOSNIKI_POZIOM_WL : !GLOSNIKI_POZIOM_WL); }

void zamroz() {
  for (auto p : PRZEKAZNIKI) gpio_hold_en(p);
  gpio_hold_en(GLOSNIKI);
}

int zabki() {
  int licznik = 0;
  pcnt_unit_get_count(enkoder, &licznik);
  int zmiana = licznik - ostatni_licznik;
  ostatni_licznik = licznik;
  if (licznik > 500 || licznik < -500) {     // daleko od granic ±1000, żeby nie zgubić impulsów
    pcnt_unit_clear_count(enkoder);
    ostatni_licznik = 0;
  }
  reszta += zmiana;
  int pelne = reszta / 4;                    // EC11: 4 zbocza na jeden ząbek
  reszta -= pelne * 4;
  return pelne * CONFIG_ENC_KIERUNEK;
}

bool wcisniety() {
  static bool poprzednio = false;
  static int64_t ostatnio_us = 0;
  bool teraz = gpio_get_level((gpio_num_t)CONFIG_PIN_ENC_SW) == 0;
  bool wynik = false;
  if (teraz && !poprzednio) {
    int64_t t = esp_timer_get_time();
    if (t - ostatnio_us > 250000) {           // drugi impuls w ciągu 250 ms to drganie styku
      wynik = true;
      ostatnio_us = t;
    }
  }
  poprzednio = teraz;
  return wynik;
}

}  // namespace sprzet
