#include "voice/voice_engine.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <variant>

namespace ircord::voice {

VoiceEngine::VoiceEngine(AppState& state, const ClientConfig& cfg)
    : state_(state), cfg_(cfg) {}

VoiceEngine::~VoiceEngine() {
    hangup();
    audio_.close();
}

rtc::Configuration VoiceEngine::make_rtc_config() {
    rtc::Configuration config;
    config.iceServers.push_back({"stun:stun.l.google.com:19302"});
    return config;
}

// ── Room ──────────────────────────────────────────────────────────────────────

void VoiceEngine::join_room(const std::string& channel_id) {
    if (in_voice_) leave_room();

    active_channel_ = channel_id;
    in_voice_       = true;
    muted_          = false;
    deafened_       = false;

    // Open audio device
    audio_.open(cfg_.voice.input_device, cfg_.voice.output_device,
        [this](const float* pcm, uint32_t frames) { on_capture(pcm, frames); },
        [this](float* out, uint32_t frames)        { mix_output(out, frames); });
    audio_.start();

    VoiceState vs = state_.voice_snapshot();
    vs.in_voice       = true;
    vs.active_channel = channel_id;
    state_.set_voice_state(vs);

    spdlog::info("Joined voice room: {}", channel_id);
}

void VoiceEngine::leave_room() {
    if (!in_voice_) return;

    audio_.stop();

    {
        std::lock_guard lk(mu_);
        peers_.clear();
    }

    in_voice_       = false;
    active_channel_.clear();

    VoiceState vs = state_.voice_snapshot();
    vs.in_voice        = false;
    vs.active_channel  = {};
    vs.participants    = {};
    state_.set_voice_state(vs);
}

// ── 1:1 call ──────────────────────────────────────────────────────────────────

void VoiceEngine::call(const std::string& peer_id) {
    if (in_voice_) leave_room();

    in_voice_       = true;
    active_channel_ = peer_id;

    audio_.open(cfg_.voice.input_device, cfg_.voice.output_device,
        [this](const float* pcm, uint32_t frames) { on_capture(pcm, frames); },
        [this](float* out, uint32_t frames)        { mix_output(out, frames); });
    audio_.start();

    // Create PeerConnection as offerer
    auto peer = get_or_create_peer(peer_id, /*is_offerer=*/true);

    // Send CALL_INVITE
    VoiceSignal invite;
    invite.set_from_user(state_.local_user_id());
    invite.set_to_user(peer_id);
    invite.set_signal_type(VoiceSignal::CALL_INVITE);
    if (send_signal_) send_signal_(invite);

    spdlog::info("Calling {}", peer_id);
}

void VoiceEngine::accept_call(const std::string& caller_id) {
    in_voice_       = true;
    active_channel_ = caller_id;

    audio_.open(cfg_.voice.input_device, cfg_.voice.output_device,
        [this](const float* pcm, uint32_t frames) { on_capture(pcm, frames); },
        [this](float* out, uint32_t frames)        { mix_output(out, frames); });
    audio_.start();

    // Create PeerConnection as answerer
    get_or_create_peer(caller_id, /*is_offerer=*/false);

    VoiceSignal accept;
    accept.set_from_user(state_.local_user_id());
    accept.set_to_user(caller_id);
    accept.set_signal_type(VoiceSignal::CALL_ACCEPT);
    if (send_signal_) send_signal_(accept);
}

void VoiceEngine::hangup() {
    if (!in_voice_) return;

    std::string peer = active_channel_;
    leave_room();

    VoiceSignal hup;
    hup.set_from_user(state_.local_user_id());
    hup.set_to_user(peer);
    hup.set_signal_type(VoiceSignal::CALL_HANGUP);
    if (send_signal_) send_signal_(hup);
}

// ── Controls ─────────────────────────────────────────────────────────────────

void VoiceEngine::set_muted(bool muted) {
    muted_ = muted;
    VoiceState vs = state_.voice_snapshot();
    vs.muted = muted;
    state_.set_voice_state(vs);
}

void VoiceEngine::set_deafened(bool deafened) {
    deafened_ = deafened;
    VoiceState vs = state_.voice_snapshot();
    vs.deafened = deafened;
    state_.set_voice_state(vs);
}

// ── Signaling ─────────────────────────────────────────────────────────────────

void VoiceEngine::on_voice_signal(const VoiceSignal& vs) {
    const std::string& from = vs.from_user();
    const std::string& sdp  = vs.sdp_or_ice();

    switch (vs.signal_type()) {
    case VoiceSignal::CALL_INVITE:
        spdlog::info("Incoming call from {}", from);
        state_.post_ui([this, from]() {
            Message msg;
            msg.type      = Message::Type::VoiceEvent;
            msg.sender_id = "voice";
            msg.content   = from + " is calling. Type /accept " + from + " to answer.";
            msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto ch = state_.active_channel().value_or("server");
            state_.push_message(ch, std::move(msg));
        });
        break;

    case VoiceSignal::CALL_ACCEPT: {
        auto peer = get_or_create_peer(from, true);
        if (peer && peer->pc) {
            peer->pc->setLocalDescription();
        }
        break;
    }

    case VoiceSignal::CALL_HANGUP:
        spdlog::info("{} hung up", from);
        {
            std::lock_guard lk(mu_);
            peers_.erase(from);
        }
        if (active_channel_ == from) leave_room();
        break;

    case VoiceSignal::OFFER: {
        auto peer = get_or_create_peer(from, false);
        if (peer && peer->pc) {
            peer->pc->setRemoteDescription(
                rtc::Description(sdp, rtc::Description::Type::Offer));
            peer->pc->setLocalDescription(rtc::Description::Type::Answer);
        }
        break;
    }

    case VoiceSignal::ANSWER: {
        std::shared_ptr<PeerConn> peer;
        {
            std::lock_guard lk(mu_);
            auto it = peers_.find(from);
            if (it != peers_.end()) peer = it->second;
        }
        if (peer && peer->pc) {
            peer->pc->setRemoteDescription(
                rtc::Description(sdp, rtc::Description::Type::Answer));
        }
        break;
    }

    case VoiceSignal::ICE_CANDIDATE: {
        std::shared_ptr<PeerConn> peer;
        {
            std::lock_guard lk(mu_);
            auto it = peers_.find(from);
            if (it != peers_.end()) peer = it->second;
        }
        if (peer && peer->pc) {
            peer->pc->addRemoteCandidate(rtc::Candidate(sdp));
        }
        break;
    }

    default: break;
    }
}

// ── Peer setup ────────────────────────────────────────────────────────────────

std::shared_ptr<PeerConn> VoiceEngine::get_or_create_peer(
    const std::string& peer_id, bool is_offerer)
{
    std::lock_guard lk(mu_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) return it->second;

    auto peer     = std::make_shared<PeerConn>();
    peer->peer_id = peer_id;
    peer->pc      = std::make_shared<rtc::PeerConnection>(make_rtc_config());

    peers_[peer_id] = peer;
    setup_peer_callbacks(peer);

    if (is_offerer) {
        // Add audio track
        auto desc = rtc::Description::Audio("audio", rtc::Description::Direction::SendRecv);
        desc.addOpusCodec(111);
        peer->track = peer->pc->addTrack(std::move(desc));
        peer->pc->setLocalDescription();
    }

    return peer;
}

void VoiceEngine::setup_peer_callbacks(std::shared_ptr<PeerConn> peer) {
    auto peer_id = peer->peer_id;

    peer->pc->onLocalDescription([this, peer_id](rtc::Description desc) {
        VoiceSignal sig;
        sig.set_from_user(state_.local_user_id());
        sig.set_to_user(peer_id);
        sig.set_sdp_or_ice(std::string(desc));
        sig.set_signal_type(desc.type() == rtc::Description::Type::Offer ?
                     VoiceSignal::OFFER : VoiceSignal::ANSWER);
        if (send_signal_) send_signal_(sig);
    });

    peer->pc->onLocalCandidate([this, peer_id](rtc::Candidate cand) {
        VoiceSignal sig;
        sig.set_from_user(state_.local_user_id());
        sig.set_to_user(peer_id);
        sig.set_sdp_or_ice(std::string(cand));
        sig.set_signal_type(VoiceSignal::ICE_CANDIDATE);
        if (send_signal_) send_signal_(sig);
    });

    peer->pc->onStateChange([this, peer_id, peer](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected) {
            spdlog::info("WebRTC connected to {}", peer_id);
            peer->connected = true;
        } else if (state == rtc::PeerConnection::State::Failed ||
                   state == rtc::PeerConnection::State::Disconnected) {
            spdlog::warn("WebRTC disconnected from {}", peer_id);
            peer->connected = false;
        }
    });

