#pragma once

#include <memory>

class AssetStore;

enum class AudioMusicMode {
    Menu,
    Gameplay
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
    void setRainVolume(float volume);
    void stopRain();
    void playThunder(float pan, float volume);
    void playExplosion(float pan, float volume);
    bool available() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
