#include "platform/memory.h"
#include "core/sound_slot.h"

void Memory::init() {
}

uint32_t Memory::getSampleBudget() {
    return SoundSlotOps::capacityBytes();
}

uint32_t Memory::getFree() {
    return SoundSlotOps::freeBytes();
}

void Memory::trackAlloc(uint32_t bytes) { (void)bytes; }
void Memory::trackFree(uint32_t bytes) { (void)bytes; }
