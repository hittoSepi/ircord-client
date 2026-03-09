#pragma once
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

// miniaudio (header-only) — implementation TU defines MA_IMPLEMENTATION
struct ma_device;
struct ma_context;

namespace ircord::voice {

using CaptureCallback  = std::function<void(const float* pcm, uint32_t frames)>;
using PlaybackCallback = std::function<void(float* pcm, uint32_t frames)>;

// Wraps miniaudio capture + playback device.
class AudioDevice {
public:
    AudioDevice();
    ~AudioDevice();

    // List available device names (empty string = system default)
    static std::vector<std::string> list_input_devices();
    static std::vector<std::string> list_output_devices();

    // Open devices. Empty strings use system defaults.
    bool open(const std::string& input_device,
              const std::string& output_device,
              CaptureCallback  on_capture,
              PlaybackCallback on_playback);

    void start();
    void stop();
    void close();

    bool is_open()    const { return open_;    }
    bool is_started() const { return started_; }

private:
    ma_device*   device_  = nullptr;
    bool         open_    = false;
    bool         started_ = false;

    CaptureCallback  capture_cb_;
    PlaybackCallback playback_cb_;

    static void data_callback(ma_device* dev, void* out, const void* in, uint32_t frames);
};

} // namespace ircord::voice
