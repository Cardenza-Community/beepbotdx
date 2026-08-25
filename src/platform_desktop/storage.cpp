#include "platform/storage.h"
#include "core/project_header.h"
#include "core/fx_dsp.h"
#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <dirent.h>
#include <sys/stat.h>

static const char* BASE_DIR = "beepbotdx_data";
static bool _ready = false;

static bool supportedWavFormat(uint16_t format, uint16_t channels,
                               uint16_t bitsPerSample, uint32_t sampleRate) {
    if ((channels != 1 && channels != 2) || sampleRate == 0) return false;
    if (format == 1) {
        return bitsPerSample == 8 || bitsPerSample == 16 ||
               bitsPerSample == 24 || bitsPerSample == 32;
    }
    return format == 3 && bitsPerSample == 32;
}

static bool readWavValue(FILE* file, uint16_t format, uint16_t bitsPerSample,
                         int32_t& sample) {
    if (format == 1 && bitsPerSample == 8) {
        uint8_t value;
        if (fread(&value, 1, 1, file) != 1) return false;
        sample = ((int32_t)value - 128) * 256;
    } else if (format == 1 && bitsPerSample == 16) {
        int16_t value;
        if (fread(&value, 2, 1, file) != 1) return false;
        sample = value;
    } else if (format == 1 && bitsPerSample == 24) {
        uint8_t bytes[3];
        if (fread(bytes, 1, 3, file) != 3) return false;
        int32_t value = (int32_t)((uint32_t)bytes[0] |
            ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16));
        if (value & 0x00800000) value |= (int32_t)0xFF000000;
        sample = value >> 8;
    } else if (format == 1 && bitsPerSample == 32) {
        int32_t value;
        if (fread(&value, 4, 1, file) != 1) return false;
        sample = value >> 16;
    } else if (format == 3 && bitsPerSample == 32) {
        float value;
        if (fread(&value, 4, 1, file) != 1) return false;
        if (!std::isfinite(value)) return false;
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        sample = (int32_t)(value * 32767.0f);
    } else {
        return false;
    }
    return true;
}

