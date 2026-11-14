#pragma once

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace PixelReadback {
struct Region {
    int left, top, right, bottom;
};

// Readbacks have a top-left origin; UI coordinates have a bottom-left origin.
inline Region uiRegion(int x, int y, int width, int height, int imageHeight) {
    return {x, imageHeight-y-height, x+width, imageHeight-y};
}

inline bool differs(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t pixel, int tolerance) {
    for (size_t channel = 0; channel < 3; ++channel)
        if (std::abs(int(a[pixel*4+channel])-int(b[pixel*4+channel])) > tolerance) return true;
    return false;
}

// An effect must survive a repeated control, change a visible area at the
// expected location, and leave unrelated pixels alone. Do not use a lone
// whole-frame inequality as evidence that a particular consumer rendered.
inline void requireEffect(const std::vector<uint8_t>& control,
                          const std::vector<uint8_t>& repeatedControl,
                          const std::vector<uint8_t>& effect,
                          int width, int height, Region region,
                          const std::string& name, size_t minimumPixels = 16,
                          int channelTolerance = 4) {
    if (width <= 0 || height <= 0 || region.left < 0 || region.top < 0 ||
        region.right > width || region.bottom > height ||
        region.left >= region.right || region.top >= region.bottom || channelTolerance < 0 ||
        control.size() != size_t(width)*size_t(height)*4 ||
        repeatedControl.size() != control.size() || effect.size() != control.size())
        throw std::runtime_error(name + ": invalid readback dimensions/region");
    size_t unstable = 0, inside = 0, outside = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const size_t pixel = size_t(y)*size_t(width)+size_t(x);
        unstable += differs(control, repeatedControl, pixel, channelTolerance);
        if (!differs(control, effect, pixel, channelTolerance)) continue;
        if (x >= region.left && x < region.right && y >= region.top && y < region.bottom) ++inside;
        else ++outside;
    }
    if (unstable || inside < minimumPixels || outside)
        throw std::runtime_error(name + ": unstable=" + std::to_string(unstable) +
            ", expected-region=" + std::to_string(inside) +
            ", unrelated-region=" + std::to_string(outside));
}
}
