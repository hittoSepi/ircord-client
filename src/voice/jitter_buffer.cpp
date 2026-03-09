#include "voice/jitter_buffer.hpp"
#include <algorithm>

namespace ircord::voice {

JitterBuffer::JitterBuffer(int target_delay_frames)
    : target_delay_(target_delay_frames) {}

void JitterBuffer::push(uint16_t seq, std::vector<float> pcm) {
    std::lock_guard lk(mu_);

    // Drop very old packets (outside window)
    if (primed_) {
        int16_t diff = static_cast<int16_t>(seq - next_seq_);
        if (diff < -kWindowSize || diff > kWindowSize * 2) return;
    }

    frames_[seq] = std::move(pcm);

    // Trim window
    while (static_cast<int>(frames_.size()) > kWindowSize) {
        frames_.erase(frames_.begin());
    }

    // Prime: wait until we have target_delay_ frames buffered
    if (!primed_ && static_cast<int>(frames_.size()) >= target_delay_) {
        primed_    = true;
        next_seq_  = frames_.begin()->first;
    }
}

std::optional<std::vector<float>> JitterBuffer::pop() {
    std::lock_guard lk(mu_);
    if (!primed_) return std::nullopt;

    auto it = frames_.find(next_seq_);
    ++next_seq_;  // always advance

    if (it == frames_.end()) {
        // Gap detected → caller should PLC
        return std::nullopt;
    }

    auto pcm = std::move(it->second);
    frames_.erase(it);
    return pcm;
}

void JitterBuffer::reset() {
    std::lock_guard lk(mu_);
    frames_.clear();
    primed_   = false;
    next_seq_ = 0;
}

int JitterBuffer::buffered_count() const {
    std::lock_guard lk(mu_);
    return static_cast<int>(frames_.size());
}

} // namespace ircord::voice
