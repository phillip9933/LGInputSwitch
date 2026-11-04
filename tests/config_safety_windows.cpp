#include "../app/app_config.cpp"
#include <iostream>

static void check(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    auto config = Defaults();
    std::string label = "Custom";
    for (char control = 1; control < 32; ++control) label += control;
    label += " quote\" slash\\";
    config.inputs[0].label = label;
    config.cycleOrder[0] = label;
    config.hotkeys.direct[label] = "CTRL+ALT+F8";
    const auto parsed = json::parse(ToJson(config));
    check(parsed["inputs"][0]["label"].get<std::string>() == label &&
              parsed["cycleOrder"][0].get<std::string>() == label &&
              parsed["hotkeys"]["direct"].contains(label), "JSON control characters did not roundtrip");
    const auto list = CreateWindowExW(0, L"LISTBOX", L"", WS_POPUP | LBS_HASSTRINGS,
                                      0, 0, 200, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(list != nullptr, "List box fixture unavailable");
    const std::wstring longLabel(4096, L'x');
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(longLabel.c_str()));
    check(ListBoxText(list, 0) == longLabel && ListBoxText(list, 99).empty(),
          "Long or missing list-box label was not read safely");
    DestroyWindow(list);
}
