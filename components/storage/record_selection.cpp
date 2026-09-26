#include "storage/record_selection.hpp"

namespace tank_monitor::storage {

Slot select_newest(
    const SlotState& zero,
    const SlotState& one,
    Slot preferred_when_equal)
{
    if (!zero.valid && !one.valid) {
        return Slot::None;
    }
    if (zero.valid && !one.valid) {
        return Slot::Zero;
    }
    if (!zero.valid && one.valid) {
        return Slot::One;
    }
    if (zero.generation == one.generation) {
        return preferred_when_equal == Slot::One ? Slot::One : Slot::Zero;
    }
    return zero.generation > one.generation ? Slot::Zero : Slot::One;
}

}  // namespace tank_monitor::storage