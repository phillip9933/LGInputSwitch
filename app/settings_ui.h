#pragma once
#include <windows.h>
#include "app_config.h"

// Initializes ADL and populates the Global Adapter Info ONCE.
void InitializeSystem();

// Dialog wrappers
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg);
