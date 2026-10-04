#include "cardenza/cardenza_m5_audio.h"
#include <M5Cardputer.h>
#include "display.h"
#include "config.h"

static Canvas _canvas;
static bool _ready = false;

void Display::init() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    _ready = _canvas.create(SCREEN_WIDTH, SCREEN_HEIGHT);
    cardenza_m5_require(_ready,"Display memory FAILED");
}

void Display::beginFrame() {
    _canvas.fillScreen(TFT_BLACK);
}

void Display::endFrame() {
    if (!_ready) return;
    bool previousSwap = M5Cardputer.Display.getSwapBytes();
    M5Cardputer.Display.setSwapBytes(true);
    M5Cardputer.Display.pushImage(
        0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, _canvas.buffer());
    M5Cardputer.Display.setSwapBytes(previousSwap);
}

Canvas& Display::canvas() {
    return _canvas;
}

void Display::shutdown() {}

#define TFT_BL 38
#define MIN_BRIGHT 160

void Display::setBrightness(uint8_t value) {
    uint8_t mapped = MIN_BRIGHT + (uint16_t)value * (255 - MIN_BRIGHT) / 255;
    analogWrite(TFT_BL, mapped);
}

void Display::toggleFullscreen() {}
