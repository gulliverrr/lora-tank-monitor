#pragma once

#include <cstdint>

namespace tank_monitor::protocol {

enum class SequenceStatus : std::uint8_t {
    First,
    Next,
    Gap,
    Duplicate,
    OutOfOrder,
    NewSession,
};

struct SequenceResult {
    SequenceStatus status{SequenceStatus::First};
    std::uint32_t missing_count{0};
};

class SequenceTracker {
public:
    [[nodiscard]] SequenceResult observe(std::uint32_t boot_nonce, std::uint32_t sequence);
    void reset();

private:
    bool initialized_{false};
    std::uint32_t boot_nonce_{0};
    std::uint32_t last_sequence_{0};
};

}  // namespace tank_monitor::protocol