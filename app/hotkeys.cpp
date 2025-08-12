#include "hotkeys.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

static UINT ModFromToken(const std::string& t) {
    if (t == "CTRL") return MOD_CONTROL;
    if (t == "ALT")  return MOD_ALT;
    if (t == "SHIFT")return MOD_SHIFT;
    if (t == "WIN")  return MOD_WIN;
    return 0;
}
static UINT VkFromToken(const std::string& t) {
    if (t.size() == 1) {
        char c = t[0];
        if (c >= '0' && c <= '9') return 0x30 + (c - '0');
        if (c >= 'A' && c <= 'Z') return 0x41 + (c - 'A');
    }
    if (t.size() > 1 && t[0] == 'F') {
        int f = atoi(t.c_str() + 1);
        if (f >= 1 && f <= 24) return VK_F1 + (f - 1);
    }
    return 0;
}
bool ParseHotkey(const std::string& spec, HotkeySpec& out) {
    out = { 0,0 };
    std::string s = spec;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    size_t pos = 0, start = 0;
    while (true) {
        pos = s.find('+', start);
        std::string tok = (pos == std::string::npos) ? s.substr(start) : s.substr(start, pos - start);
        if (tok.empty()) break;
        UINT m = ModFromToken(tok);
        if (m) out.fsModifiers |= m; else out.vk = VkFromToken(tok);
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out.vk != 0;
}
