#pragma once
#include <string>
#include <windows.h>

struct HotkeySpec { UINT fsModifiers; UINT vk; };

bool ParseHotkey(const std::string& spec, HotkeySpec& out); // "CTRL+ALT+F12"
