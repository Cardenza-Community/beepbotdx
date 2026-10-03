// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Cardenza contributors
// GPIO transport adapted from the MIT-licensed 203 Launcher codec driver.
// Copyright (c) 2023 shikarunochi
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
  CARDENZA_SDA = 2, CARDENZA_SCL = 1, CARDENZA_CODEC_ADDRESS = 0x08,
  CARDENZA_LED_EN = 21, CARDENZA_I2S_BCLK = 41,
  CARDENZA_I2S_LRCK = 43, CARDENZA_I2S_DATA = 42,
  CARDENZA_HAS_GYRO = 0, CARDENZA_HAS_BATTERY_ADC = 0,
  CARDENZA_HAS_CHARGE_DETECT = 0, CARDENZA_HAS_WS2812 = 0
};

// Call before any I2C driver takes ownership of GPIO1/2. No Wire, heap,
// background task or I2S driver is created: the application still owns I2S.
#ifndef CARDENZA_HAL_TEST
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_sig_map.h"

static inline void cardenza_hal_delay(void) { esp_rom_delay_us(50); }
static inline void cardenza_hal_sda(bool release) {
  gpio_ll_set_level(&GPIO, (gpio_num_t)CARDENZA_SDA, release);
}
static inline void cardenza_hal_scl_low(void) {
  gpio_ll_set_level(&GPIO, (gpio_num_t)CARDENZA_SCL, 0);
}
static inline bool cardenza_hal_scl_high(void) {
  gpio_ll_set_level(&GPIO, (gpio_num_t)CARDENZA_SCL, 1);
  for (unsigned wait = 0; wait < 1000; wait += 10) {
    if (gpio_ll_get_level(&GPIO, (gpio_num_t)CARDENZA_SCL)) return true;
    esp_rom_delay_us(10);
  }
  return false;
}
static inline void cardenza_hal_bus_pin(unsigned pin) {
  esp_rom_gpio_pad_select_gpio(pin);
  esp_rom_gpio_pad_pullup_only(pin);
  gpio_ll_set_level(&GPIO, (gpio_num_t)pin, 1);
  gpio_ll_od_enable(&GPIO, (gpio_num_t)pin);
  esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
  gpio_ll_input_enable(&GPIO, (gpio_num_t)pin);
  gpio_ll_output_enable(&GPIO, (gpio_num_t)pin);
}
static inline bool cardenza_hal_stop(void) {
  cardenza_hal_scl_low(); cardenza_hal_sda(false); cardenza_hal_delay();
  bool ok = cardenza_hal_scl_high();
  cardenza_hal_delay(); cardenza_hal_sda(true); cardenza_hal_delay();
  return ok;
}
static inline bool cardenza_hal_bus_begin(void) {
  cardenza_hal_bus_pin(CARDENZA_SDA); cardenza_hal_bus_pin(CARDENZA_SCL);
  if (!cardenza_hal_scl_high()) return false;
  for (unsigned n = 0; n < 9 && !gpio_ll_get_level(&GPIO, (gpio_num_t)CARDENZA_SDA); ++n) {
    cardenza_hal_scl_low(); cardenza_hal_delay();
    if (!cardenza_hal_scl_high()) return false;
    cardenza_hal_delay();
  }
  return cardenza_hal_stop();
}
static inline void cardenza_hal_bus_end(void) {
  cardenza_hal_sda(true); gpio_ll_set_level(&GPIO, (gpio_num_t)CARDENZA_SCL, 1);
  gpio_ll_output_disable(&GPIO, (gpio_num_t)CARDENZA_SDA);
  gpio_ll_output_disable(&GPIO, (gpio_num_t)CARDENZA_SCL);
}
static inline bool cardenza_hal_start(void) {
  cardenza_hal_sda(true); cardenza_hal_delay();
  if (!cardenza_hal_scl_high()) return false;
  cardenza_hal_delay();
  if (!gpio_ll_get_level(&GPIO, (gpio_num_t)CARDENZA_SDA)) return false;
  cardenza_hal_sda(false); cardenza_hal_delay(); cardenza_hal_scl_low();
  return true;
}
static inline bool cardenza_hal_send(uint8_t byte) {
  for (unsigned bit = 0; bit < 8; ++bit) {
    cardenza_hal_sda((byte & 0x80) != 0); cardenza_hal_delay();
    if (!cardenza_hal_scl_high()) return false;
    cardenza_hal_delay(); cardenza_hal_scl_low(); byte <<= 1;
  }
  cardenza_hal_sda(true); cardenza_hal_delay();
  if (!cardenza_hal_scl_high()) return false;
  cardenza_hal_delay();
  bool ack = !gpio_ll_get_level(&GPIO, (gpio_num_t)CARDENZA_SDA);
  cardenza_hal_scl_low(); return ack;
}
static inline bool cardenza_hal_receive(uint8_t *byte) {
  *byte = 0; cardenza_hal_sda(true);
  for (unsigned bit = 0; bit < 8; ++bit) {
    cardenza_hal_delay();
    if (!cardenza_hal_scl_high()) return false;
    cardenza_hal_delay();
    *byte = (uint8_t)((*byte << 1) | gpio_ll_get_level(&GPIO, (gpio_num_t)CARDENZA_SDA));
    cardenza_hal_scl_low();
  }
  cardenza_hal_delay();
  if (!cardenza_hal_scl_high()) return false; // NACK final byte
  cardenza_hal_delay(); cardenza_hal_scl_low(); return true;
}
static inline bool cardenza_hal_write(uint8_t reg, uint8_t value) {
  bool ok = cardenza_hal_start() && cardenza_hal_send(CARDENZA_CODEC_ADDRESS << 1)
      && cardenza_hal_send(reg) && cardenza_hal_send(value);
  bool stop = cardenza_hal_stop(); return ok && stop;
}
static inline bool cardenza_hal_read(uint8_t reg, uint8_t *value) {
  bool ok = cardenza_hal_start() && cardenza_hal_send(CARDENZA_CODEC_ADDRESS << 1)
      && cardenza_hal_send(reg) && cardenza_hal_start()
      && cardenza_hal_send((CARDENZA_CODEC_ADDRESS << 1) | 1)
      && cardenza_hal_receive(value);
  bool stop = cardenza_hal_stop(); return ok && stop;
}
static inline void cardenza_hal_led_off(void) {
  // Output latch high alone cannot override a previous app's RMT route.
  gpio_hold_dis((gpio_num_t)CARDENZA_LED_EN);
  esp_rom_gpio_pad_select_gpio(CARDENZA_LED_EN);
  gpio_ll_set_level(&GPIO, (gpio_num_t)CARDENZA_LED_EN, 1);
  gpio_ll_od_disable(&GPIO, (gpio_num_t)CARDENZA_LED_EN);
  esp_rom_gpio_connect_out_signal(CARDENZA_LED_EN, SIG_GPIO_OUT_IDX, false, false);
  gpio_ll_output_enable(&GPIO, (gpio_num_t)CARDENZA_LED_EN);
  gpio_hold_en((gpio_num_t)CARDENZA_LED_EN);
}
#endif

