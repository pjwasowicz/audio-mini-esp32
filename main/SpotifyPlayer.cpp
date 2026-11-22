#include "SpotifyPlayer.h"

#include <cstdint>      // for uint8_t
#include <functional>   // for __base
#include <memory>       // for shared_ptr, make_shared, make_unique
#include <mutex>        // for scoped_lock
#include <string_view>  // for hash, string_view
#include <type_traits>  // for remove_extent_t
#include <utility>      // for move
#include <variant>      // for get
#include <vector>       // for vector

#include "BellDSP.h"             // for BellDSP, BellDSP::FadeEffect, BellDS...
#include "BellUtils.h"           // for BELL_SLEEP_MS
#include "CentralAudioBuffer.h"  // for CentralAudioBuffer::AudioChunk, Cent...
#include "Logger.h"
#include "SpircHandler.h"  // for SpircHandler, SpircHandler::EventType
#include "StreamInfo.h"    // for BitWidth, BitWidth::BW_16
#include "TrackPlayer.h"   // for TrackPlayer
#include "TrackQueue.h"    // for TrackInfo

#include "CircularBuffer.h"
#include "Sterownik.h"

namespace odtwarzacz {
std::atomic<int> bufor_proc{0};
std::atomic<uint32_t> przerwy{0};
}  // namespace odtwarzacz

SpotifyPlayer::SpotifyPlayer(std::unique_ptr<AudioSink> sink,
                     std::shared_ptr<cspot::SpircHandler> handler)
    : bell::Task("player", 8 * 1024, 0, 0) {
  this->handler = handler;
  this->audioSink = std::move(sink);

  // 384 porcje po 4 KB ≈ 9 s dźwięku (1,6 MB w PSRAM) – zapas na chwilowo słabsze Wi-Fi
  this->centralAudioBuffer =
      std::make_shared<bell::CentralAudioBuffer>(384);

#ifndef BELL_DISABLE_CODECS
  this->dsp = std::make_shared<bell::BellDSP>(this->centralAudioBuffer);
#endif

  // hash przez wartość: odwołanie do zmiennej lokalnej konstruktora wisiało po jego zakończeniu
  this->handler->getTrackPlayer()->setDataCallback(
      [this](uint8_t* data, size_t bytes, std::string_view trackId) {
        auto hash = std::hash<std::string_view>()(trackId);

        return this->centralAudioBuffer->writePCM(data, bytes, hash);
      });

  this->isPaused = false;

  this->handler->setEventHandler(
      [this](std::unique_ptr<cspot::SpircHandler::Event> event) {
        switch (event->eventType) {
          case cspot::SpircHandler::EventType::PLAY_PAUSE:
            if (std::get<bool>(event->data)) {
              this->pauseRequested = true;
            } else {
              this->isPaused = false;
              this->pauseRequested = false;
              this->wyciszanie = false;
            }
            sterownik::spotify_gra(!std::get<bool>(event->data));
            break;
          case cspot::SpircHandler::EventType::TRACK_INFO: {
            auto info = std::get<cspot::TrackInfo>(event->data);
            sterownik::Utwor u;
            u.tytul = info.name;
            u.wykonawca = info.artist;
            u.album = info.album;
            u.okladka = info.imageUrl;
            u.dlugosc_ms = info.duration;
            sterownik::spotify_utwor(u);
            break;
          }
          case cspot::SpircHandler::EventType::FLUSH: {
            this->centralAudioBuffer->clearBuffer();
            break;
          }
          case cspot::SpircHandler::EventType::DISC:
            this->centralAudioBuffer->clearBuffer();
            sterownik::spotify_nieaktywny();
            break;
          case cspot::SpircHandler::EventType::SEEK:
            this->centralAudioBuffer->clearBuffer();
            this->positionBaseMs = std::get<int>(event->data);
            this->playedBytes = 0;
            break;
          case cspot::SpircHandler::EventType::PLAYBACK_START:
            this->isPaused = true;
            this->playlistEnd = false;
            this->centralAudioBuffer->clearBuffer();
            break;
          case cspot::SpircHandler::EventType::DEPLETED:
            this->playlistEnd = true;
            break;
          case cspot::SpircHandler::EventType::VOLUME: {
            sterownik::spotify_glosnosc(std::get<int>(event->data));   // PT2258, nie programowo
            break;
          }
          default:
            break;
        }
      });
  startTask();
}

void SpotifyPlayer::feedData(uint8_t* data, size_t len) {}

void SpotifyPlayer::runTask() {
  std::vector<uint8_t> outBuf = std::vector<uint8_t>(1024);

  std::scoped_lock lock(runningMutex);
  bell::CentralAudioBuffer::AudioChunk* chunk;

  size_t lastHash = 0;
  bool bylyDane = false;                // do liczenia opróżnień bufora (przerw w dźwięku)

  while (isRunning) {
    auto& bufor = this->centralAudioBuffer->audioBuffer;
    odtwarzacz::bufor_proc = (int)(bufor->size() * 100 / bufor->capacity());
    if (!this->isPaused) {
      chunk = this->centralAudioBuffer->readChunk();

      if (this->pauseRequested) {
        this->pauseRequested = false;
#ifndef BELL_DISABLE_CODECS
        auto effect = std::make_unique<bell::BellDSP::FadeEffect>(44100 / 2, false, [this]() {
          this->isPaused = true;
          this->wyciszanie = false;
        });
        this->wyciszanie = true;
        this->dsp->queryInstantEffect(std::move(effect));
#else
        this->isPaused = true;
#endif
      }

      if (chunk && chunk->pcmSize > bell::CentralAudioBuffer::PCM_CHUNK_SIZE) {
        // porcja z niemożliwą długością (rozjechany bufor): wyrzucić, nie przetwarzać poza tablicą
        CSPOT_LOG(error, "Uszkodzona porcja audio (%u B), czyszczę bufor", (unsigned)chunk->pcmSize);
        this->centralAudioBuffer->clearBuffer();
        continue;
      }
      if (!chunk || chunk->pcmSize == 0) {
        if (bylyDane && !this->playlistEnd) odtwarzacz::przerwy++;   // grało, a danych zabrakło
        bylyDane = false;
        if (this->playlistEnd) {
            this->handler->notifyAudioEnded();
            this->playlistEnd = false;
        }
        BELL_SLEEP_MS(10);
        continue;
      } else {
        if (lastHash != chunk->trackHash) {
          lastHash = chunk->trackHash;
          this->handler->notifyAudioReachedPlayback();
          this->positionBaseMs = 0;
          this->playedBytes = 0;
        }
        bylyDane = true;
        this->playedBytes += chunk->pcmSize;
        // 44,1 kHz, 16 bit, stereo = 176,4 B/ms
        sterownik::spotify_pozycja(this->positionBaseMs + (uint32_t)(this->playedBytes * 10 / 1764));

#ifndef BELL_DISABLE_CODECS
        // DSP (zamiana każdej próbki na float i z powrotem) tylko przy wyciszaniu na pauzę – oszczędza procesor
        if (this->wyciszanie)
          this->dsp->process(chunk->pcmData, chunk->pcmSize, 2, 44100, bell::BitWidth::BW_16);
#endif
        this->audioSink->feedPCMFrames(chunk->pcmData, chunk->pcmSize);
      }
    } else {
      BELL_SLEEP_MS(10);
    }
  }
}

void SpotifyPlayer::disconnect() {
  isRunning = false;
  std::scoped_lock lock(runningMutex);
}
