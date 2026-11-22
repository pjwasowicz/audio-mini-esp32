# Bugs I found in cspot and bell

*Polska wersja niżej.*

Audio mini uses [cspot](https://github.com/feelfreelinux/cspot) for Spotify Connect, together with its
helper library [bell](https://github.com/feelfreelinux/bell). I built against cspot `1b07a9c`
(July 2024) and bell `e837373` (June 2024). Out of the box the device played for a few minutes and then
restarted on its own. These are the problems I tracked down and how they're fixed here. Each fix is a
patch in `patches/`, applied by `setup.sh`.

If you use cspot in your own project, check the first five. They aren't specific to my hardware.

## Crashes

### 1. Pause fade-out writes past the end of its buffer

`bell/main/audio-dsp/BellDSP.cpp`, `BellDSP::FadeEffect::apply()`

```cpp
for (int x = 0; x <= samples; x++)   // should be <
  audioData[x] *= effect;
```

On every pause the fade-out runs over full 1024-sample blocks and writes one float past the end of both
`dataLeft` and `dataRight`. That quietly damages the heap. The crash comes a few seconds later, usually
as `assert failed: block_next tlsf_block_functions.h` when some unrelated memory is freed, so the
backtrace points somewhere else entirely. Heap poisoning (`CONFIG_HEAP_POISONING_COMPREHENSIVE`) showed
two damaged blocks about 4 KB apart, one per channel, every time right after a pause.

This was the main cause of the random restarts.

Fix: `<` instead of `<=`. Patch: `bell-bufor-audio.patch`.

### 2. The audio buffer never reports "full"

`bell/main/audio-dsp/include/CentralAudioBuffer.h`, `writePCM()`

```cpp
if ((audioBuffer->size() - audioBuffer->capacity()) < sizeof(AudioChunk))   // operands swapped
  return 0;
```

Both values are `size_t`, so `size - capacity` wraps around to a huge number and the check never stops
a write. When the buffer is full, `CircularBuffer::write()` stores only part of a chunk. Every chunk
read after that is shifted, so `pcmSize` comes from audio samples, and the player and the DSP work on
whatever length that happens to be.

Fix: `capacity() - size()`. Patch: `bell-bufor-audio.patch`. As a second guard,
`main/SpotifyPlayer.cpp` drops any chunk with `pcmSize` over 4096 and clears the buffer.

### 3. The Mercury thread runs out of stack when reconnecting

`cspot/src/MercurySession.cpp`, the `mercury_dispatcher` task has 4 KB of stack.

When the connection drops, `reconnect()` runs on that task: picking an access point over HTTP, DNS,
the handshake and the login, with all its crypto. That doesn't fit in 4 KB, and FreeRTOS stops with
`A stack overflow in task mercury_dispatcher has been detected`.

Fix: 16 KB (bell puts task stacks in PSRAM, so it costs no internal RAM). Patch:
`cspot-mercury-stabilnosc.patch`.

### 4. "No ping received" right after connecting

`cspot/include/MercurySession.h`: `unsigned long long lastPingTimestamp = -1;`

The timestamp is only set when the first ping arrives, which can take up to two minutes. Until then,
every read timeout asks `triggerTimeout()`, and with `-1` it answers "reconnect". So a few seconds after
logging in the session drops itself (`Reconnection required, no ping received`). Together with bug 3
that meant a crash soon after every start.

Fix: set `lastPingTimestamp` to the current time when `runTask()` starts. Patch:
`cspot-mercury-stabilnosc.patch`.

### 5. MercurySession isn't safe to use from several threads

`cspot/src/MercurySession.cpp`

`executeSubscription()` and `requestAudioKey()` are called from the track player and the Spirc handler
(for example `notifyAudioEnded()` when a song finishes), while the dispatcher thread reads and erases
the same `callbacks`, `subscriptions` and `audioKeyCallbacks` maps and reuses the shared
`tempMercuryHeader`. Nothing locks any of it. The result is a damaged heap at random moments, most
often around track changes.

Fix: a mutex around the maps, the shared header and the sequence numbers. The callbacks themselves run
outside the lock, because some of them take other cspot locks and holding mine would risk a deadlock.
Patch: `cspot-mercury-stabilnosc.patch`.

In the application I also never call `SpircHandler` (pause, next, volume) from my own threads. Those
calls go into a queue that the cspot thread empties between packets (`main/main.cpp`).

## Smaller problems

- **Login errors are ignored.** `LoginBlob::decodeBlob()` logs `Mac doesn't match!` and carries on with
  garbage. Now it throws, so the app can answer the phone with an error. `PlainConnection::connect()`
  had a similar gap when DNS failed. Patch: `cspot-login-mac.patch`.
- **civetweb thread start on ESP32.** The web server handed its new threads a pointer to a local
  variable and freed the start parameters too early. Patch: `bell-civetweb-threads.patch`.
- **nanopb generator vs. protobuf 5+.** The bundled generator uses an API that newer Python protobuf
  removed, so the build failed. Patch: `nanopb-protobuf7.patch`.
- **Dangling reference in the example player.** The example player passes a data callback that
  captures a local `std::hash` object by reference. It happens to work because the object is empty, but
  it's still a dangling reference. My `SpotifyPlayer.cpp` captures it by value.

## Performance

`BellDSP::process()` converts every sample to float and back on every chunk, even when no effect is
active. On the ESP32 that left the decoder only about 5% faster than real time, so any Wi-Fi hiccup
turned into a gap in the music. My player calls the DSP only while the pause fade-out runs. Together
with a 9-second buffer (384 chunks in PSRAM) the buffer now refills about six times faster.

One thing that did *not* work: raising `HTTP_BUFFER_SIZE` in `CDNAudioFile` from 14 KB to 64 KB made
streaming stop after a few seconds. I left it at 14 KB.

---

# Błędy znalezione w cspot i bell

Audio mini korzysta z [cspot](https://github.com/feelfreelinux/cspot) do Spotify Connect i z jego
biblioteki pomocniczej [bell](https://github.com/feelfreelinux/bell). Budowałem na cspot `1b07a9c`
(lipiec 2024) i bell `e837373` (czerwiec 2024). Bez zmian urządzenie grało kilka minut i samo się
restartowało. Poniżej problemy, które znalazłem, i jak są tu poprawione. Każda poprawka to łatka
w `patches/`, nakładana przez `setup.sh`.

Jeśli używasz cspot we własnym projekcie, sprawdź pierwsze pięć. Nie zależą od mojego sprzętu.

## Wywrotki

### 1. Wyciszanie przy pauzie pisze za końcem bufora

`bell/main/audio-dsp/BellDSP.cpp`, `BellDSP::FadeEffect::apply()`

```cpp
for (int x = 0; x <= samples; x++)   // powinno być <
  audioData[x] *= effect;
```

Przy każdej pauzie wyciszanie przechodzi przez pełne bloki po 1024 próbki i zapisuje jedną liczbę za
końcem `dataLeft` i `dataRight`. Po cichu psuje to stertę. Wywrotka przychodzi kilka sekund później,
zwykle jako `assert failed: block_next tlsf_block_functions.h` przy zwalnianiu zupełnie innej pamięci,
więc ślad wskazuje w złe miejsce. Kontrola sterty (`CONFIG_HEAP_POISONING_COMPREHENSIVE`) pokazywała
dwa uszkodzone bloki odległe o około 4 KB, po jednym na kanał, zawsze tuż po pauzie.

To była główna przyczyna losowych restartów.

Poprawka: `<` zamiast `<=`. Łatka: `bell-bufor-audio.patch`.

### 2. Bufor audio nigdy nie jest „pełny”

`bell/main/audio-dsp/include/CentralAudioBuffer.h`, `writePCM()`

```cpp
if ((audioBuffer->size() - audioBuffer->capacity()) < sizeof(AudioChunk))   // odwrócona kolejność
  return 0;
```

Obie wartości są typu `size_t`, więc `size - capacity` przekręca się w ogromną liczbę i warunek nigdy nie
zatrzymuje zapisu. Gdy bufor jest pełny, `CircularBuffer::write()` zapisuje tylko część porcji. Każda
następna porcja jest przesunięta, `pcmSize` bierze się z próbek dźwięku, a odtwarzacz i DSP pracują na
przypadkowej długości.

Poprawka: `capacity() - size()`. Łatka: `bell-bufor-audio.patch`. Dodatkowo `main/SpotifyPlayer.cpp`
wyrzuca porcję z `pcmSize` ponad 4096 i czyści bufor.

### 3. Wątkowi Mercury brakuje stosu przy ponownym łączeniu

`cspot/src/MercurySession.cpp`, zadanie `mercury_dispatcher` ma 4 KB stosu.

Gdy połączenie się zrywa, `reconnect()` działa w tym zadaniu: wybór serwera przez HTTP, DNS, nawiązanie
połączenia i logowanie z całą kryptografią. To nie mieści się w 4 KB i FreeRTOS zatrzymuje program
komunikatem `A stack overflow in task mercury_dispatcher has been detected`.

Poprawka: 16 KB (bell trzyma stosy zadań w PSRAM, więc nie zabiera pamięci wewnętrznej). Łatka:
`cspot-mercury-stabilnosc.patch`.

### 4. „Brak pingu” zaraz po połączeniu

`cspot/include/MercurySession.h`: `unsigned long long lastPingTimestamp = -1;`

Ta wartość jest ustawiana dopiero przy pierwszym pingu, a ten przychodzi nawet po dwóch minutach. Do
tego czasu każde przekroczenie czasu odczytu pyta `triggerTimeout()`, które przy `-1` odpowiada
„połącz od nowa”. Kilka sekund po zalogowaniu sesja zrywa się sama (`Reconnection required, no ping
received`). Razem z błędem 3 dawało to wywrotkę wkrótce po każdym starcie.

Poprawka: ustawienie `lastPingTimestamp` na bieżący czas na początku `runTask()`. Łatka:
`cspot-mercury-stabilnosc.patch`.

### 5. MercurySession nie jest odporny na kilka wątków

`cspot/src/MercurySession.cpp`

`executeSubscription()` i `requestAudioKey()` wołane są z odtwarzacza i z obsługi Spirc (na przykład
`notifyAudioEnded()` na końcu utworu), podczas gdy wątek dispatchera czyta i usuwa wpisy z tych samych
map `callbacks`, `subscriptions` i `audioKeyCallbacks` i używa wspólnego `tempMercuryHeader`. Nic tego
nie blokuje. Efekt to uszkodzona sterta w przypadkowych chwilach, najczęściej przy zmianie utworu.

Poprawka: mutex wokół map, wspólnego nagłówka i numerów sekwencji. Same odpowiedzi wykonują się poza
blokadą, bo część z nich bierze inne blokady cspot i trzymanie mojej groziłoby zakleszczeniem. Łatka:
`cspot-mercury-stabilnosc.patch`.

W samym programie nie wołam też `SpircHandler` (pauza, następny utwór, głośność) z własnych wątków. Te
polecenia trafiają do kolejki, którą opróżnia wątek cspot między pakietami (`main/main.cpp`).

## Mniejsze problemy

- **Błędy logowania są ignorowane.** `LoginBlob::decodeBlob()` wypisuje `Mac doesn't match!` i działa
  dalej na śmieciach. Teraz zgłasza wyjątek, więc program może odpowiedzieć telefonowi błędem.
  `PlainConnection::connect()` miało podobną lukę przy błędzie DNS. Łatka: `cspot-login-mac.patch`.
- **Start wątków civetweb na ESP32.** Serwer WWW przekazywał nowym wątkom wskaźnik do zmiennej lokalnej
  i za wcześnie zwalniał parametry startu. Łatka: `bell-civetweb-threads.patch`.
- **Generator nanopb a protobuf 5+.** Dołączony generator używa funkcji, której nowsze protobuf dla
  Pythona już nie mają, i kompilacja się nie udawała. Łatka: `nanopb-protobuf7.patch`.
- **Wisząca referencja w przykładowym odtwarzaczu.** Przykład przekazuje funkcję zwrotną, która łapie
  lokalny obiekt `std::hash` przez referencję. Działa przypadkiem, bo obiekt jest pusty, ale referencja
  i tak wisi. Mój `SpotifyPlayer.cpp` łapie go przez wartość.

## Wydajność

`BellDSP::process()` przy każdej porcji zamienia każdą próbkę na float i z powrotem, nawet gdy żaden
efekt nie jest włączony. Na ESP32 dekoder był przez to tylko o około 5% szybszy od odtwarzania na żywo,
więc każda czkawka Wi-Fi kończyła się przerwą w muzyce. Mój odtwarzacz woła DSP tylko podczas wyciszania
na pauzę. Razem z buforem na 9 sekund (384 porcje w PSRAM) bufor napełnia się teraz około sześć razy
szybciej.

Jedna rzecz *nie* zadziałała: zwiększenie `HTTP_BUFFER_SIZE` w `CDNAudioFile` z 14 KB do 64 KB
zatrzymywało pobieranie po kilku sekundach. Zostało 14 KB.
