#pragma once
#include "esp_log.h"

#include <vector>

#include <AudioSink.h>

#include "driver/i2s_std.h"

// Stereo PCM do DAC PCM5102A przez I2S (sterownik ESP-IDF 5.x), ze stałym ściszeniem −10 dB.
class I2SAudioSink : public AudioSink {
 public:
  I2SAudioSink(int bclk, int lrclk, int dout);
  ~I2SAudioSink() override;

  void feedPCMFrames(const uint8_t* buffer, size_t bytes) override;
  void volumeChanged(uint16_t) override {}   // głośność reguluje PT2258
  bool setParams(uint32_t sampleRate, uint8_t channelCount,
                 uint8_t bitDepth) override;

 private:
  i2s_chan_handle_t tx_ = nullptr;
  uint32_t sampleRate_ = 0;
  std::vector<int16_t> bufor_;               // próbki po stałym ściszeniu
};
