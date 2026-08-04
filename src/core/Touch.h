#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

enum class TouchPhase : uint8_t { Begin, Move, End, Cancel };

struct TouchContactId {
    uint64_t device = 0;
    uint64_t finger = 0;
    bool operator==(const TouchContactId& other) const {
        return device == other.device && finger == other.finger;
    }
    bool operator!=(const TouchContactId& other) const { return !(*this == other); }
};

struct TouchContactHash {
    size_t operator()(const TouchContactId& id) const {
        const size_t a = std::hash<uint64_t>{}(id.device);
        const size_t b = std::hash<uint64_t>{}(id.finger);
        return a ^ (b + 0x9e3779b9u + (a << 6u) + (a >> 2u));
    }
};

struct TouchEvent {
    TouchContactId id;
    TouchPhase phase = TouchPhase::Cancel;
    double x = 0.0;
    // Native window client/surface coordinates with a top-left origin.
    double y = 0.0;
};
