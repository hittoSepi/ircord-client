#include "voice/opus_codec.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>

namespace ircord::voice {

OpusCodec::OpusCodec(int bitrate) : bitrate_(bitrate) {
    int err = OPUS_OK;

    enc_ = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !enc_) {
        spdlog::error("opus_encoder_create failed: {}", opus_strerror(err));
        return;
    }
    opus_encoder_ctl(enc_, OPUS_SET_BITRATE(bitrate_));
    opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(9));
    opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(enc_, OPUS_SET_DTX(1));

    dec_ = opus_decoder_create(kSampleRate, kChannels, &err);
    if (err != OPUS_OK || !dec_) {
        spdlog::error("opus_decoder_create failed: {}", opus_strerror(err));
    }
}

OpusCodec::~OpusCodec() {
    if (enc_) { opus_encoder_destroy(enc_); enc_ = nullptr; }
    if (dec_) { opus_decoder_destroy(dec_); dec_ = nullptr; }
}

std::vector<uint8_t> OpusCodec::encode(const std::vector<float>& pcm) {
    if (!enc_ || pcm.size() < static_cast<size_t>(kFrameSamples)) return {};

    std::vector<uint8_t> out(4000);  // max Opus packet size
    int encoded = opus_encode_float(enc_, pcm.data(), kFrameSamples,
                                    out.data(), static_cast<opus_int32>(out.size()));
    if (encoded < 0) {
        spdlog::warn("Opus encode error: {}", opus_strerror(encoded));
        return {};
    }
    out.resize(encoded);
    return out;
}

std::vector<float> OpusCodec::decode(const std::vector<uint8_t>& opus_data) {
    if (!dec_) return std::vector<float>(kFrameSamples, 0.0f);

    std::vector<float> out(kFrameSamples);
    int decoded = opus_decode_float(dec_,
                                    opus_data.data(),
                                    static_cast<opus_int32>(opus_data.size()),
                                    out.data(), kFrameSamples, 0);
    if (decoded < 0) {
        spdlog::warn("Opus decode error: {}", opus_strerror(decoded));
        return decode_plc();
    }
    return out;
}

std::vector<float> OpusCodec::decode_plc() {
    if (!dec_) return std::vector<float>(kFrameSamples, 0.0f);

    std::vector<float> out(kFrameSamples);
    // Pass nullptr + 0 for FEC/PLC
    opus_decode_float(dec_, nullptr, 0, out.data(), kFrameSamples, 0);
    return out;
}

} // namespace ircord::voice
