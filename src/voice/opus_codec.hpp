#pragma once
#include <opus/opus.h>
#include <cstdint>
#include <vector>

namespace ircord::voice {

// RAII Opus encoder/decoder pair.
// Config: 48kHz, mono, 20ms frames → 960 samples per frame.
class OpusCodec {
public:
    static constexpr int kSampleRate  = 48000;
    static constexpr int kChannels    = 1;
    static constexpr int kFrameMs     = 20;
    static constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000; // 960

    explicit OpusCodec(int bitrate = 64000);
    ~OpusCodec();

    // Encode one frame of PCM float samples → compressed Opus bytes.
    // Input must be exactly kFrameSamples floats.
    std::vector<uint8_t> encode(const std::vector<float>& pcm);

    // Decode Opus bytes → PCM float samples.
    // Returns kFrameSamples floats, or FEC/PLC silence on error.
    std::vector<float> decode(const std::vector<uint8_t>& opus_data);

    // Decode with packet loss concealment (empty input = pure PLC).
    std::vector<float> decode_plc();

    bool ready() const { return enc_ && dec_; }

private:
    OpusEncoder* enc_ = nullptr;
    OpusDecoder* dec_ = nullptr;
    int          bitrate_;
};

} // namespace ircord::voice
