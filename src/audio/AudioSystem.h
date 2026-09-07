#pragma once

#include <memory>

class AssetStore;

enum class AudioMusicMode {
    Menu,
    Overworld,
    Heaven
};

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
    void setMusicMode(AudioMusicMode mode);
    AudioMusicMode musicMode() const;
    void setVolumes(float master, float music, float weather, float soundEffects);
    float masterVolume() const;
    float musicVolume() const;
    float weatherVolume() const;
    float soundEffectsVolume() const;
    void setRainVolume(float volume);
    void stopRain();
    void playThunder(float pan, float volume);
    void playExplosion(float pan, float volume);
    void playCombat(CombatSound sound);
    bool available() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
