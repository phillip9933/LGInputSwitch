#pragma once
#include "platform.h"

// Read-only compatibility with pre-profile settings. Workspace saves never
// write back to config.json or the old per-input Qt preference keys.
DesktopConfig LoadLegacyDesktopConfig(QString &warning);
DesktopConfig ImportLegacyWindowsConfig(const QByteArray &bytes, const QJsonArray &storedRows, QString &warning);
DesktopConfig LoadLegacyWindowsConfig(const QString &path, const QJsonArray &storedRows, QString &warning);
