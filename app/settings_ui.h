#pragma once
#include <windows.h>
#include "app_config.h"

// Show settings dialog modeless (returns immediately) — used by tray menu.
// The dialog will post WM_APP+2 to the parent when the user saves.
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);

// Show settings dialog modally (blocking) — used at first-run before hotkeys registered.
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg);
