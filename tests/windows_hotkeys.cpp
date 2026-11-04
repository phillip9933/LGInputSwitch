#include "../desktop/windows_hotkeys.h"
#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    HotkeySpec spec;
    Check(ParseHotkey(" ctrl + alt + f12 ", spec) && spec.fsModifiers == 3 && spec.vk == 0x7b,
          "Valid hotkey rejected");
    Check(ParseHotkey("Meta+Alt+F8", spec) && spec.fsModifiers == 9,
          "Qt Meta modifier was not mapped to the Windows key");
    Check(ParseHotkey("Ctrl+Home", spec) && spec.vk == 0x24, "Navigation hotkey rejected");
    Check(ParseHotkey("Ctrl+PgDown", spec) && spec.vk == 0x22, "Page navigation hotkey rejected");
    Check(ParseHotkey("F8", spec) && spec.fsModifiers == 0, "Standalone function key rejected");
    for (const auto *bad : {"A", "SHIFT+A", "1", "Home", "CTRL+", "CTRL++A", "CTRL+A+B",
                            "CTRL+F12junk", "CTRL+F25", "CTRL+UNKNOWN", "A+?"})
        Check(!ParseHotkey(bad, spec) && spec.vk == 0, "Invalid hotkey accepted");
}
