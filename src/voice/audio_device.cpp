// Define MA_IMPLEMENTATION in exactly one TU
#define MA_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "voice/audio_device.hpp"
#include <spdlog/spdlog.h>
#include <new>

namespace ircord::voice {

// ── Callback ─────────────────────────────────────────────────────────────────

void AudioDevice::data_callback(ma_device* dev, void* out, const void* in, uint32_t frames) {
    auto* self = static_cast<AudioDevice*>(dev->pUserData);
    if (!self) return;

    if (in && self->capture_cb_) {
        self->capture_cb_(static_cast<const float*>(in), frames);
    }
    if (out && self->playback_cb_) {
        self->playback_cb_(static_cast<float*>(out), frames);
    }
}

// ── AudioDevice ───────────────────────────────────────────────────────────────

AudioDevice::AudioDevice()
    : device_(new (std::nothrow) ma_device{}) {}

AudioDevice::~AudioDevice() {
    close();
    delete device_;
}

bool AudioDevice::open(const std::string& input_device,
                        const std::string& output_device,
                        CaptureCallback  on_capture,
                        PlaybackCallback on_playback) {
    if (open_) close();

    capture_cb_  = std::move(on_capture);
    playback_cb_ = std::move(on_playback);

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.sampleRate         = 48000;
    config.capture.format     = ma_format_f32;
    config.capture.channels   = 1;
    config.playback.format    = ma_format_f32;
    config.playback.channels  = 1;
    config.periodSizeInMilliseconds = 5;  // 5ms periods
    config.dataCallback       = data_callback;
    config.pUserData          = this;

    // Device selection (empty = default)
    // For named device selection, miniaudio requires enumeration first.
    // For now, always use system default; named device support can be added.
    (void)input_device;
    (void)output_device;

    ma_result result = ma_device_init(nullptr, &config, device_);
    if (result != MA_SUCCESS) {
        spdlog::error("ma_device_init failed: {}", static_cast<int>(result));
        return false;
    }

    open_ = true;
    spdlog::info("Audio device opened (48kHz, mono, duplex)");
    return true;
}

void AudioDevice::start() {
    if (!open_ || started_) return;
    ma_result r = ma_device_start(device_);
    if (r != MA_SUCCESS) {
        spdlog::error("ma_device_start failed: {}", static_cast<int>(r));
        return;
    }
    started_ = true;
}

void AudioDevice::stop() {
    if (!started_) return;
    ma_device_stop(device_);
    started_ = false;
}

void AudioDevice::close() {
    stop();
    if (open_) {
        ma_device_uninit(device_);
        open_ = false;
    }
}

std::vector<std::string> AudioDevice::list_input_devices() {
    std::vector<std::string> names;
    ma_context ctx{};
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return names;

    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&ctx, nullptr, nullptr, &infos, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            names.emplace_back(infos[i].name);
        }
    }
    ma_context_uninit(&ctx);
    return names;
}

std::vector<std::string> AudioDevice::list_output_devices() {
    std::vector<std::string> names;
    ma_context ctx{};
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return names;

    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&ctx, &infos, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            names.emplace_back(infos[i].name);
        }
    }
    ma_context_uninit(&ctx);
    return names;
}

} // namespace ircord::voice
