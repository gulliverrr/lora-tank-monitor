#include "protocol/sequence_tracker.hpp"

namespace tank_monitor::protocol {

SequenceResult SequenceTracker::observe(std::uint32_t boot_nonce, std::uint32_t sequence)
{
    if (!initialized_) {
        initialized_ = true;
        boot_nonce_ = boot_nonce;
        last_sequence_ = sequence;
        return {SequenceStatus::First, 0};
    }
    if (boot_nonce != boot_nonce_) {
        boot_nonce_ = boot_nonce;
        last_sequence_ = sequence;
        return {SequenceStatus::NewSession, 0};
    }
    if (sequence == last_sequence_) {
        return {SequenceStatus::Duplicate, 0};
    }
    if (last_sequence_ == UINT32_MAX && sequence == 0) {
        last_sequence_ = sequence;
        return {SequenceStatus::Next, 0};
    }
    if (sequence < last_sequence_) {
        return {SequenceStatus::OutOfOrder, 0};
    }
    if (sequence == last_sequence_ + 1U) {
        last_sequence_ = sequence;
        return {SequenceStatus::Next, 0};
    }

    const std::uint32_t missing = sequence - last_sequence_ - 1U;
    last_sequence_ = sequence;
    return {SequenceStatus::Gap, missing};
}

void SequenceTracker::reset()
{
    initialized_ = false;
    boot_nonce_ = 0;
    last_sequence_ = 0;
}

}  // namespace tank_monitor::protocol