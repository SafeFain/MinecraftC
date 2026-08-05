#include "audio/AudioSystem.h"

#include "debug/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

struct AudioSystem::Impl {
    static constexpr int CHANNELS = 2;
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr size_t CALLBACK_FRAMES = 4096;

    SDL_AudioStream* stream = nullptr;
    bool subsystemInitialized = false;
    bool initialized = false;
    std::atomic<float> rainTarget{0.0f};
    std::atomic<bool> rainReset{false};
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

    void render(float* samples, size_t frameCount) {
        if (rainReset.exchange(false)) {
            rainVolume = 0.0f;
            rainLowPass = 0.0f;
        }
        if (thunderTriggers.exchange(0) > 0)
            thunderEnvelope = thunderVolume.load();
        if (explosionTriggers.exchange(0) > 0)
            explosionEnvelope = explosionVolume.load();
        const float pan = std::clamp(thunderPan.load(), -1.0f, 1.0f);
        const float leftPan = std::sqrt(0.5f * (1.0f - pan));
        const float rightPan = std::sqrt(0.5f * (1.0f + pan));
        const float explosionPanValue = std::clamp(explosionPan.load(), -1.0f, 1.0f);
        const float explosionLeft = std::sqrt(0.5f * (1.0f - explosionPanValue));
        const float explosionRight = std::sqrt(0.5f * (1.0f + explosionPanValue));
        for (size_t frame = 0; frame < frameCount; ++frame) {
            rainVolume += (rainTarget.load() - rainVolume) * 0.0008f;
            const float white = noise();
            rainLowPass += (white - rainLowPass) * 0.055f;
            const float rain = ((white - rainLowPass) * 0.13f +
                                rainLowPass * 0.025f) * rainVolume;

            const float thunderNoise = noise();
            thunderFilter += (thunderNoise - thunderFilter) * 0.012f;
            thunderPhase += 6.283185307f * 48.0f /
                            static_cast<float>(SAMPLE_RATE);
            if (thunderPhase > 6.283185307f)
                thunderPhase -= 6.283185307f;
            const float thunder = (thunderFilter * 0.72f +
                std::sin(thunderPhase) * 0.28f) * thunderEnvelope;
            thunderEnvelope *= 0.99986f;
            if (thunderEnvelope < 0.0001f) thunderEnvelope = 0.0f;
            const float explosionNoise = noise();
            explosionFilter += (explosionNoise - explosionFilter) * .045f;
            const float explosion = (explosionNoise * .42f + explosionFilter * .78f) *
                                    explosionEnvelope;
            explosionEnvelope *= .9989f;
            if (explosionEnvelope < .0001f) explosionEnvelope = 0.0f;
            samples[frame * 2] = std::clamp(
                rain + thunder * leftPan + explosion * explosionLeft, -1.0f, 1.0f);
            samples[frame * 2 + 1] =
                std::clamp(rain + thunder * rightPan + explosion * explosionRight,
                           -1.0f, 1.0f);
        }
    }

    static void SDLCALL callback(void* userdata, SDL_AudioStream* stream,
                                 int additionalAmount, int) {
        auto* self = static_cast<Impl*>(userdata);
        std::array<float, CALLBACK_FRAMES * CHANNELS> samples{};
        int framesRemaining = std::max(0, additionalAmount) /
                              static_cast<int>(sizeof(float) * CHANNELS);
        while (framesRemaining > 0) {
            const size_t frames = std::min(
                static_cast<size_t>(framesRemaining), CALLBACK_FRAMES);
            self->render(samples.data(), frames);
            const int bytes = static_cast<int>(frames * CHANNELS * sizeof(float));
            if (!SDL_PutAudioStreamData(stream, samples.data(), bytes)) return;
            framesRemaining -= static_cast<int>(frames);
        }
    }
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}

AudioSystem::~AudioSystem() {
    if (m_impl->stream) SDL_DestroyAudioStream(m_impl->stream);
    if (m_impl->subsystemInitialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool AudioSystem::initialize() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOG_WARN("SDL audio unavailable; weather audio disabled: " << SDL_GetError());
        return false;
    }
    m_impl->subsystemInitialized = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, Impl::CHANNELS, Impl::SAMPLE_RATE};
    m_impl->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::callback, m_impl.get());
    if (!m_impl->stream || !SDL_ResumeAudioStreamDevice(m_impl->stream)) {
        LOG_WARN("SDL audio device unavailable; weather audio disabled: " <<
                 SDL_GetError());
        if (m_impl->stream) {
            SDL_DestroyAudioStream(m_impl->stream);
            m_impl->stream = nullptr;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_impl->subsystemInitialized = false;
        return false;
    }
    m_impl->initialized = true;
    LOG_INFO("SDL weather audio initialized at 48 kHz stereo");
    return true;
}

void AudioSystem::setRainVolume(float volume) {
    m_impl->rainTarget = std::clamp(volume, 0.0f, 1.0f);
}

void AudioSystem::stopRain() {
    m_impl->rainTarget = 0.0f;
    m_impl->rainReset = true;
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
