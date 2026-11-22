#pragma once

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint8_t
#include <atomic>    // for atomic
#include <memory>    // for shared_ptr, unique_ptr
#include <mutex>     // for mutex
#include <string>    // for string

#include "AudioSink.h"  // for AudioSink
#include "BellTask.h"   // for Task

namespace bell {
class BellDSP;
class CentralAudioBuffer;
}  // namespace bell
namespace cspot {
class SpircHandler;
}  // namespace cspot

// diagnostyka dla /api/system: wypełnienie bufora audio (%) i liczba opróżnień w trakcie grania
namespace odtwarzacz {
extern std::atomic<int> bufor_proc;
extern std::atomic<uint32_t> przerwy;
}  // namespace odtwarzacz

class SpotifyPlayer : public bell::Task {
 public:
  SpotifyPlayer(std::unique_ptr<AudioSink> sink,
            std::shared_ptr<cspot::SpircHandler> spircHandler);
  void disconnect();

 private:
  std::string currentTrackId;
  std::shared_ptr<cspot::SpircHandler> handler;
  std::shared_ptr<bell::BellDSP> dsp;
  std::unique_ptr<AudioSink> audioSink;
  std::shared_ptr<bell::CentralAudioBuffer> centralAudioBuffer;

  void feedData(uint8_t* data, size_t len);

  std::atomic<bool> pauseRequested = false;
  std::atomic<bool> wyciszanie = false;   // trwa wyciszanie przed pauzą (DSP potrzebny tylko wtedy)
  std::atomic<bool> isPaused = true;
  std::atomic<bool> isRunning = true;
  std::mutex runningMutex;
  std::atomic<bool> playlistEnd = false;
  std::atomic<uint32_t> positionBaseMs = 0;   // pozycja po przewinięciu (SEEK)
  std::atomic<uint64_t> playedBytes = 0;      // bajty PCM od początku utworu / przewinięcia

  void runTask() override;
};
