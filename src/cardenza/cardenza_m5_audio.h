// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Cardenza contributors
#pragma once
#include <M5Unified.h>

// A small M5Unified adapter; use only after M5.begin() configured Ex_I2C pins.
inline void cardenza_m5_require(bool ready, const char* message) {
    if (ready) return;
    Serial.printf("[Cardenza] %s; heap=%u\n", message, ESP.getFreeHeap());
    M5.Display.fillScreen(0);
    M5.Display.setTextColor(0xf800);
    M5.Display.setCursor(4,4);
    M5.Display.println(message);
    while (true) delay(100);
}

inline bool cardenza_m5_audio_restore() {
    if (!M5.Ex_I2C.begin()) {
        Serial.println("[Cardenza] ES8156 resume: I2C init FAILED");
        return false;
    }
    struct Reg { uint8_t address, value; };
    const Reg regs[]={{0x11,0x30},{0x01,0xe1},{0x04,0x20},{0x05,0x01},{0x13,0x00}};
    for (const auto& reg:regs) {
        if (!M5.Ex_I2C.writeRegister8(0x08,reg.address,reg.value,400000) ||
            M5.Ex_I2C.readRegister8(0x08,reg.address,400000)!=reg.value) {
            Serial.println("[Cardenza] ES8156 resume FAILED");
            return false;
        }
    }
    return true;
}
