#include "sound_slot.h"
#include "config.h"
#include <cstring>

namespace {

constexpr uint8_t MAX_ARENA_ALLOCATIONS = NUM_SOUNDS + 2;
alignas(int16_t) uint8_t arena[SAMPLE_ARENA_BYTES];

struct ArenaAllocation {
    SoundSlot* owner;
    uint32_t offset;
    uint32_t size;
};

ArenaAllocation allocations[MAX_ARENA_ALLOCATIONS] = {};
uint8_t allocationCount = 0;
uint32_t arenaUsed = 0;

int findAllocation(const SoundSlot& slot) {
    for (uint8_t i = 0; i < allocationCount; i++) {
        if (allocations[i].owner == &slot) return i;
    }
    return -1;
}

void updatePointers(uint8_t first) {
    for (uint8_t i = first; i < allocationCount; i++) {
        allocations[i].owner->samples =
            reinterpret_cast<int16_t*>(arena + allocations[i].offset);
    }
}

void releaseAllocation(SoundSlot& slot) {
    int index = findAllocation(slot);
    if (index < 0) return;

    const uint32_t offset = allocations[index].offset;
    const uint32_t size = allocations[index].size;
    const uint32_t tailOffset = offset + size;
    const uint32_t tailSize = arenaUsed - tailOffset;
    if (tailSize > 0) {
        memmove(arena + offset, arena + tailOffset, tailSize);
    }

    for (uint8_t i = index + 1; i < allocationCount; i++) {
        allocations[i - 1] = allocations[i];
        allocations[i - 1].offset -= size;
    }
    allocationCount--;
    arenaUsed -= size;
    updatePointers(index);
}

}

#ifndef NATIVE_TEST
#include "platform/memory.h"
#else
namespace Memory { void trackAlloc(uint32_t) {} void trackFree(uint32_t) {} }
#endif

void SoundSlotOps::init(SoundSlot& slot) {
    slot.samples = nullptr;
    slot.length = 0;
    slot.allocLength = 0;
    slot.sampleRate = SAMPLE_RATE;
    slot.name[0] = '\0';
    slot.level = 100;
    slot.occupied = false;
    SlotFxOps::defaults(slot.fx);
}

bool SoundSlotOps::allocate(SoundSlot& slot, uint32_t maxLength) {
    uint32_t bytes = maxLength * sizeof(int16_t);
    int existing = findAllocation(slot);
    uint32_t reclaimable = existing >= 0 ? allocations[existing].size : 0;
    if (bytes == 0 || bytes > SAMPLE_ARENA_BYTES - arenaUsed + reclaimable) {
        return false;
    }
    if (existing < 0 && allocationCount >= MAX_ARENA_ALLOCATIONS) return false;

    // Capacity is known to fit, so replacing a slot cannot destroy its old
    // sample because of an allocation failure.
    free(slot);

    ArenaAllocation& allocation = allocations[allocationCount++];
    allocation.owner = &slot;
    allocation.offset = arenaUsed;
    allocation.size = bytes;
    slot.samples = reinterpret_cast<int16_t*>(arena + arenaUsed);
    arenaUsed += bytes;

    Memory::trackAlloc(bytes);
    memset(slot.samples, 0, bytes);
    slot.length = 0;
    slot.allocLength = maxLength;
    slot.sampleRate = SAMPLE_RATE;
    slot.level = 100;
    slot.occupied = false;
    SlotFxOps::defaults(slot.fx);
    return true;
}

bool SoundSlotOps::shrinkToFit(SoundSlot& slot) {
    if (!slot.samples || slot.length == 0) return false;

    uint32_t bytes = slot.length * sizeof(int16_t);
    int index = findAllocation(slot);
    if (index < 0 || bytes > allocations[index].size) return false;

    uint32_t released = allocations[index].size - bytes;
    if (released == 0) return true;

    uint32_t tailOffset = allocations[index].offset + allocations[index].size;
    uint32_t tailSize = arenaUsed - tailOffset;
    if (tailSize > 0) {
        memmove(arena + allocations[index].offset + bytes,
                arena + tailOffset, tailSize);
    }
    allocations[index].size = bytes;
    for (uint8_t i = index + 1; i < allocationCount; i++) {
        allocations[i].offset -= released;
    }
    arenaUsed -= released;
    updatePointers(index);
    Memory::trackFree(released);
    slot.allocLength = slot.length;
    return true;
}

void SoundSlotOps::free(SoundSlot& slot) {
    if (findAllocation(slot) >= 0) {
        Memory::trackFree(slot.allocLength * sizeof(int16_t));
    }
    releaseAllocation(slot);
    slot.samples = nullptr;
    slot.length = 0;
    slot.allocLength = 0;
    slot.occupied = false;
}

void SoundSlotOps::setName(SoundSlot& slot, const char* name) {
    strncpy(slot.name, name, 8);
    slot.name[8] = '\0';
}

uint32_t SoundSlotOps::capacityBytes() {
    return SAMPLE_ARENA_BYTES;
}

uint32_t SoundSlotOps::usedBytes() {
    return arenaUsed;
}

uint32_t SoundSlotOps::freeBytes() {
    return SAMPLE_ARENA_BYTES - arenaUsed;
}

uint32_t SoundSlotOps::availableSamples(const SoundSlot* replacing) {
    uint32_t bytes = freeBytes();
    if (replacing) {
        int index = findAllocation(*replacing);
        if (index >= 0) bytes += allocations[index].size;
    }
    return bytes / sizeof(int16_t);
}
