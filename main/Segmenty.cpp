#include "Segmenty.h"

#include "esp_log.h"

static const char* TAG = "segmenty";
static constexpr uint8_t ADRES = 0x70;
static constexpr uint16_t KROPKA = 0x4000;

// segmenty jak w module Adafruit: bit 0–5 A–F, 6 G1, 7 G2, 8 H, 9 J, 10 K, 11 L, 12 M, 13 N, 14 kropka
// (H/K/L/N – ukośne, J/M – pionowe środkowe); znaki 0x20–0x5F, małe litery jak wielkie
static const uint16_t ZNAKI[64] = {
    0x0000, 0x0006, 0x0220, 0x12CE, 0x12ED, 0x0C24, 0x235D, 0x0400,   //  !"#$%&'
    0x2400, 0x0900, 0x3FC0, 0x12C0, 0x0800, 0x00C0, 0x0000, 0x0C00,   // ()*+,-./
    0x0C3F, 0x0006, 0x00DB, 0x008F, 0x00E6, 0x00ED, 0x00FD, 0x0007,   // 01234567
    0x00FF, 0x00EF, 0x1200, 0x0A00, 0x2400, 0x00C8, 0x0900, 0x1083,   // 89:;<=>?
    0x02BB, 0x00F7, 0x128F, 0x0039, 0x120F, 0x00F9, 0x0071, 0x00BD,   // @ABCDEFG
    0x00F6, 0x1209, 0x001E, 0x2470, 0x0038, 0x0536, 0x2136, 0x003F,   // HIJKLMNO
    0x00F3, 0x203F, 0x20F3, 0x00ED, 0x1201, 0x003E, 0x0C30, 0x2836,   // PQRSTUVW
    0x2D00, 0x1500, 0x0C09, 0x0039, 0x2100, 0x000F, 0x0C03, 0x0008,   // XYZ[\]^_
};

// litera z akcentem (Latin-1, Latin Extended-A) -> litera bazowa; nieznane -> '?'
static char zastepczy(char32_t c) {
  if (c < 128) return (char)c;
  static const struct {
    char32_t od, do_;
    const char* baza;
  } ZAKRESY[] = {
      {0xC0, 0xC5, "AAAAAA"}, {0xC7, 0xC7, "C"}, {0xC8, 0xCB, "EEEE"}, {0xCC, 0xCF, "IIII"}, {0xD1, 0xD1, "N"},
      {0xD2, 0xD6, "OOOOO"}, {0xD8, 0xD8, "O"}, {0xD9, 0xDC, "UUUU"}, {0xDD, 0xDD, "Y"}, {0xDF, 0xDF, "s"},
      {0xE0, 0xE5, "aaaaaa"}, {0xE7, 0xE7, "c"}, {0xE8, 0xEB, "eeee"}, {0xEC, 0xEF, "iiii"}, {0xF1, 0xF1, "n"},
      {0xF2, 0xF6, "ooooo"}, {0xF8, 0xF8, "o"}, {0xF9, 0xFC, "uuuu"}, {0xFD, 0xFF, "y?y"},
      {0x100, 0x105, "AaAaAa"}, {0x106, 0x10D, "CcCcCcCc"}, {0x10E, 0x111, "DdDd"}, {0x112, 0x11B, "EeEeEeEeEe"},
      {0x11C, 0x123, "GgGgGgGg"}, {0x124, 0x127, "HhHh"}, {0x128, 0x131, "IiIiIiIiIi"}, {0x134, 0x135, "Jj"},
      {0x136, 0x137, "Kk"}, {0x139, 0x142, "LlLlLlLlLl"}, {0x143, 0x148, "NnNnNn"}, {0x14C, 0x151, "OoOoOo"},
      {0x154, 0x159, "RrRrRr"}, {0x15A, 0x161, "SsSsSsSs"}, {0x162, 0x167, "TtTtTt"}, {0x168, 0x173, "UuUuUuUuUuUu"},
      {0x174, 0x177, "WwYy"}, {0x178, 0x17E, "YZzZzZz"},
  };
  for (auto& z : ZAKRESY)
    if (c >= z.od && c <= z.do_) return z.baza[c - z.od];
  switch (c) {
    case U'‘': case U'’': return '\'';
    case U'“': case U'”': case U'„': return '"';
    case U'–': case U'—': return '-';
    case U'…': return '.';
  }
  return '?';
}

static uint16_t wzor(char32_t c) {
  char a = zastepczy(c);
  if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
  if (a < 0x20 || a > 0x5F) a = '?';
  return ZNAKI[a - 0x20];
}

bool Segmenty::komenda(uint8_t c) {
  ok_ = i2c_master_transmit(dev_, &c, 1, 50) == ESP_OK;
  return ok_;
}

bool Segmenty::start(i2c_master_bus_handle_t bus) {
  if (!dev_) {
    if (i2c_master_probe(bus, ADRES, 20) != ESP_OK) return false;
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = ADRES;
    cfg.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) return false;
    ESP_LOGI(TAG, "HT16K33 pod 0x%02X", ADRES);
  }
  if (!komenda(0x21)) return false;   // oscylator wł.
  for (auto& p : poprzednie_) p = 0xFFFF;
  jasnosc_ = miganie_ = -1;
  jasnosc(8);
  miganie(false);
  return ok_;
}

void Segmenty::jasnosc(int poziom) {
  if (!dev_ || poziom == jasnosc_) return;
  if (komenda(0xE0 | (poziom & 0x0F))) jasnosc_ = poziom;
}

void Segmenty::miganie(bool wl) {
  if (!dev_ || (int)wl == miganie_) return;
  if (komenda(wl ? 0x85 : 0x81)) miganie_ = wl;   // wyświetlacz wł., miganie 1 Hz / bez migania
}

void Segmenty::pokaz(const std::u32string& tekst) {
  if (!dev_) return;
  uint16_t w[4] = {0, 0, 0, 0};
  int n = 0;
  for (char32_t c : tekst) {
    if (c == U'.' && n > 0 && !(w[n - 1] & KROPKA)) {   // kropka dokleja się do poprzedniego znaku
      w[n - 1] |= KROPKA;
      continue;
    }
    if (n == 4) break;
    w[n++] = c == U'.' ? KROPKA : wzor(c);
  }
  bool zmiana = false;
  for (int i = 0; i < 4; i++) zmiana |= w[i] != poprzednie_[i];
  if (!zmiana) return;
  uint8_t b[9] = {0x00};   // adres pamięci 0, po 2 bajty na znak (młodszy, starszy)
  for (int i = 0; i < 4; i++) {
    b[1 + 2 * i] = w[i] & 0xFF;
    b[2 + 2 * i] = w[i] >> 8;
  }
  ok_ = i2c_master_transmit(dev_, b, sizeof b, 50) == ESP_OK;
  if (ok_)
    for (int i = 0; i < 4; i++) poprzednie_[i] = w[i];
}
