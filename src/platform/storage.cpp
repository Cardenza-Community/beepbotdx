#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include "storage.h"
#include "core/project_header.h"
#include "core/fx_dsp.h"
#include "config.h"
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <cmath>

#ifndef NATIVE_TEST
#include <esp_heap_caps.h>
#endif

static bool _sdReady = false;

static bool supportedWavFormat(uint16_t format, uint16_t channels,
                               uint16_t bitsPerSample, uint32_t sampleRate) {
    if ((channels != 1 && channels != 2) || sampleRate == 0) return false;
    if (format == 1) {
        return bitsPerSample == 8 || bitsPerSample == 16 ||
               bitsPerSample == 24 || bitsPerSample == 32;
    }
    return format == 3 && bitsPerSample == 32;
}

static bool readWavValue(File& file, uint16_t format, uint16_t bitsPerSample,
                         int32_t& sample) {
    if (format == 1 && bitsPerSample == 8) {
        uint8_t value;
        if (file.read(&value, 1) != 1) return false;
        sample = ((int32_t)value - 128) * 256;
    } else if (format == 1 && bitsPerSample == 16) {
        int16_t value;
        if (file.read((uint8_t*)&value, 2) != 2) return false;
        sample = value;
    } else if (format == 1 && bitsPerSample == 24) {
        uint8_t bytes[3];
        if (file.read(bytes, 3) != 3) return false;
        int32_t value = (int32_t)((uint32_t)bytes[0] |
            ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16));
        if (value & 0x00800000) value |= (int32_t)0xFF000000;
        sample = value >> 8;
    } else if (format == 1 && bitsPerSample == 32) {
        int32_t value;
        if (file.read((uint8_t*)&value, 4) != 4) return false;
        sample = value >> 16;
    } else if (format == 3 && bitsPerSample == 32) {
        float value;
        if (file.read((uint8_t*)&value, 4) != 4) return false;
        if (!std::isfinite(value)) return false;
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        sample = (int32_t)(value * 32767.0f);
    } else {
        return false;
    }
    return true;
}

static bool readWavFrame(File& file, uint16_t format, uint16_t bitsPerSample,
                         uint16_t channels, int16_t& output) {
    int32_t left;
    if (!readWavValue(file, format, bitsPerSample, left)) return false;
    int64_t mixed = left;
    if (channels == 2) {
        int32_t right;
        if (!readWavValue(file, format, bitsPerSample, right)) return false;
        mixed = ((int64_t)left + right) / 2;
    }
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;
    output = (int16_t)mixed;
    return true;
}

bool Storage::init() {
    SPI.begin(SD_SPI_CLK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);
    _sdReady = SD.begin(SD_SPI_CS, SPI);
    Serial.printf("[storage] SD init: %s\n", _sdReady ? "OK" : "FAIL");
    return _sdReady;
}

bool Storage::isReady() {
    return _sdReady;
}

