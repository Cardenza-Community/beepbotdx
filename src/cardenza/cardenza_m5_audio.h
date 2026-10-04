// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Cardenza contributors
#pragma once
#include <M5Unified.h>

// Show an initialization failure after M5.begin() configured the display.
inline void cardenza_m5_require(bool ready, const char* message) {
    if (ready) return;
    Serial.printf("[Cardenza] %s; heap=%u\n", message, ESP.getFreeHeap());
    M5.Display.fillScreen(0);
    M5.Display.setTextColor(0xf800);
    M5.Display.setCursor(4,4);
    M5.Display.println(message);
    while (true) delay(100);
}
