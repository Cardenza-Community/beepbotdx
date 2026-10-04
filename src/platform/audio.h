#pragma once

#include <cstdint>
#include "core/sound_slot.h"

struct SlotFx;

namespace Audio {

void init();
void update();

void recordStart(void* buffer, uint32_t maxLength, BitDepth bitDepth);
void recordUpdate();
bool recordStop(); // Capture remains in the slot even if speaker resume fails.
bool isRecording();
uint32_t getRecordedLength();

void triggerSound(const SoundSlot& slot, uint8_t volume = 255, const SlotFx* fx = nullptr);
void stopAll();

void setVolume(uint8_t vol);
uint8_t getVolume();

}
