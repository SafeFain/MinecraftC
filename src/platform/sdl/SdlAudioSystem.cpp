#include "audio/AudioSystem.h"

#include "core/AssetStore.h"
#include "debug/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

struct AudioSystem::Impl {
    static constexpr int CHANNELS = 2;
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr size_t CALLBACK_FRAMES = 4096;

    SDL_AudioStream* stream = nullptr;
    bool subsystemInitialized = false;
    bool initialized = false;
    bool paused = false;
    struct MusicTrack {
        std::vector<int16_t> samples;
        size_t outputCursor = 0;

        bool available() const { return samples.size() >= 2; }
    };
    MusicTrack menuMusic;
    MusicTrack gameplayMusic;
    std::atomic<AudioMusicMode> musicTarget{AudioMusicMode::Menu};
    AudioMusicMode observedMusicMode = AudioMusicMode::Menu;
    float menuMusicGain = 0.0f;
    float gameplayMusicGain = 0.0f;
    std::atomic<float> rainTarget{0.0f};
    std::atomic<bool> rainReset{false};
    std::atomic<float> thunderPan{0.0f};
    std::atomic<float> thunderVolume{0.0f};
    std::atomic<unsigned> thunderTriggers{0};
    std::atomic<float> explosionPan{0.0f};
    std::atomic<float> explosionVolume{0.0f};
    std::atomic<unsigned> explosionTriggers{0};
    float rainVolume = 0.0f;
    float rainLowLeft = 0.0f;
    float rainLowRight = 0.0f;
    float rainMidLeft = 0.0f;
    float rainMidRight = 0.0f;
    float rainHighLeft = 0.0f;
    float rainHighRight = 0.0f;
    float rainGust = 0.0f;
    float rainDropEnvelope = 0.0f;
    float rainDropPhase = 0.0f;
    float rainDropFrequency = 1200.0f;
    float rainDropPan = 0.0f;
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

    static MusicTrack loadMusic(const AssetStore& assets, const char* path) {
        const std::vector<uint8_t> encoded = assets.readBinary(path);
        SDL_IOStream* input = SDL_IOFromConstMem(encoded.data(), encoded.size());
        if (!input) throw std::runtime_error(SDL_GetError());
        SDL_AudioSpec sourceSpec{};
        Uint8* source = nullptr;
        Uint32 sourceBytes = 0;
        if (!SDL_LoadWAV_IO(input, true, &sourceSpec, &source, &sourceBytes))
            throw std::runtime_error(SDL_GetError());
        const SDL_AudioSpec targetSpec{SDL_AUDIO_S16LE, CHANNELS, SAMPLE_RATE / 2};
        Uint8* converted = nullptr;
        int convertedBytes = 0;
        const bool convertedOk = SDL_ConvertAudioSamples(
            &sourceSpec, source, static_cast<int>(sourceBytes),
            &targetSpec, &converted, &convertedBytes);
        SDL_free(source);
        if (!convertedOk) throw std::runtime_error(SDL_GetError());
        MusicTrack track;
        const auto* integers = reinterpret_cast<const int16_t*>(converted);
        track.samples.assign(integers, integers + convertedBytes / sizeof(int16_t));
        SDL_free(converted);
        if (track.samples.size() < 2 || track.samples.size() % CHANNELS != 0)
            throw std::runtime_error("decoded music has an invalid frame count");
        return track;
    }

    static void mixTrack(MusicTrack& track, float gain, float& left, float& right) {
        if (!track.available() || gain <= 0.00001f) return;
        const size_t sourceFrames = track.samples.size() / CHANNELS;
        const size_t sourceFrame = (track.outputCursor / 2) % sourceFrames;
        const size_t nextFrame = (sourceFrame + 1) % sourceFrames;
        const float blend = (track.outputCursor & 1u) ? 0.5f : 0.0f;
        const float scale = gain / 32768.0f;
        left += (track.samples[sourceFrame * 2] * (1.0f - blend) +
                 track.samples[nextFrame * 2] * blend) * scale;
        right += (track.samples[sourceFrame * 2 + 1] * (1.0f - blend) +
                  track.samples[nextFrame * 2 + 1] * blend) * scale;
        track.outputCursor = (track.outputCursor + 1) % (sourceFrames * 2);
    }

