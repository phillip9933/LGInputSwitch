#pragma once
#include <windows.h>
#include "app_config.h"

// INITIALIZATION:
// This function initializes ADL and populates the Global Adapter Info.
// It runs EXACTLY ONCE to avoid resetting the driver state.
void InitializeSystem();

bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg);
