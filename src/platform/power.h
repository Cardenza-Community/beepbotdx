#pragma once

#include <cstdint>
#include "board.h"

namespace Power {

inline bool hasBattery() {
    return !Board::isCardenza();
}
uint8_t getBatteryPercent();

}
