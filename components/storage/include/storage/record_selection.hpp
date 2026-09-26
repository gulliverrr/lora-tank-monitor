#pragma once

#include <cstdint>

namespace tank_monitor::storage {

enum class Slot : std::uint8_t {
    None = 0xff,
    Zero = 0,
    One = 1,
};

struct SlotState {
    bool valid{false};
    std::uint32_t generation{0};
};

[[nodiscard]] Slot select_newest(
    const SlotState& zero,
    const SlotState& one,
    Slot preferred_when_equal);

[[nodiscard]] constexpr Slot opposite(Slot slot)
{
    return slot == Slot::Zero ? Slot::One : Slot::Zero;
}

}  // namespace tank_monitor::storage