static inline bool cardenza_hal_identity(void) {
  uint8_t msb = 0, lsb = 0, revision = 0;
  return cardenza_hal_read(0xFD, &msb) && cardenza_hal_read(0xFE, &lsb)
      && cardenza_hal_read(0xFF, &revision) && msb == 0x81 && lsb == 0x55
      && (revision & 0xF0) == 0x10;
}
static inline bool cardenza_hal_detect(void) {
  bool ok = cardenza_hal_bus_begin() && cardenza_hal_identity();
  cardenza_hal_bus_end(); return ok;
}

// Philips I2S stereo only. frame_bits is physical BCLK/LRCK, NOT sample width.
// 32fs = two 16-bit slots; 64fs = two 32-bit slots, including 16-bit samples.
// Reject unsupported formats before touching GPIO or registers.
static inline bool cardenza_hal_init(unsigned frame_bits, unsigned sample_bits) {
  if ((frame_bits != 32 && frame_bits != 64)
      || (sample_bits != 16 && sample_bits != 24 && sample_bits != 32)
      || 2 * sample_bits > frame_bits) return false;
  bool ok = cardenza_hal_bus_begin() && cardenza_hal_identity();
  if (ok) {
    cardenza_hal_led_off();
    const uint8_t pairs[][2] = {
      {0xFC,0x00}, {0x02,0x84}, {0x13,0x00}, {0x20,0x2A}, {0x21,0x3C},
      {0x22,0x02}, {0x24,0x07}, {0x23,0x40}, {0x0A,0x01}, {0x0B,0x01},
      {0x11,(uint8_t)(sample_bits == 16 ? 0x30 : sample_bits == 32 ? 0x40 : 0x00)},
      {0x14,0xBF}, {0x01,(uint8_t)(frame_bits == 32 ? 0xE1 : 0xA1)},
      {0x09,0x02}, {0x03,0x00}, {0x04,(uint8_t)frame_bits},
      {0x05,(uint8_t)(frame_bits == 32 ? 1 : 2)}, {0x0D,0x14},
      {0x18,0x00}, {0x08,0x3F}, {0x00,0x02}, {0x00,0x03}, {0x25,0x20}
    };
    for (unsigned n = 0; ok && n < sizeof(pairs)/sizeof(pairs[0]); ++n)
      ok = cardenza_hal_write(pairs[n][0], pairs[n][1]);
    // Read the actual interface, clock and gain values back; no false success.
    const uint8_t registers[] = {0x02,0x11,0x14,0x01,0x09,0x04,0x05};
    for (unsigned n = 0; ok && n < sizeof(registers); ++n) {
      uint8_t value = 0, expected = 0;
      for (unsigned k = 0; k < sizeof(pairs)/sizeof(pairs[0]); ++k)
        if (pairs[k][0] == registers[n]) expected = pairs[k][1];
      ok = cardenza_hal_read(registers[n], &value) && value == expected;
    }
  }
  cardenza_hal_bus_end(); return ok;
}
