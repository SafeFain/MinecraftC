#pragma once

#include <memory>

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool initialize();
    void setRainVolume(float volume);
    void stopRain();
    void playThunder(float pan, float volume);
    void playExplosion(float pan, float volume);
    bool available() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
