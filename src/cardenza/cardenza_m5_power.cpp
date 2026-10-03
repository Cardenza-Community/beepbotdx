// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Cardenza contributors
// M5Unified's automatic Power.begin otherwise initializes a battery ADC.
// Compile/link only for an explicit Cardenza target, using:
// -Wl,--wrap=_ZN2m511Power_Class5beginEv
// -Wl,--wrap=_ZN2m59M5Unified10_setup_ledEN4lgfx6boards7board_tE
#if defined(CARDENZA_TARGET)
extern "C" bool __wrap__ZN2m511Power_Class5beginEv(void *) { return true; }
// Newer M5Unified otherwise allocates RMT and configures the RGB LED at begin.
extern "C" void __wrap__ZN2m59M5Unified10_setup_ledEN4lgfx6boards7board_tE(void *, int) {}

// Arduino must not erase Launcher settings when its NVS version differs.
#include <esp_partition.h>
extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size) {
    if (partition && partition->type == ESP_PARTITION_TYPE_DATA &&
        partition->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS &&
        offset == 0 && size == partition->size) return ESP_ERR_NOT_SUPPORTED;
    return __real_esp_partition_erase_range(partition, offset, size);
}
#endif
