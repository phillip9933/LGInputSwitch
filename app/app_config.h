#pragma once
#include <string>
#include <vector>
#include <map>
#include "types.h"

struct HotkeysCfg {
    std::string cycle;
    std::map<std::string, std::string> direct; // label -> hotkey
};

struct AppConfig {
    std::vector<std::pair<int, int>> targets; // {adapter, display} (use first)
    std::vector<InputDef> inputs;            // available inputs
    std::vector<std::string> cycleOrder;     // ordered labels
    std::string i2cSourceAddr = "0x50";
    HotkeysCfg hotkeys;
    int debounceMs = 750;
    bool showNotifications = true;
    bool startWithWindows = false;
};

std::string ConfigPath();
bool EnsureConfigDir();
bool LoadConfig(AppConfig& out);
bool SaveConfig(const AppConfig& cfg);
