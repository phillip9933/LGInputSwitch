#pragma once
#include <windows.h>
#include "app_config.h"

// Scans for monitors/adapters and populates the internal list.
// Call this ONCE from main.cpp immediately after InitADL().
void RefreshTargetList();

// Open the modal Settings dialog; returns true if saved
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);
