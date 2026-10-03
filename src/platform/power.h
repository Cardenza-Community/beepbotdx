#pragma once

#include <cstdint>

namespace Power {

inline bool hasBattery() {
#ifdef CARDENZA_TARGET
    return false;
#else
    return true;
#endif
}
uint8_t getBatteryPercent();

}
