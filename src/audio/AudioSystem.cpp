#include "audio/AudioSystem.h"

#include "debug/Log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <miniaudio.h>

struct AudioSystem::Impl {
    ma_device device{};
    bool initialized = false;
    std::atomic<float> rainTarget{0.0f};
    std::atomic<float> thunderPan{0.0f};
    std::atomic<float> thunderVolume{0.0f};
    std::atomic<unsigned> thunderTriggers{0};
    std::atomic<float> explosionPan{0.0f};
    std::atomic<float> explosionVolume{0.0f};
    std::atomic<unsigned> explosionTriggers{0};
    float rainVolume = 0.0f;
    float rainLowPass = 0.0f;
    float thunderEnvelope = 0.0f;
    float thunderFilter = 0.0f;
    float thunderPhase = 0.0f;
    float explosionEnvelope = 0.0f;
    float explosionFilter = 0.0f;
    uint32_t noiseState = 0x91e10da5u;

    float noise() {
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float>((noiseState >> 8) & 0xffffu) / 32767.5f - 1.0f;
    }

    static void callback(ma_device* device, void* output,
                         const void*, ma_uint32 frameCount) {
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* samples = static_cast<float*>(output);
        if (self->thunderTriggers.exchange(0) > 0)
            self->thunderEnvelope = self->thunderVolume.load();
        if (self->explosionTriggers.exchange(0) > 0)
            self->explosionEnvelope = self->explosionVolume.load();
        const float pan = std::clamp(self->thunderPan.load(), -1.0f, 1.0f);
        const float leftPan = std::sqrt(0.5f * (1.0f - pan));
        const float rightPan = std::sqrt(0.5f * (1.0f + pan));
        const float explosionPan = std::clamp(self->explosionPan.load(), -1.0f, 1.0f);
        const float explosionLeft = std::sqrt(0.5f * (1.0f - explosionPan));
        const float explosionRight = std::sqrt(0.5f * (1.0f + explosionPan));
        for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
            self->rainVolume +=
                (self->rainTarget.load() - self->rainVolume) * 0.0008f;
            const float white = self->noise();
            self->rainLowPass += (white - self->rainLowPass) * 0.055f;
            const float rain = ((white - self->rainLowPass) * 0.13f +
                                self->rainLowPass * 0.025f) * self->rainVolume;

            const float thunderNoise = self->noise();
            self->thunderFilter +=
                (thunderNoise - self->thunderFilter) * 0.012f;
            self->thunderPhase += 6.283185307f * 48.0f / 48000.0f;
            if (self->thunderPhase > 6.283185307f)
                self->thunderPhase -= 6.283185307f;
            const float thunder = (self->thunderFilter * 0.72f +
                std::sin(self->thunderPhase) * 0.28f) * self->thunderEnvelope;
            self->thunderEnvelope *= 0.99986f;
            if (self->thunderEnvelope < 0.0001f) self->thunderEnvelope = 0.0f;
            const float explosionNoise = self->noise();
            self->explosionFilter += (explosionNoise - self->explosionFilter) * .045f;
            const float explosion = (explosionNoise * .42f + self->explosionFilter * .78f) *
                                    self->explosionEnvelope;
            self->explosionEnvelope *= .9989f;
            if (self->explosionEnvelope < .0001f) self->explosionEnvelope = 0.0f;
            samples[frame * 2] = std::clamp(
                rain + thunder * leftPan + explosion * explosionLeft, -1.0f, 1.0f);
            samples[frame * 2 + 1] =
                std::clamp(rain + thunder * rightPan + explosion * explosionRight,
                           -1.0f, 1.0f);
        }
    }
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}

AudioSystem::~AudioSystem() {
    if (m_impl->initialized) ma_device_uninit(&m_impl->device);
}

bool AudioSystem::initialize() {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 48000;
    config.dataCallback = Impl::callback;
    config.pUserData = m_impl.get();
    const ma_result result = ma_device_init(nullptr, &config, &m_impl->device);
    if (result != MA_SUCCESS || ma_device_start(&m_impl->device) != MA_SUCCESS) {
        if (result == MA_SUCCESS) ma_device_uninit(&m_impl->device);
        LOG_WARN("Audio device unavailable; weather audio disabled");
        return false;
    }
    m_impl->initialized = true;
    LOG_INFO("Weather audio initialized at 48 kHz stereo");
    return true;
}

void AudioSystem::setRainVolume(float volume) {
    m_impl->rainTarget = std::clamp(volume, 0.0f, 1.0f);
}

void AudioSystem::playThunder(float pan, float volume) {
    if (!m_impl->initialized) return;
    m_impl->thunderPan = std::clamp(pan, -1.0f, 1.0f);
    m_impl->thunderVolume = std::clamp(volume, 0.0f, 1.0f);
    ++m_impl->thunderTriggers;
}

void AudioSystem::playExplosion(float pan, float volume) {
    if (!m_impl->initialized) return;
    m_impl->explosionPan = std::clamp(pan, -1.0f, 1.0f);
    m_impl->explosionVolume = std::clamp(volume, 0.0f, 1.0f);
    ++m_impl->explosionTriggers;
}

bool AudioSystem::available() const { return m_impl->initialized; }
