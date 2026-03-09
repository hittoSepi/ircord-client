#pragma once

#include "voice/opus_codec.hpp"
#include "voice/jitter_buffer.hpp"
#include "voice/audio_device.hpp"
#include "state/app_state.hpp"
#include "config.hpp"
#include "ircord.pb.h"

#include <rtc/rtc.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ircord::voice {

// Callback to send a VoiceSignal envelope through the network layer
using SendSignalFn = std::function<void(const VoiceSignal&)>;

// Per-peer connection state
struct PeerConn {
    std::shared_ptr<rtc::PeerConnection>  pc;
    std::shared_ptr<rtc::Track>           track;
    JitterBuffer                          jitter_buf;
    OpusCodec                             codec;
    std::string                           peer_id;
    bool                                  connected = false;
};

class VoiceEngine {
public:
    VoiceEngine(AppState& state, const ClientConfig& cfg);
    ~VoiceEngine();

    void set_send_signal(SendSignalFn fn) { send_signal_ = std::move(fn); }

    // ── Room (multi-party) ────────────────────────────────────────────────
    void join_room(const std::string& channel_id);
    void leave_room();

    // ── 1:1 call ──────────────────────────────────────────────────────────
    void call(const std::string& peer_id);
    void accept_call(const std::string& caller_id);
    void hangup();

    // ── Controls ──────────────────────────────────────────────────────────
    void set_muted(bool muted);
    void set_deafened(bool deafened);

    // ── Signaling (called from MessageHandler) ───────────────────────────
    void on_voice_signal(const VoiceSignal& vs);

    // ── Audio callbacks (called from AudioDevice thread) ─────────────────
    void on_capture(const float* pcm, uint32_t frames);
    void mix_output(float* out, uint32_t frames);

    bool in_voice() const { return in_voice_; }

private:
    std::shared_ptr<PeerConn> get_or_create_peer(const std::string& peer_id,
                                                   bool is_offerer);
    void setup_peer_callbacks(std::shared_ptr<PeerConn> peer);
    rtc::Configuration make_rtc_config();

    void send_signal(const std::string& to_user, VoiceSignal::SignalType type,
                     const std::string& sdp_or_candidate);

    AppState&          state_;
    const ClientConfig& cfg_;
    SendSignalFn       send_signal_;

    std::mutex         mu_;
    std::unordered_map<std::string, std::shared_ptr<PeerConn>> peers_;

    AudioDevice        audio_;
    bool               in_voice_  = false;
    bool               muted_     = false;
    bool               deafened_  = false;
    std::string        active_channel_;

    uint16_t           rtp_seq_ = 0;
};

} // namespace ircord::voice
