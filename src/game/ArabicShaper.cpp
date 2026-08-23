#include "game/ArabicShaper.h"

#include "game/Utf8.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace {

// How a base Arabic letter joins its neighbours.
enum class JoinType : uint8_t { None, Right, Dual, Causing };

JoinType joinType(uint32_t cp) {
    switch (cp) {
        case 0x0621:                                                          // ء hamza
            return JoinType::None;
        case 0x0640: return JoinType::Causing;                                // tatweel
        case 0x0622: case 0x0623: case 0x0624: case 0x0625:                   // آ أ ؤ إ
        case 0x0627: case 0x0629: case 0x062F: case 0x0630:                   // ا ة د ذ
        case 0x0631: case 0x0632: case 0x0648: case 0x0649:                   // ر ز و ى
        case 0x0671:                                                          // ٱ alef wasla
            return JoinType::Right;
        case 0x0626: case 0x0628: case 0x062A: case 0x062B:                   // ئ ب ت ث
        case 0x062C: case 0x062D: case 0x062E: case 0x0633:                   // ج ح خ س
        case 0x0634: case 0x0635: case 0x0636: case 0x0637:                   // ش ص ض ط
        case 0x0638: case 0x0639: case 0x063A: case 0x0641:                   // ظ ع غ ف
        case 0x0642: case 0x0643: case 0x0644: case 0x0645:                   // ق ك ل م
        case 0x0646: case 0x0647: case 0x064A: case 0x066E:                   // ن ه ي ٮ
        case 0x066F:                                                          // ٯ
            return JoinType::Dual;
        default: return JoinType::None;
    }
}

// Presentation Forms-B for a dual-joining letter: isolated, final, initial,
// medial. Returns 0 when the codepoint has no listed form.
uint32_t dualForm(uint32_t cp, int which) {
    switch (cp) {
        case 0x0626: { const uint32_t f[4] = {0xFE89, 0xFE8A, 0xFE8B, 0xFE8C}; return f[which]; }
        case 0x0628: { const uint32_t f[4] = {0xFE8F, 0xFE90, 0xFE91, 0xFE92}; return f[which]; }
        case 0x062A: { const uint32_t f[4] = {0xFE95, 0xFE96, 0xFE97, 0xFE98}; return f[which]; }
        case 0x062B: { const uint32_t f[4] = {0xFE99, 0xFE9A, 0xFE9B, 0xFE9C}; return f[which]; }
        case 0x062C: { const uint32_t f[4] = {0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0}; return f[which]; }
        case 0x062D: { const uint32_t f[4] = {0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4}; return f[which]; }
        case 0x062E: { const uint32_t f[4] = {0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8}; return f[which]; }
        case 0x0633: { const uint32_t f[4] = {0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4}; return f[which]; }
        case 0x0634: { const uint32_t f[4] = {0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8}; return f[which]; }
        case 0x0635: { const uint32_t f[4] = {0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC}; return f[which]; }
        case 0x0636: { const uint32_t f[4] = {0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0}; return f[which]; }
        case 0x0637: { const uint32_t f[4] = {0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4}; return f[which]; }
        case 0x0638: { const uint32_t f[4] = {0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8}; return f[which]; }
        case 0x0639: { const uint32_t f[4] = {0xFEC9, 0xFECA, 0xFECB, 0xFECC}; return f[which]; }
        case 0x063A: { const uint32_t f[4] = {0xFECD, 0xFECE, 0xFECF, 0xFED0}; return f[which]; }
        case 0x0641: { const uint32_t f[4] = {0xFED1, 0xFED2, 0xFED3, 0xFED4}; return f[which]; }
        case 0x0642: { const uint32_t f[4] = {0xFED5, 0xFED6, 0xFED7, 0xFED8}; return f[which]; }
        case 0x0643: { const uint32_t f[4] = {0xFED9, 0xFEDA, 0xFEDB, 0xFEDC}; return f[which]; }
        case 0x0644: { const uint32_t f[4] = {0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0}; return f[which]; }
        case 0x0645: { const uint32_t f[4] = {0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4}; return f[which]; }
        case 0x0646: { const uint32_t f[4] = {0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8}; return f[which]; }
        case 0x0647: { const uint32_t f[4] = {0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC}; return f[which]; }
        case 0x064A: { const uint32_t f[4] = {0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4}; return f[which]; }
        case 0x066E: { const uint32_t f[4] = {0xFB56, 0xFB57, 0xFB58, 0xFB59}; return f[which]; }
        case 0x066F: { const uint32_t f[4] = {0xFB62, 0xFB63, 0xFB64, 0xFB65}; return f[which]; }
        default: return 0;
    }
}

// Presentation Forms-B for a right-joining letter: isolated, final.
uint32_t rightForm(uint32_t cp, int which) {
    switch (cp) {
        case 0x0622: { const uint32_t f[2] = {0xFE81, 0xFE82}; return f[which]; }
        case 0x0623: { const uint32_t f[2] = {0xFE83, 0xFE84}; return f[which]; }
        case 0x0624: { const uint32_t f[2] = {0xFE85, 0xFE86}; return f[which]; }
        case 0x0625: { const uint32_t f[2] = {0xFE87, 0xFE88}; return f[which]; }
        case 0x0627: { const uint32_t f[2] = {0xFE8D, 0xFE8E}; return f[which]; }
        case 0x0629: { const uint32_t f[2] = {0xFE93, 0xFE94}; return f[which]; }
        case 0x062F: { const uint32_t f[2] = {0xFEA9, 0xFEAA}; return f[which]; }
        case 0x0630: { const uint32_t f[2] = {0xFEAB, 0xFEAC}; return f[which]; }
        case 0x0631: { const uint32_t f[2] = {0xFEAD, 0xFEAE}; return f[which]; }
        case 0x0632: { const uint32_t f[2] = {0xFEAF, 0xFEB0}; return f[which]; }
        case 0x0648: { const uint32_t f[2] = {0xFEED, 0xFEEE}; return f[which]; }
        case 0x0649: { const uint32_t f[2] = {0xFEEF, 0xFEF0}; return f[which]; }
        case 0x0671: { const uint32_t f[2] = {0xFB50, 0xFB51}; return f[which]; }
        default: return 0;
    }
}