static bool readWavFrame(FILE* file, uint16_t format, uint16_t bitsPerSample,
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

static void ensureDir(const char* path) {
    mkdir(path, 0755);
}

bool Storage::init() {
    ensureDir(BASE_DIR);
    char samples[64];
    snprintf(samples, sizeof(samples), "%s/samples", BASE_DIR);
    ensureDir(samples);
    _ready = true;
    return true;
}

bool Storage::isReady() {
    return _ready;
}

bool Storage::loadWav(SoundSlot& slot, const char* path, BitDepth targetBitDepth) {
    char fullPath[128];
    if (strncmp(path, "/beepbotdx/", 11) == 0) {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", BASE_DIR, path + 11);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s", path);
    }

    FILE* f = fopen(fullPath, "rb");
    if (!f) return false;

    // Read RIFF header
    char riff[4], wave[4];
    uint32_t fileSize;
    if (fread(riff, 1, 4, f) != 4) { fclose(f); return false; }
    fread(&fileSize, 4, 1, f);
    if (fread(wave, 1, 4, f) != 4) { fclose(f); return false; }

    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
        fclose(f);
        return false;
    }

    uint16_t numChannels = 1;
    uint32_t sampleRate = SAMPLE_RATE;
    uint16_t bitsPerSample = 16;
    uint16_t audioFormat = 1;
    bool fmtFound = false;

    while (true) {
        char chunkId[4];
        uint32_t chunkSize;
        if (fread(chunkId, 1, 4, f) != 4) break;
        if (fread(&chunkSize, 4, 1, f) != 1) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            long fmtStart = ftell(f);
            if (chunkSize < 16) { fclose(f); return false; }
            fread(&audioFormat, 2, 1, f);
            fread(&numChannels, 2, 1, f);
            fread(&sampleRate, 4, 1, f);
            uint32_t byteRate;
            fread(&byteRate, 4, 1, f);
            uint16_t blockAlign;
            fread(&blockAlign, 2, 1, f);
            fread(&bitsPerSample, 2, 1, f);
            fmtFound = true;
            fseek(f, fmtStart + chunkSize + (chunkSize & 1), SEEK_SET);

        } else if (memcmp(chunkId, "data", 4) == 0) {
            if (!fmtFound) { fclose(f); return false; }

            if (!supportedWavFormat(audioFormat, numChannels,
                                    bitsPerSample, sampleRate)) {
                fclose(f);
                return false;
            }

            uint32_t bytesPerSample = bitsPerSample / 8;
            uint32_t srcSamples = chunkSize / bytesPerSample / numChannels;

            double ratio = (sampleRate > SAMPLE_RATE) ? (double)sampleRate / SAMPLE_RATE : 1.0;
            uint32_t outSamples = (uint32_t)(srcSamples / ratio);
            uint32_t formatMaximum = MAX_SAMPLE_LENGTH *
                (targetBitDepth == BIT_DEPTH_8 ? 2 : 1);
            if (outSamples > formatMaximum) outSamples = formatMaximum;

            bool needsResample = (ratio > 1.0);

            if (!SoundSlotOps::allocate(slot, outSamples, targetBitDepth)) {
                fclose(f);
                return false;
            }

            if (!needsResample) {
                for (uint32_t i = 0; i < outSamples; i++) {
                    int16_t sample;
                    if (!readWavFrame(f, audioFormat, bitsPerSample,
                                      numChannels, sample)) {
                        SoundSlotOps::free(slot);
                        fclose(f);
                        return false;
                    }
                    SoundSlotOps::setSample(slot, i, sample);
                }
            } else if (outSamples > 0) {
                int16_t first;
                int16_t second;
                if (!readWavFrame(f, audioFormat, bitsPerSample,
                                  numChannels, first)) {
                    SoundSlotOps::free(slot);
                    fclose(f);
                    return false;
                }
                second = first;
                uint32_t baseIndex = 0;
                if (srcSamples > 1 &&
                    !readWavFrame(f, audioFormat, bitsPerSample,
                                  numChannels, second)) {
                    SoundSlotOps::free(slot);
                    fclose(f);
                    return false;
                }
                for (uint32_t i = 0; i < outSamples; i++) {
                    double srcPos = i * ratio;
                    uint32_t idx = (uint32_t)srcPos;
                    while (baseIndex < idx) {
                        first = second;
                        baseIndex++;
                        if (baseIndex + 1 < srcSamples &&
                            !readWavFrame(f, audioFormat, bitsPerSample,
                                          numChannels, second)) {
                            SoundSlotOps::free(slot);
                            fclose(f);
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

            const char* filename = strrchr(path, '/');
            if (filename) filename++;
            else filename = path;
            char nameOnly[9];
            strncpy(nameOnly, filename, 8);
            nameOnly[8] = '\0';
            char* dot = strrchr(nameOnly, '.');
            if (dot) *dot = '\0';
            SoundSlotOps::setName(slot, nameOnly);

            fclose(f);
            return true;
        } else {
            fseek(f, chunkSize + (chunkSize & 1), SEEK_CUR);
        }
    }

    fclose(f);
    return false;
}

bool Storage::saveWav(const SoundSlot& slot, const char* path) {
    if (!slot.samples || slot.length == 0) return false;

    char fullPath[128];
    if (strncmp(path, "/beepbotdx/", 11) == 0) {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", BASE_DIR, path + 11);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s", path);
    }

    FILE* f = fopen(fullPath, "wb");
    if (!f) return false;

    uint8_t bytesPerSample = SoundSlotOps::bytesPerSample(slot.bitDepth);
    uint32_t dataSize = slot.length * bytesPerSample;
    uint32_t fileSize = 36 + dataSize;

    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16; fwrite(&fmtSize, 4, 1, f);
    uint16_t audioFmt = 1; fwrite(&audioFmt, 2, 1, f);
    uint16_t channels = 1; fwrite(&channels, 2, 1, f);
    uint32_t sr = slot.sampleRate; fwrite(&sr, 4, 1, f);
    uint32_t byteRate = sr * bytesPerSample; fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = bytesPerSample; fwrite(&blockAlign, 2, 1, f);
    uint16_t bps = bytesPerSample * 8; fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    if (slot.bitDepth == BIT_DEPTH_8) {
        for (uint32_t i = 0; i < slot.length; i++) {
            uint8_t sample = (uint8_t)(((const int8_t*)slot.samples)[i] + 128);
            fwrite(&sample, 1, 1, f);
        }
    } else {
        fwrite(slot.samples, 2, slot.length, f);
    }
    bool written = ferror(f) == 0 && fflush(f) == 0;
    if (fclose(f) != 0) written = false;
    return written;
}

bool Storage::listWavFiles(const char* dir, char names[][32], uint8_t& count, uint8_t max) {
    char fullPath[128];
    if (strncmp(dir, "/beepbotdx/", 11) == 0) {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", BASE_DIR, dir + 11);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s", dir);
    }

    DIR* d = opendir(fullPath);
    if (!d) return false;

    count = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr && count < max) {
        const char* name = entry->d_name;
        int len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".wav") == 0) {
            strncpy(names[count], name, 31);
            names[count][31] = '\0';
            count++;
        }
    }
    closedir(d);
    return count > 0;
}

