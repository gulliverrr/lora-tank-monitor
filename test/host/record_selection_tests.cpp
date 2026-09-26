#include "storage/record_selection.hpp"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

}  // namespace

int main()
{
    using tank_monitor::storage::Slot;
    using tank_monitor::storage::SlotState;
    using tank_monitor::storage::select_newest;

    expect(select_newest({}, {}, Slot::None) == Slot::None, "empty store has no slot");
    expect(select_newest({true, 4}, {}, Slot::None) == Slot::Zero,
           "single valid zero slot is selected");
    expect(select_newest({}, {true, 5}, Slot::None) == Slot::One,
           "single valid one slot is selected");
    expect(select_newest({true, 8}, {true, 7}, Slot::One) == Slot::Zero,
           "higher generation wins over stale marker");
    expect(select_newest({true, 8}, {true, 9}, Slot::Zero) == Slot::One,
           "newer inactive slot survives torn marker update");
    expect(select_newest({true, 9}, {true, 9}, Slot::One) == Slot::One,
           "active marker resolves equal generations");
    expect(tank_monitor::storage::opposite(Slot::Zero) == Slot::One,
           "zero writes to inactive one slot");

    if (failures == 0) {
        std::cout << "All record selection tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}