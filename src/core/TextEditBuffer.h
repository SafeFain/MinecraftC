#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace platform { class Clipboard; }

class TextEditBuffer {
public:
    using Filter = std::function<bool(uint32_t, const std::string&)>;

    explicit TextEditBuffer(std::string text = {}, size_t maximumCodepoints = 0,
                            platform::Clipboard* clipboard = nullptr);
    void setClipboard(platform::Clipboard* clipboard) { m_clipboard = clipboard; }

    const std::string& text() const { return m_text; }
    void setText(std::string text);
    void setText(std::string text, size_t cursor);
    size_t cursor() const { return m_cursor; }
    size_t anchor() const { return m_anchor; }
    bool hasSelection() const { return m_cursor != m_anchor; }
    void setFilter(Filter filter) { m_filter = std::move(filter); }

    bool insert(std::string_view utf8);
    bool backspace();
    bool eraseForward();
    void moveLeft(bool selecting = false);
    void moveRight(bool selecting = false);
    void moveHome(bool selecting = false);
    void moveEnd(bool selecting = false);
    void selectAll();
    std::string selectedText() const;
    bool copySelection() const;
    bool cutSelection();
    bool pasteClipboard();

private:
    std::string m_text;
    size_t m_cursor = 0;
    size_t m_anchor = 0;
    size_t m_maximumCodepoints = 0;
    Filter m_filter;
    platform::Clipboard* m_clipboard = nullptr;

    bool eraseSelection();
    static size_t previousBoundary(std::string_view text, size_t position);
    static size_t nextBoundary(std::string_view text, size_t position);
};
