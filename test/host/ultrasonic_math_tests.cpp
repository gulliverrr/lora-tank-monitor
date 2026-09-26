#include "sensor/ultrasonic_math.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

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
    expect(tank_monitor::sensor::echo_microseconds_to_millimetres(0) == 0,
           "zero pulse has zero distance");
    expect(tank_monitor::sensor::echo_microseconds_to_millimetres(1000) == 172,
           "one millisecond pulse converts with round-trip compensation");
    expect(tank_monitor::sensor::echo_microseconds_to_millimetres(11662) == 2000,
           "two metre round trip converts correctly");
    expect(tank_monitor::sensor::echo_microseconds_to_millimetres(
               std::numeric_limits<std::uint32_t>::max()) == 736586891U,
           "conversion uses wide arithmetic without overflow");

    if (failures == 0) {
        std::cout << "All ultrasonic math tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}