#pragma once
#include <windows.h>
#include "app_config.h"

// Open the modal Settings dialog; returns true if saved
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);
