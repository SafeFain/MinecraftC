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
    std::array<MusicTrack, 2> menuMusic;
    std::array<MusicTrack, 4> overworldMusic;
    std::array<MusicTrack, 2> heavenMusic;
    std::atomic<AudioMusicMode> musicTarget{AudioMusicMode::Menu};
    AudioMusicMode observedMusicMode = AudioMusicMode::Menu;
    uint64_t menuRandomState = 0x8a5cd789635d2dffULL;
    uint64_t overworldRandomState = 0xd1b54a32d192ed03ULL;
    uint64_t heavenRandomState = 0x94d049bb133111ebULL;
    size_t menuTrackIndex = 0;
    size_t overworldTrackIndex = 0;
    size_t heavenTrackIndex = 0;
    float menuMusicGain = 0.0f;
    float overworldMusicGain = 0.0f;
    float heavenMusicGain = 0.0f;
    std::atomic<float> rainTarget{0.0f};
    std::atomic<bool> rainReset{false};
    std::atomic<float> thunderPan{0.0f};
    std::atomic<float> thunderVolume{0.0f};
    std::atomic<unsigned> thunderTriggers{0};
    std::atomic<float> explosionPan{0.0f};
    std::atomic<float> explosionVolume{0.0f};
    std::atomic<unsigned> explosionTriggers{0};
    std::atomic<int> combatTrigger{-1};
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
    float combatEnvelope = 0.0f;
    float combatPhase = 0.0f;
    float combatFrequency = 220.0f;
    float combatDecay = 0.996f;
    float combatNoiseMix = 0.0f;
    uint32_t noiseState = 0x91e10da5u;

    float noise() {
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float>((noiseState >> 8) & 0xffffu) / 32767.5f - 1.0f;
    }

    static size_t randomTrack(uint64_t& state, size_t trackCount) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return static_cast<size_t>(
            (state * 0x2545f4914f6cdd1dULL) % trackCount);
    }

    template<size_t N>
    static bool playlistAvailable(const std::array<MusicTrack, N>& playlist) {
        return std::all_of(playlist.begin(), playlist.end(),
                           [](const MusicTrack& track) {
                               return track.available();
                           });
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

    static bool mixTrack(
        MusicTrack& track, float gain, float& left, float& right) {
        if (!track.available() || gain <= 0.00001f) return false;
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
        return track.outputCursor == 0;
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
        const int requestedCombat = combatTrigger.exchange(-1);
        if (requestedCombat >= 0) {
            static constexpr float frequencies[] = {
                145.0f, 205.0f, 135.0f, 520.0f, 310.0f, 760.0f, 95.0f};
            static constexpr float envelopes[] = {
                .16f, .20f, .27f, .30f, .25f, .30f, .38f};
            static constexpr float decays[] = {
                .9960f, .9964f, .9970f, .9974f, .9968f, .9972f, .9980f};
            static constexpr float noiseMixes[] = {
                .72f, .35f, .58f, .12f, .42f, .18f, .76f};
            const size_t index = static_cast<size_t>(std::clamp(requestedCombat, 0, 6));
            combatFrequency = frequencies[index];
            combatEnvelope = envelopes[index];
            combatDecay = decays[index];
            combatNoiseMix = noiseMixes[index];
            combatPhase = 0.0f;
        }
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
                // A dimension-exclusive track must never bleed into another
                // mode. Reset all gains, then fade only the requested track in.
                menuMusicGain = 0.0f;
                overworldMusicGain = 0.0f;
                heavenMusicGain = 0.0f;
                if (requestedMusic == AudioMusicMode::Menu) {
                    menuTrackIndex = randomTrack(menuRandomState, menuMusic.size());
                    for (MusicTrack& track : menuMusic) track.outputCursor = 0;
                } else if (requestedMusic == AudioMusicMode::Overworld) {
                    overworldTrackIndex = randomTrack(
                        overworldRandomState, overworldMusic.size());
                    for (MusicTrack& track : overworldMusic) track.outputCursor = 0;
                } else {
                    heavenTrackIndex = randomTrack(
                        heavenRandomState, heavenMusic.size());
                    for (MusicTrack& track : heavenMusic) track.outputCursor = 0;
                }
            }
            const float menuTarget = requestedMusic == AudioMusicMode::Menu ? 1.0f : 0.0f;
            const float overworldTarget = requestedMusic == AudioMusicMode::Overworld
                ? 1.0f : 0.0f;
            const float heavenTarget = requestedMusic == AudioMusicMode::Heaven
                ? 1.0f : 0.0f;
            menuMusicGain += (menuTarget - menuMusicGain) * 0.000035f;
            overworldMusicGain +=
                (overworldTarget - overworldMusicGain) * 0.000035f;
            heavenMusicGain += (heavenTarget - heavenMusicGain) * 0.000035f;
            float left = 0.0f;
            float right = 0.0f;
            if (requestedMusic == AudioMusicMode::Menu) {
                if (mixTrack(menuMusic[menuTrackIndex], menuMusicGain * 0.72f,
                             left, right))
                    menuTrackIndex = randomTrack(menuRandomState, menuMusic.size());
            } else if (requestedMusic == AudioMusicMode::Overworld) {
                if (mixTrack(overworldMusic[overworldTrackIndex],
                             overworldMusicGain * 0.68f, left, right))
                    overworldTrackIndex = randomTrack(
                        overworldRandomState, overworldMusic.size());
            } else {
                if (mixTrack(heavenMusic[heavenTrackIndex],
                             heavenMusicGain * 0.70f, left, right))
                    heavenTrackIndex = randomTrack(
                        heavenRandomState, heavenMusic.size());
            }

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
            combatPhase += 6.283185307f * combatFrequency /
                           static_cast<float>(SAMPLE_RATE);
            if (combatPhase > 6.283185307f) combatPhase -= 6.283185307f;
            combatFrequency *= 0.99994f;
            const float combat = (std::sin(combatPhase) * (1.0f - combatNoiseMix) +
                                  noise() * combatNoiseMix) * combatEnvelope;
            combatEnvelope *= combatDecay;
            if (combatEnvelope < .0001f) combatEnvelope = 0.0f;
            samples[frame * 2] = std::clamp(
                left + rainLeft + thunder * leftPan + explosion * explosionLeft +
                    combat * .707f,
                -1.0f, 1.0f);
            samples[frame * 2 + 1] =
                std::clamp(right + rainRight + thunder * rightPan +
                               explosion * explosionRight + combat * .707f,
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
            m_impl->menuMusic[0] =
                Impl::loadMusic(*assets, "audio/menu_whimsy.wav");
            m_impl->menuMusic[1] =
                Impl::loadMusic(*assets, "audio/menu_spark.wav");
            m_impl->overworldMusic[0] =
                Impl::loadMusic(*assets, "audio/gameplay_calm.wav");
            m_impl->overworldMusic[1] =
                Impl::loadMusic(*assets, "audio/overworld_solitude.wav");
            m_impl->overworldMusic[2] =
                Impl::loadMusic(*assets, "audio/overworld_horizon.wav");
            m_impl->overworldMusic[3] =
                Impl::loadMusic(*assets, "audio/overworld_nightfall.wav");
            m_impl->heavenMusic[0] =
                Impl::loadMusic(*assets, "audio/heaven_ether.wav");
            m_impl->heavenMusic[1] =
                Impl::loadMusic(*assets, "audio/heaven_sanctum.wav");
        } catch (const std::exception& error) {
            LOG_WARN("Music assets unavailable; continuing without music: " <<
                     error.what());
            m_impl->menuMusic = {};
            m_impl->overworldMusic = {};
            m_impl->heavenMusic = {};
        }
    }
    const uint64_t randomSeed = static_cast<uint64_t>(SDL_GetTicksNS()) ^
        (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m_impl.get())) << 1);
    m_impl->menuRandomState = randomSeed ^ 0x8a5cd789635d2dffULL;
    m_impl->overworldRandomState = randomSeed ^ 0xd1b54a32d192ed03ULL;
    m_impl->heavenRandomState = randomSeed ^ 0x94d049bb133111ebULL;
    if (m_impl->menuRandomState == 0) m_impl->menuRandomState = 1;
    if (m_impl->overworldRandomState == 0) m_impl->overworldRandomState = 1;
    if (m_impl->heavenRandomState == 0) m_impl->heavenRandomState = 1;
    m_impl->menuTrackIndex = Impl::randomTrack(
        m_impl->menuRandomState, m_impl->menuMusic.size());
    m_impl->overworldTrackIndex = Impl::randomTrack(
        m_impl->overworldRandomState, m_impl->overworldMusic.size());
    m_impl->heavenTrackIndex = Impl::randomTrack(
        m_impl->heavenRandomState, m_impl->heavenMusic.size());
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
             (Impl::playlistAvailable(m_impl->menuMusic) ? "2/2" : "unavailable") <<
             ", overworld music=" <<
             (Impl::playlistAvailable(m_impl->overworldMusic)
                  ? "4/4" : "unavailable") <<
             ", Heaven music=" <<
             (Impl::playlistAvailable(m_impl->heavenMusic)
                  ? "2/2" : "unavailable") << ")");
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

void AudioSystem::playCombat(CombatSound sound) {
    if (!m_impl->initialized) return;
    m_impl->combatTrigger = static_cast<int>(sound);
}

bool AudioSystem::available() const { return m_impl->initialized; }