    void render(float* samples, size_t frameCount) {
        if (rainReset.exchange(false)) {
            rainVolume = 0.0f;
            rainLowLeft = rainLowRight = 0.0f;
            rainMidLeft = rainMidRight = 0.0f;
            rainHighLeft = rainHighRight = 0.0f;
            rainGust = rainDropEnvelope = 0.0f;
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
            const AudioMusicMode requestedMusic = musicTarget.load();
            if (requestedMusic != observedMusicMode) {
                observedMusicMode = requestedMusic;
                (requestedMusic == AudioMusicMode::Menu
                    ? menuMusic : gameplayMusic).outputCursor = 0;
            }
            const float menuTarget = requestedMusic == AudioMusicMode::Menu ? 1.0f : 0.0f;
            const float gameplayTarget = requestedMusic == AudioMusicMode::Gameplay
                ? 1.0f : 0.0f;
            menuMusicGain += (menuTarget - menuMusicGain) * 0.000035f;
            gameplayMusicGain += (gameplayTarget - gameplayMusicGain) * 0.000035f;
            float left = 0.0f;
            float right = 0.0f;
            mixTrack(menuMusic, menuMusicGain * 0.72f, left, right);
            mixTrack(gameplayMusic, gameplayMusicGain * 0.68f, left, right);

            rainVolume += (rainTarget.load() - rainVolume) * 0.0008f;
            const float whiteLeft = noise();
            const float whiteRight = noise();
            rainLowLeft += (whiteLeft - rainLowLeft) * 0.012f;
            rainLowRight += (whiteRight - rainLowRight) * 0.012f;
            rainMidLeft += (whiteLeft - rainMidLeft) * 0.070f;
            rainMidRight += (whiteRight - rainMidRight) * 0.070f;
            rainHighLeft += (whiteLeft - rainHighLeft) * 0.16f;
            rainHighRight += (whiteRight - rainHighRight) * 0.16f;
            rainGust += (noise() - rainGust) * 0.000025f;
            const float gust = 0.82f + rainGust * 0.16f;
            float rainLeft = ((rainMidLeft - rainLowLeft) * 0.095f +
                              (rainHighLeft - rainMidLeft) * 0.028f +
                              rainLowLeft * 0.020f) * gust;
            float rainRight = ((rainMidRight - rainLowRight) * 0.095f +
                               (rainHighRight - rainMidRight) * 0.028f +
                               rainLowRight * 0.020f) * gust;
            if (rainDropEnvelope < 0.0001f && noise() > 0.99945f) {
                rainDropEnvelope = 0.052f;
                rainDropFrequency = 850.0f + (noise() + 1.0f) * 520.0f;
                rainDropPan = noise() * 0.72f;
            }
            rainDropPhase += 6.283185307f * rainDropFrequency /
                             static_cast<float>(SAMPLE_RATE);
            if (rainDropPhase > 6.283185307f) rainDropPhase -= 6.283185307f;
            rainDropFrequency *= 0.99993f;
            const float drop = std::sin(rainDropPhase) * rainDropEnvelope;
            rainDropEnvelope *= 0.9948f;
            rainLeft += drop * std::sqrt(0.5f * (1.0f - rainDropPan));
            rainRight += drop * std::sqrt(0.5f * (1.0f + rainDropPan));
            rainLeft *= rainVolume;
            rainRight *= rainVolume;

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
                left + rainLeft + thunder * leftPan + explosion * explosionLeft,
                -1.0f, 1.0f);
            samples[frame * 2 + 1] =
                std::clamp(right + rainRight + thunder * rightPan +
                               explosion * explosionRight,
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

bool AudioSystem::initialize(const AssetStore* assets) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOG_WARN("SDL audio unavailable; game audio disabled: " << SDL_GetError());
        return false;
    }
    m_impl->subsystemInitialized = true;
    if (assets) {
        try {
            m_impl->menuMusic = Impl::loadMusic(*assets, "audio/menu_whimsy.wav");
            m_impl->gameplayMusic = Impl::loadMusic(*assets, "audio/gameplay_calm.wav");
        } catch (const std::exception& error) {
            LOG_WARN("Music assets unavailable; continuing without music: " <<
                     error.what());
            m_impl->menuMusic = {};
            m_impl->gameplayMusic = {};
        }
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, Impl::CHANNELS, Impl::SAMPLE_RATE};
    m_impl->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::callback, m_impl.get());
    const bool streamReady = m_impl->stream &&
        (m_impl->paused || SDL_ResumeAudioStreamDevice(m_impl->stream));
    if (!streamReady) {
        LOG_WARN("SDL audio device unavailable; game audio disabled: " <<
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
    LOG_INFO("SDL game audio initialized at 48 kHz stereo (menu music=" <<
             (m_impl->menuMusic.available() ? "yes" : "no") <<
             ", gameplay music=" <<
             (m_impl->gameplayMusic.available() ? "yes" : "no") << ")");
    return true;
}

void AudioSystem::setPaused(bool paused) {
    if (m_impl->paused == paused) return;
    if (!m_impl->initialized || !m_impl->stream) {
        m_impl->paused = paused;
        return;
    }
    const bool changed = paused
        ? SDL_PauseAudioStreamDevice(m_impl->stream)
        : SDL_ResumeAudioStreamDevice(m_impl->stream);
    if (!changed) {
        LOG_WARN("Could not " << (paused ? "pause" : "resume")
                 << " SDL audio: " << SDL_GetError());
        return;
    }
    m_impl->paused = paused;
}

bool AudioSystem::paused() const {
    if (m_impl->initialized && m_impl->stream)
        return SDL_AudioStreamDevicePaused(m_impl->stream);
    return m_impl->paused;
}

void AudioSystem::setMusicMode(AudioMusicMode mode) {
    m_impl->musicTarget = mode;
}

AudioMusicMode AudioSystem::musicMode() const {
    return m_impl->musicTarget.load();
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
