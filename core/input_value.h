#pragma once
#include <charconv>
#include <string_view>

// Config values are decimal or explicitly 0x-prefixed hex. Never truncate a
// malformed value into a different command sent to a monitor.
inline bool ParseInputValue(std::string_view text, unsigned int max, unsigned int& out) {
    int base = 10;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
        base = 16;
    }
    if (text.empty()) return false;
    unsigned int value = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > max)
        return false;
    out = value;
    return true;
}
