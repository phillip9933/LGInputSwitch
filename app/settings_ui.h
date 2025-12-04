#pragma once
#include <windows.h>
#include "app_config.h"

// Pre-scans adapters/displays once and caches the result. 
// Safe to call multiple times (subsequent calls do nothing).
void PreloadTargets();

// Open the modal Settings dialog; returns true if saved
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);
