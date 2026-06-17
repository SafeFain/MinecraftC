#include "world/Noise.h"
#include <algorithm>
#include <random>

// ── Standard Perlin permutation table ─────────────────────────────────

static const std::array<int, 256> PERM = {
    151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
    140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
    247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
     57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
     74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
     60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
     65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
    200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
     52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
    207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
    119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
    129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
    218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
     81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
    184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
    222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,
};

Noise::Noise() {
    // Double the standard permutation table for easy indexing
    for (int i = 0; i < 256; ++i) {
        m_perm[i] = m_perm[i + 256] = PERM[i];
    }
}

Noise::Noise(uint64_t seed) {
    seedPermutation(seed);
}

void Noise::seedPermutation(uint64_t seed) {
    // Initialize identity permutation
    for (int i = 0; i < 256; ++i) {
        m_perm[i] = i;
    }

    // Fisher-Yates shuffle driven by seed
    std::mt19937_64 rng(seed);
    for (int i = 255; i > 0; --i) {
        int j = static_cast<int>(rng() % (i + 1));
        std::swap(m_perm[i], m_perm[j]);
    }

    // Double for wraparound-free indexing
    for (int i = 0; i < 256; ++i) {
        m_perm[i + 256] = m_perm[i];
    }
}

// ── Helper functions ──────────────────────────────────────────────────

float Noise::fade(float t) {
    // 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Noise::lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float Noise::grad2D(int hash, float x, float y) {
    int h = hash & 3;
    switch (h) {
        case 0:  return  x + y;
        case 1:  return -x + y;
        case 2:  return  x - y;
        default: return -x - y;
    }
}

float Noise::grad3D(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = (h < 8) ? x : y;
    float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
    if (h & 1) u = -u;
    if (h & 2) v = -v;
    return u + v;
}

// ── 2D Noise ─────────────────────────────────────────────────────────

float Noise::noise2D(float x, float y) const {
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    float u = fade(xf);
    float v = fade(yf);

    int aa = m_perm[m_perm[xi] + yi];
    int ab = m_perm[m_perm[xi] + yi + 1];
    int ba = m_perm[m_perm[xi + 1] + yi];
    int bb = m_perm[m_perm[xi + 1] + yi + 1];

    float x1 = lerp(grad2D(aa, xf, yf),     grad2D(ba, xf - 1, yf),     u);
    float x2 = lerp(grad2D(ab, xf, yf - 1), grad2D(bb, xf - 1, yf - 1), u);

    return lerp(x1, x2, v);
}

// ── 3D Noise ─────────────────────────────────────────────────────────

float Noise::noise3D(float x, float y, float z) const {
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;
    int zi = static_cast<int>(std::floor(z)) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float zf = z - std::floor(z);

    float u = fade(xf);
    float v = fade(yf);
    float w = fade(zf);

    int aaa = m_perm[m_perm[m_perm[xi] + yi] + zi];
    int aba = m_perm[m_perm[m_perm[xi] + yi + 1] + zi];
    int aab = m_perm[m_perm[m_perm[xi] + yi] + zi + 1];
    int abb = m_perm[m_perm[m_perm[xi] + yi + 1] + zi + 1];
    int baa = m_perm[m_perm[m_perm[xi + 1] + yi] + zi];
    int bba = m_perm[m_perm[m_perm[xi + 1] + yi + 1] + zi];
    int bab = m_perm[m_perm[m_perm[xi + 1] + yi] + zi + 1];
    int bbb = m_perm[m_perm[m_perm[xi + 1] + yi + 1] + zi + 1];

    float x1 = lerp(grad3D(aaa, xf, yf, zf),     grad3D(baa, xf - 1, yf, zf),     u);
    float x2 = lerp(grad3D(aba, xf, yf - 1, zf), grad3D(bba, xf - 1, yf - 1, zf), u);
    float y1 = lerp(x1, x2, v);

    x1 = lerp(grad3D(aab, xf, yf, zf - 1),     grad3D(bab, xf - 1, yf, zf - 1),     u);
    x2 = lerp(grad3D(abb, xf, yf - 1, zf - 1), grad3D(bbb, xf - 1, yf - 1, zf - 1), u);
    float y2 = lerp(x1, x2, v);

    return lerp(y1, y2, w);
}

// ── Fractal / Octave noise ────────────────────────────────────────────

float Noise::octave2D(float x, float y, int octaves,
                      float persistence, float lacunarity) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude * noise2D(x * frequency, y * frequency);
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

float Noise::octave3D(float x, float y, float z, int octaves,
                      float persistence, float lacunarity) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude * noise3D(x * frequency, y * frequency, z * frequency);
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

// ── Domain Warping ──────────────────────────────────────────────────────

float Noise::warped2D(float x, float y, int octaves,
                      float warpStrength, float warpScale) const {
    // Two independent warp fields (decorrelated by large offsets)
    float wx = octave2D(x * warpScale + 100.0f, y * warpScale + 100.0f,
                         2, 0.5f, 2.0f) * warpStrength;
    float wy = octave2D(x * warpScale + 900.0f, y * warpScale + 900.0f,
                         2, 0.5f, 2.0f) * warpStrength;
    return octave2D(x + wx, y + wy, octaves, 0.5f, 2.0f);
}
