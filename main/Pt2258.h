#pragma once
// PT2258 – 6-kanałowy regulator głośności (I2C 0x44 = 0x88 w nocie), kanały 1/2 = L/P.
// Odpowiednik AudioMini/pi/enkoder/pt2258.py: zmiany po 1 dB, kolejność bajtów dziesiątki/jednostki tak,
// żeby stan pośredni był zawsze cichszy (bez trzasków), C0H tylko przy starcie.
#include "driver/i2c_master.h"

class Pt2258 {
 public:
  // krzywa z kolanem: 1–60 % = −59 dB…−kolano, 60–100 % = −kolano…0 dB (kolano ustawiane w panelu, domyślnie
  // 36 dB); 0 % = wyciszenie. PT2258 ma krok 1 dB, więc sąsiednie procenty mogą dawać ten sam poziom.
  static constexpr int ZAKRES_DB = 60;
  static constexpr int KOLANO_PROC = 60;
  static void ustaw_kolano(int db);                    // 20–55 dB

  // dodaje urządzenie i ustawia; czysc=false po cichym restarcie programu (PT2258 ma nadal zasilanie i ustawienia)
  bool start(i2c_master_bus_handle_t bus, int proc, bool czysc = true);
  bool glosnosc(int proc);                             // zmiana płynna po 1 dB
  bool odswiez(int proc);                              // pełny zestaw ustawień (po błędzie I2C)
  // tłumienie kanałów 1/2 przed głośnością wspólną: stała korekta poziomu wejścia; tylko przy odłączonych głośnikach
  bool kanaly(int db);
  bool ok() const { return ok_; }
  void bez_zasilania() { ok_ = false; }   // zasilanie odcięte: po włączeniu potrzebny pełny start

  static int db_z_procent(int proc);                   // −1 = wyciszenie

 private:
  bool wyslij(const uint8_t* bajty, size_t n);
  bool para(uint8_t dziesiatki, uint8_t jednostki, int db);
  bool zmien(uint8_t b10, uint8_t b1, int z, int na);   // z dB na dB, stan pośredni zawsze cichszy
  bool glowna(int z, int na);
  bool wycisz(bool wl);

  i2c_master_dev_handle_t dev_ = nullptr;
  int db_ = -1;          // tłumienie ustawione w układzie (−1 = nieznane)
  int wyciszony_ = -1;   // −1 = nieznane
  int kanal_db_ = 0;     // korekta kanałów (ustawiona albo do ustawienia przy starcie)
  static int kolano_db_;
  bool ok_ = false;
};
