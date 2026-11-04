#pragma once
#include <string>

struct HotkeySpec { unsigned int fsModifiers = 0, vk = 0; };
bool ParseHotkey(const std::string &spec, HotkeySpec &out);
