#pragma once

#include <cstdint>

// Stable physical-key codes based on USB HID usage IDs. Backends translate
// native events at the window boundary; gameplay and persisted settings never
// depend on a window-system library's public constants.
namespace Key {
constexpr int Unknown = 0;
constexpr int A = 4;
constexpr int C = 6;
constexpr int D = 7;
constexpr int E = 8;
constexpr int S = 22;
constexpr int T = 23;
constexpr int V = 25;
constexpr int W = 26;
constexpr int X = 27;
constexpr int Num1 = 30;
constexpr int Num2 = 31;
constexpr int Num3 = 32;
constexpr int Num4 = 33;
constexpr int Num5 = 34;
constexpr int Num6 = 35;
constexpr int Num7 = 36;
constexpr int Num8 = 37;
constexpr int Num9 = 38;
constexpr int Enter = 40;
constexpr int Escape = 41;
constexpr int Backspace = 42;
constexpr int Tab = 43;
constexpr int Space = 44;
constexpr int F4 = 61;
constexpr int Home = 74;
constexpr int Delete = 76;
constexpr int End = 77;
constexpr int Right = 79;
constexpr int Left = 80;
constexpr int Down = 81;
constexpr int Up = 82;
constexpr int LeftControl = 224;
constexpr int LeftShift = 225;
constexpr int LeftAlt = 226;
constexpr int RightAlt = 230;
constexpr int Count = 512;
}

namespace MouseButton {
constexpr int Left = 0;
constexpr int Right = 1;
constexpr int Middle = 2;
constexpr int Count = 16;
}

enum class ButtonAction : uint8_t { Release, Press, Repeat };

namespace KeyModifier {
constexpr uint16_t Shift = 1u << 0;
constexpr uint16_t Control = 1u << 1;
constexpr uint16_t Alt = 1u << 2;
constexpr uint16_t Super = 1u << 3;
}

int migrateLegacyGlfwKey(int legacyCode);
const char* physicalKeyName(int key);