// Mandatory lam-alef ligatures: isolated, final.
uint32_t lamAlefForm(uint32_t alef, int which) {
    switch (alef) {
        case 0x0627: { const uint32_t f[2] = {0xFEF5, 0xFEF6}; return f[which]; }
        case 0x0623: { const uint32_t f[2] = {0xFEF7, 0xFEF8}; return f[which]; }
        case 0x0625: { const uint32_t f[2] = {0xFEF9, 0xFEFA}; return f[which]; }
        case 0x0622: { const uint32_t f[2] = {0xFEFB, 0xFEFC}; return f[which]; }
        default: return 0;
    }
}

bool isArabicScript(uint32_t cp) {
    return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
           (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
           (cp >= 0xFE70 && cp <= 0xFEFF);
}

bool isDigit(uint32_t cp) {
    return (cp >= '0' && cp <= '9') || (cp >= 0x0660 && cp <= 0x0669) ||
           (cp >= 0x06F0 && cp <= 0x06F9);
}

bool isLatin(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

// Joins letters into presentation forms. Non-Arabic characters reset the
// joining state, exactly like a real shaping engine treats script breaks.
std::vector<uint32_t> joinLetters(const std::vector<uint32_t>& input) {
    std::vector<uint32_t> output;
    output.reserve(input.size());
    bool prevConnects = false;
    for (size_t i = 0; i < input.size(); ++i) {
        const uint32_t cp = input[i];
        // Mandatory lam-alef ligature.
        if (cp == 0x0644 && i + 1 < input.size() &&
            (input[i + 1] == 0x0622 || input[i + 1] == 0x0623 ||
             input[i + 1] == 0x0625 || input[i + 1] == 0x0627)) {
            output.push_back(lamAlefForm(input[i + 1], prevConnects ? 1 : 0));
            ++i;
            prevConnects = false;
            continue;
        }
        const JoinType type = joinType(cp);
        if (type == JoinType::Causing) {
            const bool nextJoins = i + 1 < input.size() &&
                joinType(input[i + 1]) != JoinType::None;
            output.push_back(cp);
            prevConnects = nextJoins;
            continue;
        }
        if (type == JoinType::Right) {
            output.push_back(rightForm(cp, prevConnects ? 1 : 0));
            prevConnects = false;
            continue;
        }
        if (type == JoinType::Dual) {
            const bool nextJoins = i + 1 < input.size() &&
                joinType(input[i + 1]) != JoinType::None;
            const int which = prevConnects ? (nextJoins ? 3 : 1)
                                           : (nextJoins ? 2 : 0);
            output.push_back(dualForm(cp, which));
            prevConnects = nextJoins;
            continue;
        }
        output.push_back(cp);
        prevConnects = false;
    }
    return output;
}

enum class Dir : uint8_t { Left, Right };

std::optional<Dir> strongDirection(uint32_t cp) {
    if (isLatin(cp) || isDigit(cp)) return Dir::Left;
    if (isArabicScript(cp)) return Dir::Right;
    return std::nullopt;
}

// Weak direction resolution for an Arabic base direction: strong classes are
// Latin/digits (left) and Arabic script (right). A neutral resolves left only
// when it sits strictly between two left runs; otherwise it joins the Arabic
// side, which keeps punctuation such as ":" glued to the words around it.
std::vector<Dir> resolveDirections(const std::vector<uint32_t>& input) {
    std::vector<Dir> result(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (const auto strong = strongDirection(input[i])) {
            result[i] = *strong;
            continue;
        }
        std::optional<Dir> left, right;
        for (size_t j = i; j-- > 0;) {
            if (const auto strong = strongDirection(input[j])) { left = strong; break; }
        }
        for (size_t j = i + 1; j < input.size(); ++j) {
            if (const auto strong = strongDirection(input[j])) { right = strong; break; }
        }
        result[i] = left && right && *left == Dir::Left && *right == Dir::Left
            ? Dir::Left : Dir::Right;
    }
    return result;
}

std::string encode(const std::vector<uint32_t>& codepoints) {
    std::string result;
    for (uint32_t cp : codepoints) appendUtf8(result, cp);
    return result;
}

} // namespace

std::string shapeArabic(std::string_view text) {
    const std::vector<uint32_t> input = decodeUtf8(text);
    if (input.empty()) return {};

    const std::vector<uint32_t> joined = joinLetters(input);
    const std::vector<Dir> directions = resolveDirections(joined);

    // Split into maximal same-direction runs, reverse the run order, and
    // reverse each right run so the string draws correctly left to right.
    std::vector<std::pair<std::vector<uint32_t>, Dir>> runs;
    for (size_t i = 0; i < joined.size();) {
        const Dir dir = directions[i];
        std::vector<uint32_t> run;
        while (i < joined.size() && directions[i] == dir) run.push_back(joined[i++]);
        runs.emplace_back(std::move(run), dir);
    }
    std::vector<uint32_t> visual;
    for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
        if (it->second == Dir::Right)
            visual.insert(visual.end(), it->first.rbegin(), it->first.rend());
        else
            visual.insert(visual.end(), it->first.begin(), it->first.end());
    }
    return encode(visual);
}