static void projectDir(uint8_t slot, char* buf, size_t len) {
    snprintf(buf, len, "%s/%02d", BASE_DIR, slot + 1);
}

static void projectPath(uint8_t slot, char* buf, size_t len) {
    snprintf(buf, len, "%s/%02d/project.dat", BASE_DIR, slot + 1);
}

bool Storage::projectExists(uint8_t slot) {
    if (!_ready || slot >= 8) return false;
    char path[80];
    projectPath(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

bool Storage::deleteProject(uint8_t slot) {
    if (!_ready || slot >= 8) return false;
    char path[80];
    projectPath(slot, path, sizeof(path));
    return remove(path) == 0;
}

uint8_t Storage::loadProjectTheme(uint8_t slot) {
    if (!_ready || slot >= 8) return 0;
    char path[80];
    projectPath(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    ProjectHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    fread(&hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (hdr.magic != PROJECT_MAGIC || hdr.version < 1 || hdr.version > PROJECT_VERSION) return 0;
    return hdr.themeIndex;
}

bool Storage::saveProjectTheme(uint8_t slot, uint8_t themeIndex) {
    if (!_ready || slot >= 8) return false;
    char path[80];
    projectPath(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    ProjectHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    static const size_t fxFieldsSize = sizeof(hdr.fxValues) + sizeof(hdr.fxEnabled);
    static const size_t fxFieldsOffset = offsetof(ProjectHeader, fxValues);
    if (fileSize <= sizeof(hdr) - fxFieldsSize) {
        uint8_t* p = (uint8_t*)&hdr;
        fread(p, 1, fxFieldsOffset, f);
        size_t remaining = fileSize - fxFieldsOffset;
        fread(p + fxFieldsOffset + fxFieldsSize, 1, remaining, f);
    } else {
        fread(&hdr, sizeof(hdr), 1, f);
    }
    fclose(f);
    if (hdr.magic != PROJECT_MAGIC || hdr.version < 1 || hdr.version > PROJECT_VERSION) return false;
    hdr.themeIndex = themeIndex;
    hdr.version = PROJECT_VERSION;
    f = fopen(path, "wb");
    if (!f) return false;
    fwrite(&hdr, sizeof(hdr), 1, f);
    fclose(f);
    return true;
}

bool Storage::saveProjectName(uint8_t slot, const char* name) {
    if (!_ready || slot >= 8) return false;
    char path[80];
    projectPath(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    ProjectHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    static const size_t fxFieldsSize = sizeof(hdr.fxValues) + sizeof(hdr.fxEnabled);
    static const size_t fxFieldsOffset = offsetof(ProjectHeader, fxValues);
    if (fileSize <= sizeof(hdr) - fxFieldsSize) {
        uint8_t* p = (uint8_t*)&hdr;
        fread(p, 1, fxFieldsOffset, f);
        size_t remaining = fileSize - fxFieldsOffset;
        fread(p + fxFieldsOffset + fxFieldsSize, 1, remaining, f);
    } else {
        fread(&hdr, sizeof(hdr), 1, f);
    }
    fclose(f);
    if (hdr.magic != PROJECT_MAGIC || hdr.version < 1 || hdr.version > PROJECT_VERSION) return false;
    strncpy(hdr.name, name, 8);
    hdr.name[8] = '\0';
    hdr.version = PROJECT_VERSION;
    f = fopen(path, "wb");
    if (!f) return false;
    fwrite(&hdr, sizeof(hdr), 1, f);
    fclose(f);
    return true;
}

uint16_t Storage::loadProjectBpm(uint8_t slot) {
    if (!_ready || slot >= 8) return DEFAULT_BPM;
    char path[80];
    projectPath(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return DEFAULT_BPM;
    ProjectHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    fread(&hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (hdr.magic != PROJECT_MAGIC || hdr.version < 1 || hdr.version > PROJECT_VERSION) return DEFAULT_BPM;
    return hdr.bpm;
}

void Storage::loadProjectName(uint8_t slot, char* buf, uint8_t len) {
    buf[0] = '\0';
    if (!_ready || slot >= 8) return;
    char path[80];
    projectPath(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return;
    ProjectHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    fread(&hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (hdr.magic != PROJECT_MAGIC || hdr.version < 1 || hdr.version > PROJECT_VERSION) return;
    strncpy(buf, hdr.name, len - 1);
    buf[len - 1] = '\0';
}

bool Storage::saveProject(const Project& project, uint8_t slot) {
    if (!_ready || slot >= 8) return false;

    char dir[64];
    projectDir(slot, dir, sizeof(dir));
    ensureDir(dir);

    // Save each occupied sound as WAV
    for (int i = 0; i < NUM_SOUNDS; i++) {
        if (project.sounds[i].occupied) {
            char path[96];
            snprintf(path, sizeof(path), "%s/s%d.wav", dir, i);
            if (!Storage::saveWav(project.sounds[i], path)) return false;
        }
    }

    // Save project metadata
    char datPath[80];
    projectPath(slot, datPath, sizeof(datPath));
    FILE* f = fopen(datPath, "wb");
    if (!f) return false;

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

    bool written = fwrite(&hdr, sizeof(hdr), 1, f) == 1 &&
                   ferror(f) == 0 && fflush(f) == 0;
    if (fclose(f) != 0) written = false;
    return written;
}

bool Storage::loadProject(Project& project, uint8_t slot) {
    if (!_ready || slot >= 8) return false;

    char datPath[80];
    projectPath(slot, datPath, sizeof(datPath));
    FILE* f = fopen(datPath, "rb");
    if (!f) return false;

    ProjectHeader hdr;
    memset(&hdr, 0, sizeof(hdr));

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    static const size_t fxFieldsSize = sizeof(hdr.fxValues) + sizeof(hdr.fxEnabled);
    static const size_t fxFieldsOffset = offsetof(ProjectHeader, fxValues);

    if (fileSize <= sizeof(hdr) - fxFieldsSize) {
        uint8_t* p = (uint8_t*)&hdr;
        fread(p, 1, fxFieldsOffset, f);
        size_t remaining = fileSize - fxFieldsOffset;
        fread(p + fxFieldsOffset + fxFieldsSize, 1, remaining, f);
    } else {
        fread(&hdr, sizeof(hdr), 1, f);
    }
    fclose(f);

    if (hdr.magic != PROJECT_MAGIC || hdr.version < 1 || hdr.version > PROJECT_VERSION) {
        return false;
    }

    project.bpm = hdr.bpm;
    project.themeIndex = hdr.themeIndex;
    project.bitDepth = (BitDepth)hdr.bitDepth;
    strncpy(project.name, hdr.name, 8);
    project.name[8] = '\0';
    memcpy(project.patterns, hdr.patterns, sizeof(project.patterns));
    memcpy(project.song, hdr.song, sizeof(project.song));

    char dir[64];
    projectDir(slot, dir, sizeof(dir));

    for (int i = 0; i < NUM_SOUNDS; i++) {
        SoundSlotOps::free(project.sounds[i]);
        if (hdr.soundOccupied[i]) {
            char path[96];
            snprintf(path, sizeof(path), "%s/s%d.wav", dir, i);
            FILE* wf = fopen(path, "rb");
            if (wf) {
                fclose(wf);
                BitDepth slotDepth = hdr.version >= 4
                    ? (BitDepth)hdr.soundBitDepth[i] : BIT_DEPTH_16;
                Storage::loadWav(project.sounds[i], path, slotDepth);
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

    return true;
}

bool Storage::renderSongToWav(const Project& project, const char* path) {
    char fullPath[128];
    if (strncmp(path, "/beepbotdx/", 11) == 0) {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", BASE_DIR, path + 11);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s", path);
    }

    uint32_t samplesPerStep = SAMPLE_RATE * 60 / project.bpm / 4;

    uint8_t songLength = 0;
    for (uint8_t i = 0; i < NUM_SONG_POSITIONS; i++) {
        if (project.song[i] < NUM_PATTERNS) songLength = i + 1;
    }
    if (songLength == 0) songLength = 1;

    uint32_t totalSteps = songLength * NUM_STEPS;
    uint32_t totalSamples = totalSteps * samplesPerStep;

    FILE* f = fopen(fullPath, "wb");
    if (!f) return false;

    uint32_t dataSize = totalSamples * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;

    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16; fwrite(&fmtSize, 4, 1, f);
    uint16_t audioFmt = 1; fwrite(&audioFmt, 2, 1, f);
    uint16_t channels = 1; fwrite(&channels, 2, 1, f);
    uint32_t sr = SAMPLE_RATE; fwrite(&sr, 4, 1, f);
    uint32_t byteRate = sr * 2; fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = 2; fwrite(&blockAlign, 2, 1, f);
    uint16_t bps = 16; fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);

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

    const uint32_t CHUNK = 512;
    int16_t chunk[CHUNK];

    uint32_t samplePos = 0;
    for (uint32_t step = 0; step < totalSteps; step++) {
        uint8_t songPos = step / NUM_STEPS;
        uint8_t patStep = step % NUM_STEPS;
        uint8_t patIdx = project.song[songPos];
        uint8_t triggers = (patIdx < NUM_PATTERNS) ? project.patterns[patIdx].steps[patStep] : 0;

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

            fwrite(chunk, 2, toRender, f);
            samplePos += toRender;
        }
    }

    fclose(f);
    return true;
}
