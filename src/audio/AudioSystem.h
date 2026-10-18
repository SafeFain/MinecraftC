#pragma once

#include <memory>
#include <cstdint>

class AssetStore;

enum class AudioMusicMode {
    Menu,
    Overworld
};

enum class FishingSound { Cast, Splash, Bite, Reel };

enum class CombatSound {
    Miss, Weak, Strong, Critical, Sweep, ShieldBlock, ShieldBreak
};

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool initialize(const AssetStore* assets = nullptr);
    void setPaused(bool paused);
    bool paused() const;
    void setMusicMode(AudioMusicMode mode,uint32_t dimension=0);
    AudioMusicMode musicMode() const;
    uint32_t musicDimension() const;
    void setVolumes(float master, float music, float weather, float soundEffects);
    float masterVolume() const;
    float musicVolume() const;
    float weatherVolume() const;
    float soundEffectsVolume() const;
    void setRainVolume(float volume);
    void stopRain();
    void playThunder(float pan, float volume);
    void playExplosion(float pan, float volume);
    void playFishing(FishingSound sound);
    void playCombat(CombatSound sound);
    void playBlockInteraction(bool metal, bool opening, bool button);
    bool available() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