bool Storage::loadWav(SoundSlot& slot, const char* path, BitDepth targetBitDepth) {
    Serial.printf("[wav] opening: %s\n", path);

    File file = SD.open(path);
    if (!file) {
        Serial.println("[wav] file open failed");
        return false;
    }

    Serial.printf("[wav] file size: %d\n", file.size());

    // Read RIFF header (12 bytes)
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    if (file.read((uint8_t*)riff, 4) != 4) { file.close(); Serial.println("[wav] can't read RIFF"); return false; }
    file.read((uint8_t*)&fileSize, 4);
    if (file.read((uint8_t*)wave, 4) != 4) { file.close(); Serial.println("[wav] can't read WAVE"); return false; }

    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
        Serial.printf("[wav] not RIFF/WAVE: %.4s / %.4s\n", riff, wave);
        file.close();
        return false;
    }

    // Parse chunks to find fmt and data
    uint16_t numChannels = 1;
    uint32_t sampleRate = SAMPLE_RATE;
    uint16_t bitsPerSample = 16;
    uint16_t audioFormat = 1;
    bool fmtFound = false;

    while (file.available() >= 8) {
        char chunkId[4];
        uint32_t chunkSize;
        file.read((uint8_t*)chunkId, 4);
        file.read((uint8_t*)&chunkSize, 4);

        Serial.printf("[wav] chunk: %.4s size: %u pos: %u\n", chunkId, chunkSize, (unsigned)file.position());

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            size_t fmtStart = file.position();
            if (chunkSize < 16) {
                file.close();
                return false;
            }
            file.read((uint8_t*)&audioFormat, 2);
            file.read((uint8_t*)&numChannels, 2);
            file.read((uint8_t*)&sampleRate, 4);
            uint32_t byteRate;
            file.read((uint8_t*)&byteRate, 4);
            uint16_t blockAlign;
            file.read((uint8_t*)&blockAlign, 2);
            file.read((uint8_t*)&bitsPerSample, 2);
            fmtFound = true;

            Serial.printf("[wav] fmt: format=%u ch=%u rate=%u bits=%u\n",
                          audioFormat, numChannels, sampleRate, bitsPerSample);

            // Seek past any extra fmt bytes
            file.seek(fmtStart + chunkSize + (chunkSize & 1));

        } else if (memcmp(chunkId, "data", 4) == 0) {
            if (!fmtFound) {
                Serial.println("[wav] data before fmt");
                file.close();
                return false;
            }

            if (!supportedWavFormat(audioFormat, numChannels,
                                    bitsPerSample, sampleRate)) {
                Serial.printf("[wav] unsupported: fmt=%u ch=%u rate=%u bits=%u\n",
                              audioFormat, numChannels, sampleRate, bitsPerSample);
                file.close();
                return false;
            }

            uint32_t bytesPerSample = bitsPerSample / 8;
            uint32_t srcSamples = chunkSize / bytesPerSample / numChannels;

            // Downsample to SAMPLE_RATE if source is higher
            double ratio = (sampleRate > SAMPLE_RATE) ? (double)sampleRate / SAMPLE_RATE : 1.0;
            uint32_t outSamples = (uint32_t)(srcSamples / ratio);
            uint32_t formatMaximum = MAX_SAMPLE_LENGTH *
                (targetBitDepth == BIT_DEPTH_8 ? 2 : 1);
            if (outSamples > formatMaximum) outSamples = formatMaximum;

            Serial.printf("[wav] %u src @ %uHz -> %u out @ %uHz\n", srcSamples, sampleRate, outSamples, SAMPLE_RATE);

            if (!SoundSlotOps::allocate(slot, outSamples, targetBitDepth)) {
                Serial.println("[wav] alloc failed");
                file.close();
                return false;
            }

            bool needsResample = (ratio > 1.0);
            if (!needsResample) {
                for (uint32_t i = 0; i < outSamples; i++) {
                    int16_t sample;
                    if (!readWavFrame(file, audioFormat, bitsPerSample,
                                      numChannels, sample)) {
                        SoundSlotOps::free(slot);
                        file.close();
                        return false;
                    }
                    SoundSlotOps::setSample(slot, i, sample);
                }
            } else if (outSamples > 0) {
                // Linear resampling needs only the two source frames around the
                // current output position, keeping import memory constant.
                int16_t first;
                int16_t second;
                if (!readWavFrame(file, audioFormat, bitsPerSample,
                                  numChannels, first)) {
                    SoundSlotOps::free(slot);
                    file.close();
                    return false;
                }
                second = first;
                uint32_t baseIndex = 0;
                if (srcSamples > 1 &&
                    !readWavFrame(file, audioFormat, bitsPerSample,
                                  numChannels, second)) {
                    SoundSlotOps::free(slot);
                    file.close();
                    return false;
                }
                for (uint32_t i = 0; i < outSamples; i++) {
                    double srcPos = i * ratio;
                    uint32_t idx = (uint32_t)srcPos;
                    while (baseIndex < idx) {
                        first = second;
                        baseIndex++;
                        if (baseIndex + 1 < srcSamples &&
                            !readWavFrame(file, audioFormat, bitsPerSample,
                                          numChannels, second)) {
                            SoundSlotOps::free(slot);
                            file.close();
                            return false;
                        }
                    }
                    float frac = (float)(srcPos - idx);
                    SoundSlotOps::setSample(
                        slot, i, (int16_t)(first + frac * (second - first)));
                }
            }

            slot.length = outSamples;
            slot.sampleRate = SAMPLE_RATE;
            slot.occupied = true;

            // Extract filename for slot name
            const char* filename = strrchr(path, '/');
            if (filename) filename++;
            else filename = path;
            char nameOnly[9];
            strncpy(nameOnly, filename, 8);
            nameOnly[8] = '\0';
            char* dot = strrchr(nameOnly, '.');
            if (dot) *dot = '\0';
            SoundSlotOps::setName(slot, nameOnly);

            Serial.printf("[wav] loaded OK: %s, %u samples @ %u Hz\n", slot.name, slot.length, slot.sampleRate);
            file.close();
            return true;
        } else {
            // Skip unknown chunk
            file.seek(file.position() + chunkSize + (chunkSize & 1));
        }
    }

    Serial.println("[wav] no data chunk found");
    file.close();
    return false;
}

