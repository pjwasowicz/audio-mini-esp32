# Audio mini: a small stereo amplifier with Spotify, on an ESP32

*Polska wersja niżej: [przejdź do opisu po polsku](#audio-mini-mały-wzmacniacz-stereo-ze-spotify-na-esp32).*

![Audio mini with the lid off: amplifier module, power supply, controller board and the front panel display](docs/audio_mini_wnetrze.jpg)

*Audio mini with the lid off: the class-D amplifier module on the left, the 24 V power supply on the
right, the controller board with the ESP32 at the back, and the 14-segment display and volume knob on
the front panel.*

What I wanted was simple: pick up my phone, choose "Audio mini" in Spotify, and hear music from my
speakers. The TV and the turntable had to keep working too, all from one knob and one remote, with no
extra box that needs its own app.

So I built a small amplifier that does it. Inside the aluminium case there's a class-D amplifier module
(HW-576), a 24 V power supply and the controller board from this repository. The controller plays
Spotify, switches between Spotify, TV and the turntable with relays, and sets the volume with an analog
chip, so the TV and records never go through anything digital. The knob, the remote, the Spotify app
and a small web page all control the same volume. Turn the knob and the slider on your phone moves
with it.

It started on a Raspberry Pi. That worked, but a Pi is a whole computer to look after: an SD card, an
operating system, updates, and a slow start. The ESP32 version is ready a few seconds after you switch
it on, needs no looking after, and costs a fraction of the price.

## Using it

You pick "Audio mini" in the Spotify app like any other Connect speaker (you need Premium). Anyone at
home can connect from their own phone and account. After a power cut it remembers the last login and
connects again by itself.

Press play in Spotify and the amplifier switches to Spotify on its own. Switch to TV and Spotify pauses.

The knob, the remote, the phone and the web page all move the same volume. At low volume each click of
the knob is a small 1 dB step, so late-evening listening is easy to set. The shape of the volume curve
can be changed on the web page.

You shouldn't hear a pop when you switch it on or change inputs. The speakers have their own relay and
connect a couple of seconds later, once everything has power, the right input is on and the volume chip
has confirmed its settings twice.

On Spotify, after 30 seconds of silence the speakers disconnect and the display dims to a clock. TV and
the turntable switch off after 3 hours if nobody touches anything; press 2 or 3 on the remote to bring
them back.

The display is four red 14-segment characters, like on an 80s tuner. It shows the time, the scrolling
title and artist, "V 42" while you turn the volume, and the input name. On TV and the turntable a small
dot next to the clock tells you which one is on.

The remote is the cheap 21-button infrared kind: volume, play and pause, next and previous track,
inputs 1, 2 and 3, and mute.

The web page (`http://<device-address>:8080`) shows what's playing with the album cover, the volume,
the inputs, the volume curve and a few numbers about the device, like Wi-Fi signal and version.

To change the Wi-Fi network you don't need a computer: choose "CONF" in the knob menu, connect your
phone to the "Audio mini config" network, pick your home network and type the password.

New firmware goes over Wi-Fi with `make ota`, so the case stays closed.

## The hardware

| Part | What it does | Connection |
|---|---|---|
| ESP32-DevKitC-VIE (ESP32-WROVER-IE, 8 MB PSRAM, external antenna) | brain, Wi-Fi, Spotify | (none) |
| PCM5102A DAC module | Spotify audio → analog | I2S: BCK 26, LCK 25, DIN 27 |
| PT2258 | analog volume control | I2C 0x44 (SDA 21, SCL 22) |
| HT16K33 + 4 × 14-segment LED (0.54") | display | I2C 0x70 |
| 3 relays | input selection Spotify / TV / Phono | GPIO 19 / 18 / 23 |
| relay | speakers on/off | ZAP1, GPIO 13 |
| VS1838 IR receiver + 21-button NEC remote | remote control | ZAP2, GPIO 5 |
| EC11 rotary encoder with button | volume and menu | A 32, B 33, button 14 |
| spare connector | for future ideas | ZAP3, GPIO 15 |

The KiCad files are in `pcb/`: `input_selector` is the main board (there's a PDF of the schematic too)
and `speaker_delay` is the small speaker relay board. The pictures below are 3D views from KiCad. The
ESP32, the DAC and the power module have no 3D models, so you only see their outlines.

![Main board, 3D render from KiCad](docs/input_selector_3d.png)

| Main board (`input_selector`) | Speaker relay boards (`speaker_delay`) |
|---|---|
| ![Main board, top view](docs/input_selector_gora.png) | ![Speaker relay boards, top view](docs/speaker_delay_gora.png) |

Along the top of the main board are the three input relays, the PT2258 volume chip and the regulators
for the audio part. Along the bottom: the DAC module on the left, the ESP32 in the middle, and on the
right the spot for an LM2596 converter (U4), which I later swapped for a separate 5 V adapter.

### What I learned building it

The ESP32 needs its own 5 V power supply. When it shared power with the audio part, through either a
switching converter or a plain regulator, you could hear Wi-Fi in the speakers as a faint buzz. A
separate 5 V / 2 A adapter made it silent.

The DAC is too loud for the volume chip. The PCM5102A puts out about 2.1 V, more than the PT2258 can
take on 5 V, and some sound leaks straight through. For now the firmware turns Spotify down 10 dB before
the DAC and the volume chip turns that input down another 78 dB. On the next board I'd add two resistors
after the DAC, or run the volume chip from 9 V.

The relays disturb the wires to the volume chip. A command sent right after a relay clicked sometimes
arrived garbled, and the amplifier suddenly played very loud. Now the firmware sends all volume
settings again, twice, just before the speakers connect.

GPIO 5 is a bad pin for a relay. The ESP32 pulls it high while it starts, so anything on it switches on
before the program runs. The infrared receiver is happy there.

4 MB of flash is tight. With room for two copies of the firmware, needed for updates over Wi-Fi, about
100 KB is left. Next time I'd use the 8 or 16 MB version of the same module.

## Building and flashing

You need [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/) in `~/esp/esp-idf`. The Makefile
does the rest.

```bash
make            # first run asks for your Wi-Fi, downloads cspot and applies the patches
make flash      # flash over USB (PORT=/dev/cu.usbserial-XXXX if needed)
make ota        # flash over Wi-Fi (ADRES=192.168.1.130 by default)
make monitor    # serial log
make wifi       # change the Wi-Fi network stored at build time
```

Your Wi-Fi name and password stay in `sdkconfig.local`, which isn't in the repository. A network set
from the "CONF" menu wins over it. If you want `make ota` to ask for a password, set
`CONFIG_AUDIO_OTA_HASLO` in `menuconfig`; there's none by default.

The version number is `PROJECT_VER` in `CMakeLists.txt`. The display shows it for a few seconds after
switching on, and it's also in `/api/system`.

## Under the hood

The Spotify part isn't mine. It comes from [cspot](https://github.com/feelfreelinux/cspot), an
open-source Spotify client small enough to fit on a chip like the ESP32. The first `make` runs
`setup.sh`, which downloads it and applies my fixes from `patches/`.

Those fixes took most of the time. At first the box would play for a few minutes and then restart on its
own, usually right when I pressed pause or switched to TV. Once I tracked them down, the causes were:

- When you pause, the music fades out over half a second. The fade-out code went one step too far and
  wrote into memory that belonged to something else. That alone explains most of the restarts.
- The audio buffer had its "full" check backwards. When it filled up, bits of sound were cut off and
  the rest slid out of place.
- Every time the connection to Spotify dropped and came back, the part of the program that handles it
  ran out of working memory and crashed.
- The knob, the remote and the web page could all talk to Spotify at the same moment, and the library
  wasn't built for that.
- Right after start, a timer decided Spotify had been silent for ages and hung up.

On a weaker Wi-Fi signal the sound still stuttered. Two changes fixed it: a bigger buffer, now 9 seconds
of music, and switching off a sound-processing step that touched every sample even though it only does
anything during the pause fade-out. The chip was spending a lot of time on it, and decoding barely kept
up.

If you want to poke at it: the web page uses a small JSON API (`/api/status`, `/api/system`,
`/api/wejscie`, `/api/strojenie`, `/api/resety`). `/api/resety` lists the last ten restarts and why they
happened. Without it I would not have found the bugs above.

Code comments are in Polish.

## License

GPL-3.0 (see `LICENSE`), the same as cspot, which the Spotify part is built on.

---

# Audio mini: mały wzmacniacz stereo ze Spotify, na ESP32

![Audio mini bez pokrywy](docs/audio_mini_wnetrze.jpg)

*Audio mini bez pokrywy: moduł wzmacniacza klasy D z lewej, zasilacz 24 V z prawej, płytka sterownika
z ESP32 z tyłu, a na panelu przednim wyświetlacz 14-segmentowy i gałka głośności.*

Chciałem prostej rzeczy: biorę telefon, wybieram w Spotify „Audio mini” i słyszę muzykę z moich
kolumn. Telewizor i gramofon miały działać dalej, wszystko z jednej gałki i jednego pilota, bez
kolejnego pudełka z osobną aplikacją.

Zbudowałem więc mały wzmacniacz, który to robi. W aluminiowej obudowie jest moduł wzmacniacza klasy D
(HW-576), zasilacz 24 V i płytka sterownika z tego repozytorium. Sterownik gra Spotify, przełącza
przekaźnikami między Spotify, telewizorem i gramofonem, a głośność ustawia analogowym układem, więc
dźwięk z telewizora i z płyt nie przechodzi przez nic cyfrowego. Gałka, pilot, aplikacja Spotify i mała
strona WWW sterują tą samą głośnością. Kręcisz gałką, a suwak w telefonie przesuwa się razem z nią.

Zaczęło się od Raspberry Pi. Działało, ale Pi to cały komputer do pilnowania: karta SD, system,
aktualizacje i długi start. Wersja na ESP32 jest gotowa kilka sekund po włączeniu, nie trzeba się nią
opiekować i kosztuje ułamek ceny.

## Jak się z tego korzysta

„Audio mini” wybierasz w aplikacji Spotify jak każdy inny głośnik Connect (potrzebne jest Premium).
Każdy domownik może się połączyć ze swojego telefonu i konta. Po zaniku prądu urządzenie pamięta
ostatnie logowanie i łączy się samo.

Wciskasz „play” w Spotify i wzmacniacz sam przełącza się na Spotify. Przełączasz na telewizor
i Spotify się pauzuje.

Gałka, pilot, telefon i strona WWW przesuwają tę samą głośność. Przy cichym graniu każdy ząbek gałki to
mały krok o 1 dB, więc wieczorem łatwo trafić w odpowiedni poziom. Kształt krzywej głośności można
zmienić na stronie WWW.

Przy włączaniu i zmianie wejścia nie powinno nic stuknąć. Kolumny mają własny przekaźnik i dołączają
się kilka sekund później, gdy wszystko ma już zasilanie, włączone jest właściwe wejście, a układ
głośności dwa razy potwierdził ustawienia.

Na Spotify po 30 sekundach ciszy kolumny się odłączają, a wyświetlacz przygasa do zegara. Telewizor
i gramofon wyłączają się po 3 godzinach, jeśli nikt niczego nie dotyka; przycisk 2 albo 3 na pilocie
włącza je z powrotem.

Wyświetlacz to cztery czerwone znaki 14-segmentowe, jak w tunerze z lat 80. Pokazuje godzinę,
przewijany tytuł i wykonawcę, „V 42” przy kręceniu głośnością i nazwę wejścia. Na telewizorze
i gramofonie mała kropka przy zegarze mówi, które z nich gra.

Pilot to ten tani, na podczerwień, z 21 przyciskami: głośność, pauza, następny i poprzedni utwór,
wejścia 1, 2 i 3 oraz wyciszenie.

Strona WWW (`http://<adres-urządzenia>:8080`) pokazuje, co gra, razem z okładką, a do tego głośność,
wejścia, krzywą głośności i kilka liczb o urządzeniu, na przykład siłę Wi-Fi i wersję.

Do zmiany sieci Wi-Fi nie potrzeba komputera: w menu gałki wybierasz „CONF”, łączysz telefon z siecią
„Audio mini config”, wybierasz swoją sieć i wpisujesz hasło.

Nowy program idzie przez Wi-Fi poleceniem `make ota`, więc obudowa zostaje zamknięta.

## Sprzęt

| Element | Do czego | Połączenie |
|---|---|---|
| ESP32-DevKitC-VIE (ESP32-WROVER-IE, 8 MB PSRAM, antena zewnętrzna) | mózg, Wi-Fi, Spotify | (brak) |
| moduł DAC PCM5102A | dźwięk Spotify → analog | I2S: BCK 26, LCK 25, DIN 27 |
| PT2258 | analogowa regulacja głośności | I2C 0x44 (SDA 21, SCL 22) |
| HT16K33 + 4 × LED 14-seg (0,54") | wyświetlacz | I2C 0x70 |
| 3 przekaźniki | wybór wejścia Spotify / TV / Phono | GPIO 19 / 18 / 23 |
| przekaźnik | dołączanie kolumn | ZAP1, GPIO 13 |
| odbiornik IR VS1838 + pilot NEC 21 przycisków | pilot | ZAP2, GPIO 5 |
| enkoder EC11 z przyciskiem | głośność i menu | A 32, B 33, przycisk 14 |
| wolne złącze | na przyszłe pomysły | ZAP3, GPIO 15 |

Pliki KiCad są w `pcb/`: `input_selector` to płyta główna (schemat jest też w PDF), a `speaker_delay`
to mała płytka przekaźnika kolumn. Obrazki niżej to widoki 3D z KiCad. ESP32, DAC i moduł zasilania nie
mają modeli 3D, więc widać tylko ich obrysy.

![Płyta główna, widok 3D z KiCad](docs/input_selector_3d.png)

| Płyta główna (`input_selector`) | Płytki przekaźnika kolumn (`speaker_delay`) |
|---|---|
| ![Płyta główna, widok z góry](docs/input_selector_gora.png) | ![Płytki przekaźnika kolumn, widok z góry](docs/speaker_delay_gora.png) |

U góry płyty głównej są trzy przekaźniki wejść, układ głośności PT2258 i stabilizatory części audio.
Na dole: moduł DAC z lewej, ESP32 w środku, a z prawej miejsce na przetwornicę LM2596 (U4), którą
później zamieniłem na osobny zasilacz 5 V.

### Czego się nauczyłem przy budowie

ESP32 potrzebuje osobnego zasilacza 5 V. Gdy dzielił zasilanie z częścią audio, czy to przez
przetwornicę, czy przez zwykły stabilizator, Wi-Fi było słychać w kolumnach jako ciche brzęczenie.
Osobny zasilacz 5 V / 2 A dał ciszę.

DAC jest za głośny dla układu głośności. PCM5102A daje około 2,1 V, więcej niż PT2258 przyjmuje przy
zasilaniu 5 V, i część dźwięku przecieka obok. Na razie program ścisza Spotify o 10 dB przed DAC,
a układ głośności to wejście jeszcze o 78 dB. W następnej płytce dałbym dwa rezystory za DAC albo
zasiliłbym układ głośności z 9 V.

Przekaźniki zakłócają przewody do układu głośności. Polecenie wysłane tuż po kliknięciu przekaźnika
dochodziło czasem przekłamane i wzmacniacz nagle grał bardzo głośno. Teraz program wysyła wszystkie
ustawienia głośności jeszcze raz, dwa razy, tuż przed dołączeniem kolumn.

GPIO 5 to zły pin na przekaźnik. ESP32 przy starcie ustawia na nim stan wysoki, więc to, co jest do
niego podłączone, włącza się, zanim ruszy program. Odbiornikowi podczerwieni to nie przeszkadza.

4 MB pamięci flash to mało. Przy miejscu na dwie kopie programu, potrzebnym do aktualizacji przez
Wi-Fi, zostaje około 100 KB. Następnym razem wziąłbym ten sam moduł w wersji z 8 albo 16 MB.

## Kompilacja i wgrywanie

Potrzebny jest [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/) w `~/esp/esp-idf`. Resztą
zajmuje się Makefile.

```bash
make            # za pierwszym razem pyta o Wi-Fi, pobiera cspot i nakłada łatki
make flash      # wgranie przez USB (PORT=/dev/cu.usbserial-XXXX, jeśli trzeba)
make ota        # wgranie przez Wi-Fi (domyślnie ADRES=192.168.1.130)
make monitor    # komunikaty z ESP32
make wifi       # zmiana sieci Wi-Fi zapisanej przy kompilacji
```

Nazwa Twojej sieci i hasło zostają w `sdkconfig.local`, którego nie ma w repozytorium. Sieć ustawiona
z menu „CONF” ma przed nią pierwszeństwo. Jeśli chcesz, żeby `make ota` pytało o hasło, ustaw
`CONFIG_AUDIO_OTA_HASLO` w `menuconfig`; domyślnie hasła nie ma.

Numer wersji to `PROJECT_VER` w `CMakeLists.txt`. Wyświetlacz pokazuje go przez kilka sekund po
włączeniu, jest też w `/api/system`.

## Pod spodem

Część od Spotify nie jest moja. To [cspot](https://github.com/feelfreelinux/cspot), otwarty klient
Spotify na tyle mały, że mieści się w ESP32. Przy pierwszym `make` skrypt `setup.sh` pobiera go
i nakłada moje poprawki z `patches/`.

Na te poprawki poszło najwięcej czasu. Na początku urządzenie grało kilka minut i samo się restartowało,
najczęściej wtedy, gdy wciskałem pauzę albo przełączałem na telewizor. Przyczyny okazały się takie:

- Przy pauzie muzyka cichnie przez pół sekundy. Kod tego wyciszania robił o jeden krok za dużo
  i zapisywał w pamięci, która należała do czegoś innego. To tłumaczy większość restartów.
- Bufor dźwięku miał odwrotnie sprawdzane, czy jest pełny. Gdy się zapełnił, kawałki muzyki były
  ucinane, a reszta się rozjeżdżała.
- Za każdym razem, gdy połączenie ze Spotify zrywało się i wracało, części programu, która się tym
  zajmuje, brakowało pamięci roboczej i padała.
- Gałka, pilot i strona WWW mogły odezwać się do Spotify w tej samej chwili, a biblioteka nie była na
  to przygotowana.
- Zaraz po starcie licznik uznawał, że Spotify od dawna milczy, i rozłączał.

Przy słabszym Wi-Fi dźwięk dalej się zacinał. Pomogły dwie zmiany: większy bufor, teraz na 9 sekund
muzyki, i wyłączenie przetwarzania dźwięku, które obrabiało każdą próbkę, choć do czegokolwiek jest
potrzebne tylko przy wyciszaniu na pauzę. Procesor tracił na nie sporo czasu i dekodowanie ledwo
nadążało.

Jeśli chcesz pogrzebać: strona WWW korzysta z małego API w JSON (`/api/status`, `/api/system`,
`/api/wejscie`, `/api/strojenie`, `/api/resety`). `/api/resety` pokazuje dziesięć ostatnich restartów
z przyczyną. Bez niego tych błędów bym nie znalazł.

Komentarze w kodzie są po polsku.

## Licencja

GPL-3.0 (plik `LICENSE`), tak samo jak cspot, na którym opiera się część od Spotify.
