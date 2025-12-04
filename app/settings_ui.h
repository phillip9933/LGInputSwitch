#pragma once
#include <windows.h>
#include "app_config.h"

// Open the Settings dialog (Modeless for tray)
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);

// Open the Settings dialog (Modal for first-run)
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg);