bool Storage::saveWav(const SoundSlot& slot, const char* path) {
    if (!slot.samples || slot.length == 0) return false;

    SD.mkdir("/beepbotdx");
    SD.mkdir("/beepbotdx/samples");

    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;

    uint8_t bytesPerSample = SoundSlotOps::bytesPerSample(slot.bitDepth);
    uint32_t dataSize = slot.length * bytesPerSample;
    uint32_t fileSize = 36 + dataSize;

    bool written = true;
    auto writeExact = [&](const void* data, size_t size) {
        if (written && file.write((const uint8_t*)data, size) != size) {
            written = false;
        }
    };

    // RIFF header
    writeExact("RIFF", 4);
    writeExact(&fileSize, 4);
    writeExact("WAVE", 4);

    // fmt chunk
    writeExact("fmt ", 4);
    uint32_t fmtSize = 16;
    writeExact(&fmtSize, 4);
    uint16_t audioFormat = 1;
    writeExact(&audioFormat, 2);
    uint16_t numChannels = 1;
    writeExact(&numChannels, 2);
    writeExact(&slot.sampleRate, 4);
    uint32_t byteRate = slot.sampleRate * bytesPerSample;
    writeExact(&byteRate, 4);
    uint16_t blockAlign = bytesPerSample;
    writeExact(&blockAlign, 2);
    uint16_t bitsPerSample = bytesPerSample * 8;
    writeExact(&bitsPerSample, 2);

    // data chunk
    writeExact("data", 4);
    writeExact(&dataSize, 4);
    if (slot.bitDepth == BIT_DEPTH_8) {
        for (uint32_t i = 0; i < slot.length; i++) {
            uint8_t sample = (uint8_t)(((const int8_t*)slot.samples)[i] + 128);
            writeExact(&sample, 1);
        }
    } else {
        writeExact(slot.samples, dataSize);
    }

    file.close();
    if (!written) return false;
    Serial.printf("[wav] saved: %s (%u samples)\n", path, slot.length);
    return true;
}

bool Storage::listWavFiles(const char* dir, char names[][32], uint8_t& count, uint8_t max) {
    count = 0;
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) return false;

    File entry = root.openNextFile();
    while (entry && count < max) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            if (name[0] == '.') { entry = root.openNextFile(); continue; }
            size_t len = strlen(name);
            if (len > 4 && strcasecmp(name + len - 4, ".wav") == 0) {
                strncpy(names[count], name, 31);
                names[count][31] = '\0';
                count++;
            }
        }
        entry = root.openNextFile();
    }

    root.close();

    // Reverse so newest files appear first
    for (int i = 0; i < count / 2; i++) {
        char tmp[32];
        memcpy(tmp, names[i], 32);
        memcpy(names[i], names[count - 1 - i], 32);
        memcpy(names[count - 1 - i], tmp, 32);
    }

    return true;
}


static void projectDir(uint8_t slot, char* buf, size_t len) {
    snprintf(buf, len, "/beepbotdx/%02d", slot + 1);
}

static void projectPath(uint8_t slot, char* buf, size_t len) {
    snprintf(buf, len, "/beepbotdx/%02d/project.dat", slot + 1);
}

bool Storage::projectExists(uint8_t slot) {
    if (!_sdReady || slot >= 8) return false;
    char path[48];
    projectPath(slot, path, sizeof(path));
    File file = SD.open(path);
    if (!file) return false;
    file.close();
    return true;
}

bool Storage::deleteProject(uint8_t slot) {
    if (!_sdReady || slot >= 8) return false;
    char path[48];
    projectPath(slot, path, sizeof(path));
    return SD.remove(path);
}

