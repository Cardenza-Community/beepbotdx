#include <unity.h>
#include "core/sound_slot.h"
#include "config.h"

static SoundSlot slots[3];

void setUp() {
    for (SoundSlot& slot : slots) {
        SoundSlotOps::free(slot);
        SoundSlotOps::init(slot);
    }
}

void tearDown() {
    for (SoundSlot& slot : slots) {
        SoundSlotOps::free(slot);
    }
}

void test_allocations_share_one_budget() {
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[0], 1000));
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[1], 2000));
    TEST_ASSERT_EQUAL_UINT32(6000, SoundSlotOps::usedBytes());
    TEST_ASSERT_EQUAL_UINT32(
        SAMPLE_ARENA_BYTES - 6000, SoundSlotOps::freeBytes());
}

void test_free_compacts_and_updates_later_pointers() {
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[0], 4));
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[1], 4));
    slots[1].samples[0] = 1234;
    int16_t* oldPointer = slots[1].samples;

    SoundSlotOps::free(slots[0]);

    TEST_ASSERT_NOT_EQUAL(oldPointer, slots[1].samples);
    TEST_ASSERT_EQUAL_INT16(1234, slots[1].samples[0]);
    TEST_ASSERT_EQUAL_UINT32(8, SoundSlotOps::usedBytes());
}

void test_shrink_compacts_without_a_second_allocation() {
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[0], 100));
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[1], 10));
    slots[0].length = 25;
    slots[1].samples[0] = -4321;

    TEST_ASSERT_TRUE(SoundSlotOps::shrinkToFit(slots[0]));

    TEST_ASSERT_EQUAL_UINT32(70, SoundSlotOps::usedBytes());
    TEST_ASSERT_EQUAL_INT16(-4321, slots[1].samples[0]);
}

void test_failed_replacement_preserves_existing_sample() {
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[0], 10));
    slots[0].samples[0] = 777;
    int16_t* oldPointer = slots[0].samples;

    TEST_ASSERT_FALSE(
        SoundSlotOps::allocate(slots[0], SAMPLE_ARENA_BYTES));

    TEST_ASSERT_EQUAL_PTR(oldPointer, slots[0].samples);
    TEST_ASSERT_EQUAL_INT16(777, slots[0].samples[0]);
}

void test_available_samples_includes_replaced_slot() {
    TEST_ASSERT_TRUE(SoundSlotOps::allocate(slots[0], 100));
    TEST_ASSERT_EQUAL_UINT32(
        SAMPLE_ARENA_BYTES / sizeof(int16_t),
        SoundSlotOps::availableSamples(&slots[0]));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_allocations_share_one_budget);
    RUN_TEST(test_free_compacts_and_updates_later_pointers);
    RUN_TEST(test_shrink_compacts_without_a_second_allocation);
    RUN_TEST(test_failed_replacement_preserves_existing_sample);
    RUN_TEST(test_available_samples_includes_replaced_slot);
    return UNITY_END();
}
