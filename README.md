# Audio mini – ESP32

Sterownik do klasycznego wzmacniacza: Spotify Connect na ESP32, przełącznik wejść na przekaźnikach,
analogowa regulacja głośności na PT2258, gałka, pilot na podczerwień, panel WWW i retro wyświetlacz
14-segmentowy. Program w C++ (ESP-IDF 5.5 + [cspot](https://github.com/feelfreelinux/cspot)) i płytki w KiCad.

*English summary at the bottom.*

## Co potrafi

- **Spotify Connect** – urządzenie „Audio mini” widać w aplikacji Spotify (konto Premium), logowanie
  dowolnym kontem z telefonu, zapamiętane logowanie łączy się samo po starcie.
- **Trzy wejścia** na przekaźnikach: Spotify (DAC PCM5102A), TV, Phono. Start grania w Spotify przełącza
  na wejście Spotify, wybór innego wejścia pauzuje Spotify.
- **Głośność analogowa PT2258**, wspólna dla gałki, pilota, telefonu i panelu WWW; krzywa z kolanem
  (drobne kroki przy cichym słuchaniu, ustawiana w panelu), stała korekta poziomu dla każdego wejścia.
- **Głośniki na osobnym przekaźniku**: dołączane 2 s po starcie i po zmianie wejścia, dopiero gdy PT2258
  dwa razy potwierdzi ustawienia – bez stuków i huków.
- **Czuwanie**: na wejściu Spotify po 30 s ciszy głośniki odłączone, na wyświetlaczu zegar;
  TV/Phono po 3 h bez akcji odłączone (włącza przycisk 2/3 na pilocie).
- **Wyświetlacz 4 znaki 14-seg (HT16K33)**: zegar, przewijany tytuł i wykonawca, „V 42” przy głośności,
  znacznik wejścia przy zegarze, menu wejść.
- **Gałka EC11** (głośność po 1 dB, menu wejść i konfiguracji) i **pilot IR** (NEC, 21 przycisków).
- **Panel WWW** pod `http://<adres>:8080` – sterowanie, wejścia, głośność, strojenie krzywej,
  stan systemu; API JSON (`/api/status`, `/api/system`, `/api/wejscie`, `/api/resety`, …).
- **Konfiguracja Wi-Fi z telefonu**: w menu gałki „CONF” – sieć „Audio mini config”, strona z listą sieci.
- **Wgrywanie programu przez Wi-Fi**: `make ota`.
- **Dziennik resetów** (przyczyna i czas pracy, ostatnie 10) pod `/api/resety`.

## Sprzęt

| Element | Połączenie |
|---|---|
| ESP32-DevKitC-VIE (ESP32-WROVER-IE, 4 MB flash, 8 MB PSRAM, antena zewnętrzna) | – |
| DAC PCM5102A | I2S: BCK GPIO26, LCK GPIO25, DIN GPIO27 |
| PT2258 (0x44), wyświetlacz HT16K33 (0x70) | I2C: SDA GPIO21, SCL GPIO22 |
| Przekaźniki wejść K1/K2/K3 | GPIO19 / GPIO18 / GPIO23 |
| Przekaźnik głośników | ZAP1, GPIO13 |
| Odbiornik IR VS1838 | ZAP2, GPIO5 |
| Gałka EC11 | A GPIO32, B GPIO33, przycisk GPIO14 |
| Wolne złącze | ZAP3, GPIO15 |

Płytki w `pcb/`: `input_selector` (płyta główna, schemat także w PDF) i `speaker_delay`
(opóźnione dołączanie głośników).

**Uwagi z uruchamiania:**
- Część cyfrową (ESP32, przekaźniki) zasilać z **osobnego zasilacza 5 V**, część analogową osobno –
  przetwornica impulsowa ani stabilizator liniowy ze wspólnego zasilania nie dawały czystego dźwięku.
- PCM5102A daje ok. 2,1 V RMS – za dużo dla wejścia PT2258 na 5 V. Program ścisza Spotify o 10 dB przed
  DAC i o 78 dB na kanale PT2258; w następnej wersji płytki lepiej dzielnik na wyjściu DAC
  albo PT2258 na 9 V.
- GPIO5 ma przy starcie podciąganie do plusa (pin konfiguracyjny) – nie dla przekaźników, które
  muszą być wyłączone przy włączaniu.

## Kompilacja i wgrywanie

Potrzebny [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/) w `~/esp/esp-idf`.

```bash
make            # pierwsze uruchomienie: pyta o Wi-Fi (sdkconfig.local), pobiera cspot i nakłada łatki
make flash      # wgranie przez USB (PORT=/dev/cu.usbserial-XXXX)
make ota        # wgranie przez Wi-Fi (ADRES=192.168.1.130)
make monitor    # komunikaty z ESP32
make wifi       # zmiana sieci Wi-Fi zapisanej przy kompilacji
```

Nazwa sieci i hasło są tylko w `sdkconfig.local` (nie trafia do repozytorium); sieć ustawiona przez
menu „CONF” ma pierwszeństwo. Hasło do `make ota` można ustawić w `menuconfig`
(`CONFIG_AUDIO_OTA_HASLO`, domyślnie bez hasła).

Numer wersji: `PROJECT_VER` w `CMakeLists.txt` (pokazywany po starcie na wyświetlaczu i w `/api/system`).

### Łatki do cspot i bell (`patches/`)

`setup.sh` klonuje cspot i nakłada:
- `nanopb-protobuf7`, `cspot-login-mac`, `bell-civetweb-threads` – zgodność z ESP-IDF 5.5 i dzisiejszym protobuf,
- `cspot-mercury-stabilnosc` – stos 16 KB wątku `mercury_dispatcher` (przepełnienie przy ponownym łączeniu),
  licznik pingu od startu (fałszywe „brak pingu”), blokada map odpowiedzi (wyścig wątków),
- `bell-bufor-audio` – odwrócony warunek „bufor pełny” w `CentralAudioBuffer` i zapis za tablicą
  w `FadeEffect` (`<=` zamiast `<`) – przyczyny losowych resetów.

## Licencja

cspot jest na licencji GPL-3.0, więc program, który go używa, też podlega GPL-3.0.

---

## English summary

Controller for a classic stereo amplifier built around an ESP32-WROVER-IE: Spotify Connect (cspot)
through a PCM5102A DAC, three relay-switched inputs (Spotify / TV / Phono), analog volume on a PT2258
shared between a rotary encoder, an IR remote, the Spotify app and a web panel, a delayed speaker relay,
a 4-digit 14-segment HT16K33 display, Wi-Fi setup from a phone (captive portal) and OTA updates
(`make ota`). Firmware: C++ on ESP-IDF 5.5 with patches to cspot/bell fixing several memory and threading
bugs (see `patches/`). Hardware: KiCad projects in `pcb/`. Code comments and docs are in Polish.
