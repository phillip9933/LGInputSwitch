#pragma once
#include "update_policy.h"

// Windows: staged old-version helper waits for this process, verifies again,
// swaps only executables and keeps one .previous rollback copy. No shell script.
bool StartWindowsUpdate(const UpdateOffer &offer, const QString &download, QString &error);
int ApplyWindowsUpdate(const QString &planPath);
QString WindowsExecutableVersion(const QString &path);