static bool readHeader(File& file, ProjectHeader& hdr) {
    memset(&hdr, 0, sizeof(hdr));
    size_t fileSize = file.size();
    static const size_t fxFieldsSize = sizeof(hdr.fxValues) + sizeof(hdr.fxEnabled);
    static const size_t fxFieldsOffset = offsetof(ProjectHeader, fxValues);
    if (fileSize <= sizeof(hdr) - fxFieldsSize) {
        uint8_t* p = (uint8_t*)&hdr;
        file.read(p, fxFieldsOffset);
        size_t remaining = fileSize - fxFieldsOffset;
        file.read(p + fxFieldsOffset + fxFieldsSize, remaining);
    } else {
        file.read((uint8_t*)&hdr, sizeof(hdr));
    }
    return (hdr.magic == PROJECT_MAGIC && hdr.version >= 1 && hdr.version <= PROJECT_VERSION);
}

uint8_t Storage::loadProjectTheme(uint8_t slot) {
    if (!_sdReady || slot >= 8) return 0;
    char path[48];
    projectPath(slot, path, sizeof(path));
    File file = SD.open(path);
    if (!file) return 0;
    ProjectHeader hdr;
    if (!readHeader(file, hdr)) { file.close(); return 0; }
    file.close();
    return hdr.themeIndex;
}

