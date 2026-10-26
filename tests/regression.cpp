#include "../app/app_toggle.h"
#include "../app/hotkeys.h"
#include "../core/input_value.h"
#include <cstdlib>
#include <iostream>

static int calls = 0, result = 0;
static unsigned int sent = 0;
extern "C" int SetVcpFeatureWithI2cAddr(int, int, unsigned short vcp,
    unsigned int value, unsigned int source) {
    if (vcp != 0xf4 || source != 0x50) std::abort();
    ++calls;
    sent = value;
    return result;
}
static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    unsigned int value = 123;
    for (const auto* bad : {"", "0x", "-1", "+1", "0x90junk", "65536", "99999999999999999999999"})
        Check(!ParseInputValue(bad, 0xffff, value), "Malformed command accepted");
    Check(ParseInputValue("0XD0", 0xffff, value) && value == 208, "Hex parse failed");
    Check(ParseInputValue("090", 0xffff, value) && value == 90, "Decimal interpreted as octal");
    Target target{5, 0};
    Check(!SendInputCode(target, "0x150", "0x90"), "Source address truncated");
    Check(!SendInputCode(target, "0x50", "0x10000"), "Input truncated");
    Check(!SendInputCode(target, "0x50", "garbage"), "Invalid input sent");
    Check(calls == 0, "Invalid config reached transport");
    std::vector<InputDef> inputs{{"HDMI1", "0x90"}, {"DisplayPort", "0xD0"}};
    int index = -1;
    result = -3;
    Check(!ToggleCycle(target, "0x50", inputs, index) && index == -1, "Failure advanced cycle");
    result = 0;
    Check(ToggleCycle(target, "0x50", inputs, index) && index == 0 && sent == 0x90, "Retry skipped input");
    Check(ToggleCycle(target, "0x50", inputs, index) && index == 1 && sent == 0xd0, "Cycle order changed");
    Check(ToggleCycle(target, "0x50", inputs, index) && index == 0, "Cycle did not wrap");
    index = 100;
    Check(ToggleCycle(target, "0x50", inputs, index) && index == 0, "Stale index not repaired");
    Check(!ToggleCycle(target, "0x50", {}, index) && index == 0, "Empty cycle changed state");
    HotkeySpec spec{};
    Check(ParseHotkey(" ctrl + alt + f12 ", spec) && spec.fsModifiers == (MOD_CONTROL | MOD_ALT)
        && spec.vk == VK_F1 + 11, "Valid hotkey rejected");
    for (const auto* bad : {"CTRL+", "CTRL++A", "CTRL+A+B", "CTRL+F12junk", "CTRL+F25", "CTRL+UNKNOWN", "A+?"})
        Check(!ParseHotkey(bad, spec) && spec.vk == 0, "Invalid hotkey accepted");
    std::cout << "Regression checks passed\n";
}
