#pragma once

#include <string>
#include <string_view>

// Converts logical-order Arabic text into a visual-order string the UI can
// draw left to right: letters are joined via Arabic Presentation Forms-B and
// runs are reordered right to left. The bundled NotoNaskhArabic font contains
// glyphs for every presentation form this function emits. Non-Arabic text
// passes through unchanged.
std::string shapeArabic(std::string_view text);
