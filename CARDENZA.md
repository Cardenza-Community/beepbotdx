# Runtime Cardputer / Cardenza build

Default environment: `unified` (`pio run -e unified`). One app image uses the normal M5Cardputer keyboard selection and the 203Null/M5Unified fork's ES8156 chip-ID detection. Original Cardputer V1 and ADV retain their upstream peripheral choices; Cardenza stays identified as original Cardputer for its matrix keyboard.

Dependencies: M5Cardputer 1.1.1, M5GFX 0.2.31, 203Null/M5Unified commit `74fe31c6d9a2bd7c04f81eb4f8f0af99262e3bc2`. No `CARDENZA_TARGET`, codec writes in the app, or Power/LED linker wrappers. The library owns LED hold, battery/IMU suppression, ES8156 initialization and speaker restart after PDM input. Audio uses M5 Speaker/Mic APIs.

Cardenza reports battery unavailable and disables onboard LED/shake options and shortcuts at runtime. ADV/V1 retain them. Microphone recording retains captured data and shows AUDIO INIT FAILED if playback cannot resume.

The unified target uses QIO 80 MHz, 8 MB flash and the QSPI SDK variant without `BOARD_HAS_PSRAM`. Cardenza has no gyro, battery sensing, charger or onboard RGB output. The original PDM microphone is retained (DATA46/CLK43); playback uses BCLK41/LRCK43/DATA42.

Build evidence lives in `../artifacts/beepbot-dx/unified`. Previous stock, Cardenza and ADV images remain immutable. Current unified compile/ELF inspection and host guards are separate from device proof: phase0 startup and functional audio/key/SD/recording checks are pending. Install only the app image through the Launcher; do not erase shared NVS or install the build's merged flash regions.

Publication CI retains `cardenza` as an alias of `unified`. An independent, unconditional `esp_partition_erase_range` linker guard blocks full NVS-partition erases while forwarding normal page GC and app/filesystem erases. The old Power/LED wrappers and compile-time device forcing are removed; the existing NVS host test remains.
