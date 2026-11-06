#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

// Level and normalized progress are independent: Java's level commands preserve
// the fraction of the bar, rather than preserving the number of points in it.
class Experience {
public:
    static constexpr int32_t MAX_LEVEL = 238609312;
    int32_t level() const { return m_level; }
    double progress() const { return m_progress; }
    static int64_t nextLevelPoints(int32_t level) {
        return level <= 15 ? 2LL * level + 7 :
               level <= 30 ? 5LL * level - 38 : 9LL * level - 158;
    }
    static int64_t pointsAtLevel(int32_t level) {
        const int64_t l = level;
        return l <= 16 ? l*l + 6*l : l <= 31 ? (5*l*l - 81*l + 720)/2 :
            (9*l*l - 325*l + 4440)/2;
    }
    int64_t points() const {
        return std::min(nextLevelPoints(m_level)-1,
            static_cast<int64_t>(std::floor(m_progress * static_cast<double>(nextLevelPoints(m_level)) + 1e-7)));
    }
    static bool valid(int32_t level, double progress) {
        return level >= 0 && level <= MAX_LEVEL && std::isfinite(progress) &&
            progress >= 0 && progress < 1 && (level != MAX_LEVEL || progress == 0);
    }
    void restore(int32_t level, double progress) {
        if (!valid(level, progress)) throw std::runtime_error("Invalid player experience");
        m_level = level; m_progress = progress;
    }
    void setLevel(int64_t level) {
        if (level < 0) { clear(); return; }
        m_level = static_cast<int32_t>(std::min<int64_t>(level, MAX_LEVEL));
        if (m_level == MAX_LEVEL) m_progress = 0;
    }
    void addLevels(int32_t amount) { setLevel(int64_t(m_level) + amount); }
    bool setPoints(int32_t amount) {
        if (amount < 0 || amount >= nextLevelPoints(m_level) || m_level == MAX_LEVEL) return false;
        m_progress = double(amount) / static_cast<double>(nextLevelPoints(m_level)); return true;
    }
    void addPoints(int32_t amount) {
        long double total = std::clamp<long double>(
            static_cast<long double>(pointsAtLevel(m_level)) + static_cast<long double>(m_progress) * static_cast<long double>(nextLevelPoints(m_level)) + amount,
            0.0L, static_cast<long double>(pointsAtLevel(MAX_LEVEL)));
        if(std::abs(total-std::round(total))<1e-7L)total=std::round(total);
        int32_t lo = 0, hi = MAX_LEVEL;
        while (lo < hi) {
            const int32_t mid = lo + (hi-lo+1)/2;
            if (static_cast<long double>(pointsAtLevel(mid)) <= total) lo = mid; else hi = mid-1;
        }
        m_level = lo;
        m_progress = lo == MAX_LEVEL ? 0 : std::clamp(
            double((total-static_cast<long double>(pointsAtLevel(lo)))/static_cast<long double>(nextLevelPoints(lo))), 0.0, std::nextafter(1.0, 0.0));
    }
    uint32_t deathDrop() const { return static_cast<uint32_t>(std::min<int64_t>(7LL*m_level, 100)); }
    void clear() { m_level = 0; m_progress = 0; }
private:
    int32_t m_level = 0;
    double m_progress = 0;
};

inline constexpr std::array<uint32_t,11> EXPERIENCE_ORB_VALUES{{2477,1237,617,307,149,73,37,17,7,3,1}};
struct ExperienceOrbState {
    uint32_t value = 0, count = 0;
    bool valid(bool orb) const {
        return orb ? count > 0 && count <= INT32_MAX &&
            std::find(EXPERIENCE_ORB_VALUES.begin(),EXPERIENCE_ORB_VALUES.end(),value) != EXPERIENCE_ORB_VALUES.end() :
            value == 0 && count == 0;
    }
};
