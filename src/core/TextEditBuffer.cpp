#include "core/TextEditBuffer.h"

#include "debug/Log.h"
#include "game/Utf8.h"
#include "platform/Clipboard.h"

#include <algorithm>

namespace {
bool isContinuation(unsigned char value) { return (value & 0xC0u) == 0x80u; }

bool validUtf8(std::string_view input) {
    std::string encoded;
    for (uint32_t codepoint : decodeUtf8(input)) {
        if (codepoint == 0xFFFDu) return false;
        appendUtf8(encoded, codepoint);
    }
    return encoded == input;
}
}

TextEditBuffer::TextEditBuffer(std::string text, size_t maximumCodepoints,
                               platform::Clipboard* clipboard)
    : m_maximumCodepoints(maximumCodepoints), m_clipboard(clipboard) {
    setText(std::move(text));
}

void TextEditBuffer::setText(std::string text) {
    const size_t cursor = text.size();
    setText(std::move(text), cursor);
}

void TextEditBuffer::setText(std::string text, size_t cursor) {
    if (!validUtf8(text)) text.clear();
    cursor = std::min(cursor, text.size());
    while (cursor > 0 && cursor < text.size() &&
           isContinuation(static_cast<unsigned char>(text[cursor])))
        --cursor;
    m_text = std::move(text);
    m_cursor = m_anchor = cursor;
}

size_t TextEditBuffer::previousBoundary(std::string_view text, size_t position) {
    if (position == 0) return 0;
    --position;
    while (position > 0 && isContinuation(static_cast<unsigned char>(text[position]))) --position;
    return position;
}

size_t TextEditBuffer::nextBoundary(std::string_view text, size_t position) {
    if (position >= text.size()) return text.size();
    ++position;
    while (position < text.size() && isContinuation(static_cast<unsigned char>(text[position]))) ++position;
    return position;
}

bool TextEditBuffer::eraseSelection() {
    if (!hasSelection()) return false;
    const size_t first = std::min(m_cursor, m_anchor);
    const size_t last = std::max(m_cursor, m_anchor);
    m_text.erase(first, last - first);
    m_cursor = m_anchor = first;
    return true;
}

bool TextEditBuffer::insert(std::string_view input) {
    if (!validUtf8(input)) return false;
    std::string accepted;
    std::string prospective=m_text;
    if(hasSelection()){const size_t first=std::min(m_cursor,m_anchor),last=std::max(m_cursor,m_anchor);prospective.erase(first,last-first);}
    const size_t selectedCount = utf8CodepointCount(selectedText());
    size_t count = utf8CodepointCount(m_text) - selectedCount;
    for (uint32_t codepoint : decodeUtf8(input)) {
        if (codepoint == '\r' || codepoint == '\n') continue;
        if (m_maximumCodepoints && count >= m_maximumCodepoints) break;
        if (m_filter && !m_filter(codepoint, prospective+accepted)) continue;
        appendUtf8(accepted, codepoint);
        ++count;
    }
    if (accepted.empty()) return false;
    eraseSelection();
    m_text.insert(m_cursor, accepted);
    m_cursor += accepted.size();
    m_anchor = m_cursor;
    return true;
}

bool TextEditBuffer::backspace() {
    if (eraseSelection()) return true;
    if (m_cursor == 0) return false;
    const size_t previous = previousBoundary(m_text, m_cursor);
    m_text.erase(previous, m_cursor - previous);
    m_cursor = m_anchor = previous;
    return true;
}

bool TextEditBuffer::eraseForward() {
    if (eraseSelection()) return true;
    if (m_cursor == m_text.size()) return false;
    m_text.erase(m_cursor, nextBoundary(m_text, m_cursor) - m_cursor);
    m_anchor = m_cursor;
    return true;
}

void TextEditBuffer::moveLeft(bool selecting) {
    m_cursor = previousBoundary(m_text, m_cursor);
    if (!selecting) m_anchor = m_cursor;
}
void TextEditBuffer::moveRight(bool selecting) {
    m_cursor = nextBoundary(m_text, m_cursor);
    if (!selecting) m_anchor = m_cursor;
}
void TextEditBuffer::moveHome(bool selecting) { m_cursor = 0; if (!selecting) m_anchor = 0; }
void TextEditBuffer::moveEnd(bool selecting) { m_cursor = m_text.size(); if (!selecting) m_anchor = m_cursor; }
void TextEditBuffer::selectAll() { m_anchor = 0; m_cursor = m_text.size(); }

std::string TextEditBuffer::selectedText() const {
    const size_t first = std::min(m_cursor, m_anchor);
    return m_text.substr(first, std::max(m_cursor, m_anchor) - first);
}

bool TextEditBuffer::copySelection() const {
    return hasSelection() && m_clipboard && m_clipboard->writeText(selectedText());
}

bool TextEditBuffer::cutSelection() { return copySelection() && eraseSelection(); }

bool TextEditBuffer::pasteClipboard() {
    std::string copy;
    if (!m_clipboard || !m_clipboard->readText(copy)) return false;
    if (copy.empty()) return false;
    return insert(copy);
}
