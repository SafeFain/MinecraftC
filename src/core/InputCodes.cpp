#include "core/InputCodes.h"

#include <array>
#include <cstdio>

int migrateLegacyGlfwKey(int code) {
    // GLFW printable key values follow ASCII, while the project stores stable
    // USB HID usage IDs (also the physical-key domain used by SDL scancodes).
    if (code >= 'A' && code <= 'Z') return 4 + code - 'A';
    if (code >= '1' && code <= '9') return 30 + code - '1';
    if (code == '0') return 39;
    switch (code) {
        case 32: return 44;  // Space
        case 39: return 52;  // Apostrophe
        case 44: return 54;  // Comma
        case 45: return 45;  // Minus
        case 46: return 55;  // Period
        case 47: return 56;  // Slash
        case 59: return 51;  // Semicolon
        case 61: return 46;  // Equal
        case 91: return 47;  // Left bracket
        case 92: return 49;  // Backslash
        case 93: return 48;  // Right bracket
        case 96: return 53;  // Grave accent
        case 256: return 41; // Escape
        case 257: return 40; // Enter
        case 258: return 43; // Tab
        case 259: return 42; // Backspace
        case 260: return 73; // Insert
        case 261: return 76; // Delete
        case 262: return 79; // Right
        case 263: return 80; // Left
        case 264: return 81; // Down
        case 265: return 82; // Up
        case 266: return 75; // Page up
        case 267: return 78; // Page down
        case 268: return 74; // Home
        case 269: return 77; // End
        case 280: return 57; // Caps lock
        case 281: return 71; // Scroll lock
        case 282: return 83; // Num lock
        case 283: return 70; // Print screen
        case 284: return 72; // Pause
        case 320: return 98; // Keypad 0
        case 321: return 89; // Keypad 1
        case 322: return 90;
        case 323: return 91;
        case 324: return 92;
        case 325: return 93;
        case 326: return 94;
        case 327: return 95;
        case 328: return 96;
        case 329: return 97; // Keypad 9
        case 330: return 99; // Keypad decimal
        case 331: return 84; // Keypad divide
        case 332: return 85; // Keypad multiply
        case 333: return 86; // Keypad subtract
        case 334: return 87; // Keypad add
        case 335: return 88; // Keypad enter
        case 336: return 103; // Keypad equal
        case 340: return 225;
        case 341: return 224;
        case 342: return 226;
        case 343: return 227;
        case 344: return 229;
        case 345: return 228;
        case 346: return 230;
        case 347: return 231;
        case 348: return 101; // Menu
        default: break;
    }
    if (code >= 290 && code <= 301) return 58 + code - 290; // F1-F12
    if (code >= 302 && code <= 313) return 104 + code - 302; // F13-F24
    return Key::Unknown;
}

const char* physicalKeyName(int key) {
    if (key <= Key::Unknown || key >= Key::Count) return nullptr;
    static constexpr std::array<const char*, 40> names = {{
        nullptr, nullptr, nullptr, nullptr,
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
        "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X",
        "Y", "Z", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"
    }};
    if (key < static_cast<int>(names.size())) return names[static_cast<size_t>(key)];
    switch (key) {
        case 40: return "Enter";
        case 41: return "Escape";
        case 42: return "Backspace";
        case 43: return "Tab";
        case 44: return "Space";
        case 56: return "/";
        case 73: return "Insert";
        case 74: return "Home";
        case 75: return "Page Up";
        case 76: return "Delete";
        case 77: return "End";
        case 78: return "Page Down";
        case 79: return "Right";
        case 80: return "Left";
        case 81: return "Down";
        case 82: return "Up";
        case 224: return "Left Ctrl";
        case 225: return "Left Shift";
        case 226: return "Left Alt";
        case 227: return "Left GUI";
        case 228: return "Right Ctrl";
        case 229: return "Right Shift";
        case 230: return "Right Alt";
        case 231: return "Right GUI";
        default: break;
    }
    static thread_local char generated[16];
    if (key >= 58 && key <= 69) {
        std::snprintf(generated, sizeof(generated), "F%d", key - 57);
        return generated;
    }
    std::snprintf(generated, sizeof(generated), "Key %d", key);
    return generated;
}
