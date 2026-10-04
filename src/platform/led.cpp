#include "led.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <FastLED.h>

#define RGB_PIN 21
#define LED_EN 38
#define NUM_LEDS 1

static CRGB leds[NUM_LEDS];

void LED::init() {
    if (M5.isCardenza()) return;
    pinMode(LED_EN, OUTPUT);
    digitalWrite(LED_EN, HIGH);
    FastLED.addLeds<WS2812, RGB_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(64);
    leds[0] = CRGB::Black;
    FastLED.show();
}

void LED::setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (M5.isCardenza()) return;
    leds[0] = CRGB(r, g, b);
    FastLED.show();
}

void LED::off() {
    if (M5.isCardenza()) return;
    leds[0] = CRGB::Black;
    FastLED.show();
}
