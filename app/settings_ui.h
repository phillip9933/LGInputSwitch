#pragma once
#include <windows.h>
#include "app_config.h"

// INITIALIZATION:
// This function initializes ADL, populates the Global Adapter Info (for hotkeys),
// and populates the Target List (for the UI).
// It runs EXACTLY ONCE. Subsequent calls return immediately to protect the driver state.
void InitializeSystem();

// Show settings dialog modeless (returns immediately) — used by tray menu.
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);

// Show settings dialog modally (blocking) — used at first-run.
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg);
