#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Authored UI colors are sRGB, whereas the fragment shader and atlas sampling
// operate in linear light before the swapchain's output conversion.
inline float uiSrgbToLinear(float value) {
    value=std::clamp(value,0.0f,1.0f);
    return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);
}
inline glm::vec4 uiLinearColor(const glm::vec4& color,float opacity=1) {
    return {uiSrgbToLinear(color.r),uiSrgbToLinear(color.g),uiSrgbToLinear(color.b),
            color.a*std::clamp(opacity,0.0f,1.0f)};
}
