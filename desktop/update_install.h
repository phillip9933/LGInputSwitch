#pragma once
#include "update_policy.h"

#ifdef Q_OS_WIN
bool WindowsUpdateAllowed();
inline constexpr auto WindowsUpdatePrivilegeMessage =
    "Close LG Input Switch and reopen it without administrator privileges to install updates.";
#endif

// Windows: staged old-version helper waits for this process, verifies again,
// swaps only executables and keeps one .previous rollback copy. No shell script.
bool StartWindowsUpdate(const UpdateOffer &offer, const QString &download, QString &error);
int ApplyWindowsUpdate(const QString &planPath);
QString WindowsExecutableVersion(const QString &path);
void CleanupWindowsUpdateFiles();
