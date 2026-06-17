#pragma once

#include <array>
#include <cmath>
#include <cstdint>

class Noise {
public:
    Noise();
    explicit Noise(uint64_t seed);  // Seeded: Fisher-Yates shuffle on permutation

    // 2D Perlin noise, returns [-1, 1]
    float noise2D(float x, float y) const;

    // 3D Perlin noise, returns [-1, 1]
    float noise3D(float x, float y, float z) const;

    // Fractal Brownian Motion (octave noise)
    float octave2D(float x, float y, int octaves = 4,
                   float persistence = 0.5f, float lacunarity = 2.0f) const;

    float octave3D(float x, float y, float z, int octaves = 3,
                   float persistence = 0.5f, float lacunarity = 2.0f) const;

    // Domain warping: offsets sample coords by a small noise field,
    // then samples the main noise at the warped position.
    // warpStrength = max displacement in world units
    // warpScale    = frequency of the warp noise
    float warped2D(float x, float y, int octaves,
                   float warpStrength, float warpScale) const;

private:
    std::array<int, 512> m_perm{};

    void seedPermutation(uint64_t seed);

    static float fade(float t);
    static float lerp(float a, float b, float t);
    static float grad2D(int hash, float x, float y);
    static float grad3D(int hash, float x, float y, float z);
};