bool Storage::saveProjectTheme(uint8_t slot, uint8_t themeIndex) {
    if (!_sdReady || slot >= 8) return false;
    char path[48];
    projectPath(slot, path, sizeof(path));
    File file = SD.open(path);
    if (!file) return false;
    ProjectHeader hdr;
    if (!readHeader(file, hdr)) { file.close(); return false; }
    file.close();
    hdr.themeIndex = themeIndex;
    hdr.version = PROJECT_VERSION;
    file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    bool written = file.write((const uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
    file.close();
    return written;
}

bool Storage::saveProjectName(uint8_t slot, const char* name) {
    if (!_sdReady || slot >= 8) return false;
    char path[48];
    projectPath(slot, path, sizeof(path));
    File file = SD.open(path);
    if (!file) return false;
    ProjectHeader hdr;
    if (!readHeader(file, hdr)) { file.close(); return false; }
    file.close();
    strncpy(hdr.name, name, 8);
    hdr.name[8] = '\0';
    hdr.version = PROJECT_VERSION;
    file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    bool written = file.write((const uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
    file.close();
    return written;
}

uint16_t Storage::loadProjectBpm(uint8_t slot) {
    if (!_sdReady || slot >= 8) return DEFAULT_BPM;
    char path[48];
    projectPath(slot, path, sizeof(path));
    File file = SD.open(path);
    if (!file) return DEFAULT_BPM;
    ProjectHeader hdr;
    if (!readHeader(file, hdr)) { file.close(); return DEFAULT_BPM; }
    file.close();
    return hdr.bpm;
}

void Storage::loadProjectName(uint8_t slot, char* buf, uint8_t len) {
    buf[0] = '\0';
    if (!_sdReady || slot >= 8) return;
    char path[48];
    projectPath(slot, path, sizeof(path));
    File file = SD.open(path);
    if (!file) return;
    ProjectHeader hdr;
    if (!readHeader(file, hdr)) { file.close(); return; }
    file.close();
    strncpy(buf, hdr.name, len - 1);
    buf[len - 1] = '\0';
}

bool Storage::saveProject(const Project& project, uint8_t slot) {
    if (!_sdReady || slot >= 8) return false;

    char dir[32];
    projectDir(slot, dir, sizeof(dir));
    SD.mkdir("/beepbotdx");
    SD.mkdir(dir);

    // Save each occupied sound as WAV
    for (int i = 0; i < NUM_SOUNDS; i++) {
        if (project.sounds[i].occupied) {
            char path[64];
            snprintf(path, sizeof(path), "%s/s%d.wav", dir, i);
            if (!saveWav(project.sounds[i], path)) return false;
        }
    }

    // Save project metadata
    char datPath[48];
    projectPath(slot, datPath, sizeof(datPath));
    File file = SD.open(datPath, FILE_WRITE);
    if (!file) return false;

    ProjectHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = PROJECT_MAGIC;
    hdr.version = PROJECT_VERSION;
    hdr.bpm = project.bpm;
    hdr.themeIndex = project.themeIndex;
    hdr.bitDepth = (uint8_t)project.bitDepth;
    strncpy(hdr.name, project.name, 8);
    hdr.name[8] = '\0';
    memcpy(hdr.patterns, project.patterns, sizeof(hdr.patterns));
    memcpy(hdr.song, project.song, sizeof(hdr.song));

    for (int i = 0; i < NUM_SOUNDS; i++) {
        hdr.soundOccupied[i] = project.sounds[i].occupied ? 1 : 0;
        hdr.soundLevels[i] = project.sounds[i].level;
        hdr.soundBitDepth[i] = (uint8_t)project.sounds[i].bitDepth;
        if (project.sounds[i].occupied) {
            strncpy(hdr.soundNames[i], project.sounds[i].name, 8);
            hdr.soundNames[i][8] = '\0';
        }
        memcpy(hdr.fxValues[i], project.sounds[i].fx.value, NUM_FX);
        uint8_t bits = 0;
        for (int f2 = 0; f2 < NUM_FX; f2++) {
            if (project.sounds[i].fx.enabled[f2]) bits |= (1 << f2);
        }
        hdr.fxEnabled[i] = bits;
    }

    if (file.write((const uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        file.close();
        return false;
    }
    file.close();

    Serial.printf("[project] saved to slot %d\n", slot + 1);
    return true;
}

bool Storage::loadProject(Project& project, uint8_t slot) {
    if (!_sdReady || slot >= 8) return false;

    char datPath[48];
    projectPath(slot, datPath, sizeof(datPath));
    File file = SD.open(datPath);
    if (!file) return false;

    ProjectHeader hdr;
    if (!readHeader(file, hdr)) {
        file.close();
        Serial.println("[project] bad magic/version");
        return false;
    }
    file.close();

    project.bpm = hdr.bpm;
    project.themeIndex = hdr.themeIndex;
    project.bitDepth = (BitDepth)hdr.bitDepth;
    strncpy(project.name, hdr.name, 8);
    project.name[8] = '\0';
    memcpy(project.patterns, hdr.patterns, sizeof(project.patterns));
    memcpy(project.song, hdr.song, sizeof(project.song));

    char dir[32];
    projectDir(slot, dir, sizeof(dir));

    // Load sounds from WAV files
    for (int i = 0; i < NUM_SOUNDS; i++) {
        SoundSlotOps::free(project.sounds[i]);
        if (hdr.soundOccupied[i]) {
            char path[64];
            snprintf(path, sizeof(path), "%s/s%d.wav", dir, i);
            BitDepth slotDepth = hdr.version >= 4
                ? (BitDepth)hdr.soundBitDepth[i] : BIT_DEPTH_16;
            if (Storage::loadWav(project.sounds[i], path, slotDepth)) {
                SoundSlotOps::setName(project.sounds[i], hdr.soundNames[i]);
                project.sounds[i].level = hdr.soundLevels[i];
            }
        }
        if (hdr.version >= 2) {
            memcpy(project.sounds[i].fx.value, hdr.fxValues[i], NUM_FX);
            for (int f2 = 0; f2 < NUM_FX; f2++) {
                project.sounds[i].fx.enabled[f2] = (hdr.fxEnabled[i] & (1 << f2)) != 0;
            }
        } else {
            SlotFxOps::defaults(project.sounds[i].fx);
        }
    }

    Serial.printf("[project] loaded from slot %d\n", slot + 1);
    return true;
}

bool Storage::renderSongToWav(const Project& project, const char* path) {
    if (!_sdReady) return false;

    SD.mkdir("/beepbotdx");

    uint32_t samplesPerStep = SAMPLE_RATE * 60 / project.bpm / 4;

    uint8_t songLength = 0;
    for (uint8_t i = 0; i < NUM_SONG_POSITIONS; i++) {
        if (project.song[i] < NUM_PATTERNS) songLength = i + 1;
    }
    if (songLength == 0) songLength = 1;

    uint32_t totalSteps = songLength * NUM_STEPS;
    uint32_t totalSamples = totalSteps * samplesPerStep;

    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;

    uint32_t dataSize = totalSamples * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;

    // Write WAV header
    file.write((const uint8_t*)"RIFF", 4);
    file.write((const uint8_t*)&fileSize, 4);
    file.write((const uint8_t*)"WAVE", 4);
    file.write((const uint8_t*)"fmt ", 4);
    uint32_t fmtSize = 16;
    file.write((const uint8_t*)&fmtSize, 4);
    uint16_t audioFormat = 1;
    file.write((const uint8_t*)&audioFormat, 2);
    uint16_t numChannels = 1;
    file.write((const uint8_t*)&numChannels, 2);
    uint32_t sampleRate = SAMPLE_RATE;
    file.write((const uint8_t*)&sampleRate, 4);
    uint32_t byteRate = SAMPLE_RATE * 2;
    file.write((const uint8_t*)&byteRate, 4);
    uint16_t blockAlign = 2;
    file.write((const uint8_t*)&blockAlign, 2);
    uint16_t bitsPerSample = 16;
    file.write((const uint8_t*)&bitsPerSample, 2);
    file.write((const uint8_t*)"data", 4);
    file.write((const uint8_t*)&dataSize, 4);

    // Voice state for mixing
    struct Voice {
        const SoundSlot* slot;
        uint32_t srcRate;
        uint8_t level;
        bool active;
        SlotFx fx;
        FxFilterState filterState;
        double fracPos;
    };
    Voice voices[NUM_VOICES];
    memset(voices, 0, sizeof(voices));
    uint8_t nextVoice = 0;

    // Render chunk by chunk
    const uint32_t CHUNK = 512;
    int16_t chunk[CHUNK];

    uint32_t samplePos = 0;
    for (uint32_t step = 0; step < totalSteps; step++) {
        uint8_t songPos = step / NUM_STEPS;
        uint8_t patStep = step % NUM_STEPS;
        uint8_t patIdx = project.song[songPos];
        uint8_t triggers = (patIdx < NUM_PATTERNS) ? project.patterns[patIdx].steps[patStep] : 0;

        // Fire triggers
        for (uint8_t s = 0; s < NUM_SOUNDS; s++) {
            if ((triggers & (1 << s)) && project.sounds[s].occupied) {
                Voice& v = voices[nextVoice];
                v.slot = &project.sounds[s];
                v.srcRate = project.sounds[s].sampleRate;
                v.level = project.sounds[s].level;
                v.fracPos = 0.0;
                v.fx = project.sounds[s].fx;
                FxDsp::initFilterState(v.filterState);
                v.active = true;
                nextVoice = (nextVoice + 1) % NUM_VOICES;
            }
        }

        // Render this step's samples
        uint32_t stepEnd = samplePos + samplesPerStep;
        while (samplePos < stepEnd) {
            uint32_t toRender = stepEnd - samplePos;
            if (toRender > CHUNK) toRender = CHUNK;

            memset(chunk, 0, toRender * sizeof(int16_t));

            for (int v = 0; v < NUM_VOICES; v++) {
                if (!voices[v].active) continue;
                double step = (double)voices[v].srcRate / SAMPLE_RATE;
                if (voices[v].fx.enabled[FX_PITCH])
                    step *= FxDsp::pitchRate(voices[v].fx.value[FX_PITCH]);
                for (uint32_t i = 0; i < toRender; i++) {
                    uint32_t idx = (uint32_t)voices[v].fracPos;
                    if (idx >= voices[v].slot->length) {
                        voices[v].active = false;
                        break;
                    }
                    double frac = voices[v].fracPos - idx;
                    int16_t s;
                    if (idx + 1 < voices[v].slot->length)
                        s = (int16_t)(
                            SoundSlotOps::getSample(*voices[v].slot, idx) * (1.0 - frac) +
                            SoundSlotOps::getSample(*voices[v].slot, idx + 1) * frac);
                    else
                        s = SoundSlotOps::getSample(*voices[v].slot, idx);
                    s = FxDsp::processSample(s, voices[v].fx.value, voices[v].fx.enabled, voices[v].filterState, (float)voices[v].srcRate);
                    int32_t sample = (int32_t)s * voices[v].level / 100;
                    int32_t mixed = (int32_t)chunk[i] + sample;
                    if (mixed > 32767) mixed = 32767;
                    if (mixed < -32768) mixed = -32768;
                    chunk[i] = (int16_t)mixed;
                    voices[v].fracPos += step;
                }
            }

            file.write((const uint8_t*)chunk, toRender * sizeof(int16_t));
            samplePos += toRender;
        }
    }

    file.close();
    Serial.printf("[render] exported: %s (%u samples)\n", path, totalSamples);
    return true;
}
