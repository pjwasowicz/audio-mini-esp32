#include "Pt2258.h"

#include <algorithm>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "pt2258";

static constexpr uint8_t ADRES = 0x44;
static constexpr uint8_t CZYSC = 0xC0;
static constexpr uint8_t GLOWNA_10 = 0xD0, GLOWNA_1 = 0xE0;
static constexpr uint8_t KANAL_10[2] = {0x80, 0x40}, KANAL_1[2] = {0x90, 0x50};   // kanały 1 i 2
static constexpr uint8_t WYCISZ_WL = 0xF9, WYCISZ_WYL = 0xF8;

int Pt2258::kolano_db_ = 36;

void Pt2258::ustaw_kolano(int db) { kolano_db_ = std::clamp(db, 20, 55); }

int Pt2258::db_z_procent(int proc) {
  proc = std::clamp(proc, 0, 100);
  if (proc == 0) return -1;
  if (proc <= KOLANO_PROC)
    return (ZAKRES_DB - 1) - ((proc - 1) * (ZAKRES_DB - 1 - kolano_db_) + (KOLANO_PROC - 1) / 2) / (KOLANO_PROC - 1);
  return ((100 - proc) * kolano_db_ + (100 - KOLANO_PROC) / 2) / (100 - KOLANO_PROC);
}

bool Pt2258::wyslij(const uint8_t* bajty, size_t n) {
  esp_err_t e = i2c_master_transmit(dev_, bajty, n, 50);
  if (e != ESP_OK) {
    if (ok_) ESP_LOGW(TAG, "brak odpowiedzi: %s", esp_err_to_name(e));
    ok_ = false;
    return false;
  }
  return true;
}

bool Pt2258::para(uint8_t dziesiatki, uint8_t jednostki, int db) {
  db = std::clamp(db, 0, 79);
  uint8_t b[2] = {(uint8_t)(dziesiatki | (db / 10)), (uint8_t)(jednostki | (db % 10))};
  return wyslij(b, 2);
}

bool Pt2258::glowna(int z, int na) { return zmien(GLOWNA_10, GLOWNA_1, z, na); }

bool Pt2258::zmien(uint8_t b10, uint8_t b1, int z, int na) {
  uint8_t dz = b10 | (na / 10), dj = b1 | (na % 10);
  if (z < 0) {
    uint8_t b[2] = {dz, dj};
    return wyslij(b, 2);
  }
  // który bajt pierwszy: ten, po którym stan pośredni ma większe tłumienie (ciszej)
  int najpierw_dziesiatki = (na / 10) * 10 + z % 10;
  int najpierw_jednostki = (z / 10) * 10 + na % 10;
  uint8_t b[2] = {dz, dj};
  if (najpierw_dziesiatki < najpierw_jednostki) std::swap(b[0], b[1]);
  return wyslij(b, 2);
}

bool Pt2258::wycisz(bool wl) {
  if ((int)wl == wyciszony_) return true;
  uint8_t b = wl ? WYCISZ_WL : WYCISZ_WYL;
  if (!wyslij(&b, 1)) return false;
  wyciszony_ = wl;
  return true;
}

bool Pt2258::start(i2c_master_bus_handle_t bus, int proc, bool czysc) {
  if (!dev_) {
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = ADRES;
    cfg.scl_speed_hz = 100000;            // nota: maks. 100 kHz
    if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) return false;
  }
  ok_ = true;
  if (czysc) {
    vTaskDelay(pdMS_TO_TICKS(200));       // nota: ≥200 ms od włączenia zasilania
    uint8_t c = CZYSC;
    if (!wyslij(&c, 1)) return false;
  }
  db_ = wyciszony_ = -1;
  if (!odswiez(proc)) return false;
  ESP_LOGI(TAG, "gotowy, głośność %d %%", proc);
  return true;
}

bool Pt2258::odswiez(int proc) {
  // PT2258 nie ma ustawień domyślnych – korekta kanałów 1/2, głośność wspólna, wyciszenie
  ok_ = true;
  for (int k = 0; k < 2; k++)
    if (!para(KANAL_10[k], KANAL_1[k], kanal_db_)) return false;
  int db = db_z_procent(proc);
  if (db >= 0) {
    if (!glowna(-1, db)) return false;
    db_ = db;
  }
  wyciszony_ = -1;
  return wycisz(db < 0);
}

bool Pt2258::kanaly(int db) {
  // jednym zapisem, tylko przy odłączonych głośnikach (zmiana wejścia) – przestawianie w trakcie grania stuka
  kanal_db_ = std::clamp(db, 0, 79);
  if (!ok_ || !dev_) return false;        // bez układu: ustawi się przy starcie (odswiez)
  for (int k = 0; k < 2; k++)
    if (!para(KANAL_10[k], KANAL_1[k], kanal_db_)) return false;
  return true;
}

bool Pt2258::glosnosc(int proc) {
  int db = db_z_procent(proc);
  if (db < 0) return wycisz(true);
  if (db_ < 0) {
    if (!glowna(-1, db)) return false;
  } else if (db != db_) {
    int krok = db > db_ ? 1 : -1;
    for (int d = db_ + krok;; d += krok) {
      if (!glowna(d - krok, d)) return false;
      if (d == db) break;
      vTaskDelay(pdMS_TO_TICKS(3));
    }
  }
  db_ = db;
  return wycisz(false);
}
