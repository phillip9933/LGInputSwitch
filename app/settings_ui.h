#pragma once
#include <windows.h>
#include "app_config.h"

// Initialize ADL and populate global state ONCE.
void InitializeSystem();

// Dialog functions
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg);