    peer->pc->onTrack([peer](std::shared_ptr<rtc::Track> track) {
        peer->track = track;
        track->onMessage([peer](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::binary>(msg)) return;
            const auto& data = std::get<rtc::binary>(msg);
            // Incoming RTP data → decode Opus → push to jitter buffer
            // Extract RTP payload (skip 12-byte header)
            if (data.size() <= 12) return;
            uint16_t seq = static_cast<uint16_t>(
                (static_cast<uint16_t>(std::to_integer<uint8_t>(data[2])) << 8) |
                 static_cast<uint16_t>(std::to_integer<uint8_t>(data[3])));
            std::vector<uint8_t> opus;
            opus.reserve(data.size() - 12);
            for (size_t i = 12; i < data.size(); ++i)
                opus.push_back(std::to_integer<uint8_t>(data[i]));
            auto pcm = peer->codec.decode(opus);
            peer->jitter_buf.push(seq, std::move(pcm));
        });
    });
}

// ── Audio I/O ─────────────────────────────────────────────────────────────────

void VoiceEngine::on_capture(const float* pcm, uint32_t frames) {
    if (muted_ || !in_voice_) return;
    if (frames < static_cast<uint32_t>(OpusCodec::kFrameSamples)) return;

    std::vector<float> pcm_vec(pcm, pcm + OpusCodec::kFrameSamples);

    std::lock_guard lk(mu_);
    for (auto& [pid, peer] : peers_) {
        if (!peer->connected || !peer->track) continue;
        auto opus = peer->codec.encode(pcm_vec);
        if (opus.empty()) continue;

        // Build minimal RTP packet (12 byte header + payload)
        std::vector<std::byte> rtp(12 + opus.size());
        rtp[0] = std::byte{0x80};                      // V=2
        rtp[1] = std::byte{0x6F};                      // PT=111 (Opus)
        rtp[2] = std::byte{static_cast<uint8_t>((rtp_seq_ >> 8) & 0xFF)};
        rtp[3] = std::byte{static_cast<uint8_t>(rtp_seq_ & 0xFF)};
        ++rtp_seq_;
        // Timestamp, SSRC left as zero for simplicity
        std::memcpy(rtp.data() + 12, opus.data(), opus.size());
        peer->track->send(rtp);
    }
}

void VoiceEngine::mix_output(float* out, uint32_t frames) {
    std::fill(out, out + frames, 0.0f);
    if (deafened_) return;

    std::lock_guard lk(mu_);
    for (auto& [pid, peer] : peers_) {
        auto frame = peer->jitter_buf.pop();
        const std::vector<float>* pcm_ptr = nullptr;
        std::vector<float> plc_frame;

        if (frame) {
            pcm_ptr = &(*frame);
        } else if (peer->connected) {
            plc_frame = peer->codec.decode_plc();
            pcm_ptr   = &plc_frame;
        }

        if (pcm_ptr) {
            uint32_t n = std::min(frames, static_cast<uint32_t>(pcm_ptr->size()));
            for (uint32_t i = 0; i < n; ++i) {
                out[i] += (*pcm_ptr)[i];
            }
        }
    }

    // Soft clip to [-1, 1]
    for (uint32_t i = 0; i < frames; ++i) {
        out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
    }
}

} // namespace ircord::voice
