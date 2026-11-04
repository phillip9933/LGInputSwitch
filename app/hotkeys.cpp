#include "hotkeys.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <charconv>

static UINT ModFromToken(const std::string& t) {
    if (t == "CTRL") return MOD_CONTROL;
    if (t == "ALT")  return MOD_ALT;
    if (t == "SHIFT")return MOD_SHIFT;
    if (t == "WIN" || t == "META")  return MOD_WIN;
    return 0;
}
static UINT VkFromToken(const std::string& t) {
    if (t.size() == 1) {
        char c = t[0];
        if (c >= '0' && c <= '9') return 0x30 + (c - '0');
        if (c >= 'A' && c <= 'Z') return 0x41 + (c - 'A');
    }
    if (t.size() > 1 && t[0] == 'F') {
        int f = 0;
        auto parsed = std::from_chars(t.data() + 1, t.data() + t.size(), f);
        if (parsed.ec == std::errc{} && parsed.ptr == t.data() + t.size() &&
            f >= 1 && f <= 24) return VK_F1 + (f - 1);
    }
    // Qt PortableText names for common navigation keys. Values are Win32 VKs.
    static const struct { const char *name; UINT key; } navigation[] = {
        {"HOME", 0x24}, {"END", 0x23}, {"LEFT", 0x25}, {"UP", 0x26},
        {"RIGHT", 0x27}, {"DOWN", 0x28}, {"PGUP", 0x21}, {"PGDOWN", 0x22},
        {"INS", 0x2d}, {"DEL", 0x2e}, {"TAB", 0x09}, {"ESC", 0x1b},
        {"SPACE", 0x20}, {"BACKSPACE", 0x08}, {"RETURN", 0x0d}
    };
    for (const auto &entry : navigation)
        if (t == entry.name) return entry.key;
    return 0;
}
bool ParseHotkey(const std::string& spec, HotkeySpec& out) {
    out = { 0,0 };
    std::string s = spec;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    size_t pos = 0, start = 0;
    HotkeySpec parsed{};
    while (true) {
        pos = s.find('+', start);
        std::string tok = (pos == std::string::npos) ? s.substr(start) : s.substr(start, pos - start);
        auto first = tok.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return false;
        tok = tok.substr(first, tok.find_last_not_of(" \t\r\n") - first + 1);
        UINT m = ModFromToken(tok);
        if (m) parsed.fsModifiers |= m;
        else {
            UINT vk = VkFromToken(tok);
            if (!vk || parsed.vk) return false;
            parsed.vk = vk;
        }
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (!parsed.vk || (!(parsed.fsModifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)) &&
        (parsed.vk < VK_F1 || parsed.vk > VK_F1 + 23))) return false;
    out = parsed;
    return true;
}
