#include <M5Cardputer.h>
#include "audio.h"
#include "config.h"
#include "core/slot_fx.h"
#include "core/fx_dsp.h"

static const uint32_t REC_CHUNK = 1024;
static int16_t _recChunk[REC_CHUNK];

static void* _recBuffer = nullptr;
static BitDepth _recBitDepth = BIT_DEPTH_16;
static uint32_t _recMaxLength = 0;
static uint32_t _recOffset = 0;
static bool _recording = false;
static bool _primed = false;

static uint8_t _nextChannel = 0;
static uint8_t _volume = SPEAKER_VOLUME;

static constexpr uint32_t FX_CHUNK_SAMPLES = 256;
static constexpr uint8_t FX_BUFFER_COUNT = 3;

struct FxVoice {
    const SoundSlot* source;
    float position;
    float pitchMultiplier;
    SlotFx fx;
    FxFilterState filterState;
    int16_t buffers[FX_BUFFER_COUNT][FX_CHUNK_SAMPLES];
    uint8_t nextBuffer;
    bool active;
};

static FxVoice _fxVoices[NUM_VOICES] = {};

static bool queueFxChunk(uint8_t channel, bool stopCurrent = false) {
    FxVoice& voice = _fxVoices[channel];
    if (!voice.active) return false;

    int16_t* output = voice.buffers[voice.nextBuffer];
    uint32_t outputLength = 0;
    while (outputLength < FX_CHUNK_SAMPLES &&
           (uint32_t)voice.position < voice.source->length) {
        uint32_t index = (uint32_t)voice.position;
        float fraction = voice.position - index;
        int16_t first = SoundSlotOps::getSample(*voice.source, index);
        int16_t second =
            (index + 1 < voice.source->length)
                ? SoundSlotOps::getSample(*voice.source, index + 1) : first;
        int16_t interpolated =
            (int16_t)(first + fraction * (second - first));
        output[outputLength++] = FxDsp::processSample(
            interpolated, voice.fx.value, voice.fx.enabled,
            voice.filterState, (float)SAMPLE_RATE);
        voice.position += voice.pitchMultiplier;
    }

    if (outputLength == 0) {
        voice.active = false;
        return false;
    }

    M5Cardputer.Speaker.playRaw(
        output, outputLength, SAMPLE_RATE, false, 1, channel, stopCurrent);
    voice.nextBuffer = (voice.nextBuffer + 1) % FX_BUFFER_COUNT;
    if ((uint32_t)voice.position >= voice.source->length) voice.active = false;
    return true;
}

void Audio::init() {
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(_volume);
}

void Audio::update() {
    for (uint8_t channel = 0; channel < NUM_VOICES; channel++) {
        FxVoice& voice = _fxVoices[channel];
        while (voice.active && M5Cardputer.Speaker.isPlaying(channel) < 2) {
            if (!queueFxChunk(channel)) break;
        }
    }
}

void Audio::recordStart(void* buffer, uint32_t maxLength, BitDepth bitDepth) {
    M5Cardputer.Speaker.end();
    M5Cardputer.Mic.end();
    delay(100);

    _recBuffer = buffer;
    _recBitDepth = bitDepth;
    _recMaxLength = maxLength;
    _recOffset = 0;
    _recording = true;
    _primed = false;

    memset(_recBuffer, 0,
           _recMaxLength * SoundSlotOps::bytesPerSample(_recBitDepth));

    auto micCfg = M5Cardputer.Mic.config();
    micCfg.sample_rate = SAMPLE_RATE;
    micCfg.magnification = 64;
    micCfg.noise_filter_level = 0;
    micCfg.over_sampling = 2;
    micCfg.dma_buf_len = 256;
    micCfg.dma_buf_count = 8;
    M5Cardputer.Mic.config(micCfg);
    M5Cardputer.Mic.begin();
    delay(200);

    // Prime the double-buffer — first call queues but data isn't valid yet
    M5Cardputer.Mic.record(_recChunk, REC_CHUNK, SAMPLE_RATE);
}

void Audio::recordUpdate() {
    if (!_recording) return;
    if (_recOffset >= _recMaxLength) return;

    // record() blocks until previous chunk is done, then queues this buffer
    // After returning, _recChunk contains data from the PREVIOUS call
    if (M5Cardputer.Mic.record(_recChunk, REC_CHUNK, SAMPLE_RATE)) {
        if (!_primed) {
            // Skip first return — data isn't valid until second call
            _primed = true;
            return;
        }

        uint32_t remaining = _recMaxLength - _recOffset;
        uint32_t toCopy = (REC_CHUNK < remaining) ? REC_CHUNK : remaining;
        if (_recBitDepth == BIT_DEPTH_8) {
            int8_t* destination = (int8_t*)_recBuffer;
            for (uint32_t i = 0; i < toCopy; i++)
                destination[_recOffset + i] = (int8_t)(_recChunk[i] >> 8);
        } else {
            memcpy((int16_t*)_recBuffer + _recOffset,
                   _recChunk, toCopy * sizeof(int16_t));
        }
        _recOffset += toCopy;
    }
}

void Audio::recordStop() {
    _recording = false;
    delay(100);
    M5Cardputer.Mic.end();
    delay(100);

    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(SPEAKER_VOLUME);
    delay(100);
}

bool Audio::isRecording() {
    return _recording;
}

uint32_t Audio::getRecordedLength() {
    return _recOffset;
}

void Audio::triggerSound(const SoundSlot& slot, uint8_t volume, const SlotFx* fx) {
    if (!slot.samples || slot.length == 0) return;
    M5Cardputer.Speaker.setChannelVolume(_nextChannel, volume);

    if (fx && FxDsp::hasActiveFx(*fx)) {
        FxVoice& voice = _fxVoices[_nextChannel];
        voice.source = &slot;
        voice.position = 0.0f;
        voice.pitchMultiplier = fx->enabled[FX_PITCH]
            ? FxDsp::pitchRate(fx->value[FX_PITCH]) : 1.0f;
        voice.fx = *fx;
        FxDsp::initFilterState(voice.filterState);
        voice.active = true;
        // A third persistent buffer ensures the first replacement chunk
        // cannot overwrite either buffer still owned by the speaker task.
        queueFxChunk(_nextChannel, true);
    } else {
        _fxVoices[_nextChannel].active = false;
        if (slot.bitDepth == BIT_DEPTH_8) {
            M5Cardputer.Speaker.playRaw(
                (const int8_t*)slot.samples, slot.length, slot.sampleRate,
                false, 1, _nextChannel, true);
        } else {
            M5Cardputer.Speaker.playRaw(
                slot.samples, slot.length, slot.sampleRate,
                false, 1, _nextChannel, true);
        }
    }

    _nextChannel = (_nextChannel + 1) % NUM_VOICES;
}

void Audio::stopAll() {
    for (uint8_t channel = 0; channel < NUM_VOICES; channel++) {
        _fxVoices[channel].active = false;
    }
    M5Cardputer.Speaker.stop();
}

void Audio::setVolume(uint8_t vol) {
    _volume = vol;
    M5Cardputer.Speaker.setVolume(_volume);
}

uint8_t Audio::getVolume() {
    return _volume;
}
