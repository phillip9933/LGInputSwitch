#include "update_install.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <winver.h>
#include <vector>
namespace {
QString WinError() { return "Windows error " + QString::number(GetLastError()); }
bool Move(const QString &from, const QString &to, bool replace) {
    return MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()), reinterpret_cast<LPCWSTR>(to.utf16()),
                      MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0));
}
QString Creation(HANDLE process) {
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &create, &exit, &kernel, &user)) return {};
    return QString::number((quint64(create.dwHighDateTime) << 32) | create.dwLowDateTime);
}
bool Restart(const QString &target) {
    return QProcess::startDetached(target, {}, QFileInfo(target).absolutePath());
}
void Result(const QString &message) {
    QSettings settings("LGInputSwitch", "LGInputSwitchDesktop");
    settings.setValue("updates/result", message); settings.sync();
}
void RememberCleanup(const QString &prefix) {
    QSettings settings("LGInputSwitch", "LGInputSwitchDesktop");
    settings.setValue("updates/cleanup", QStringList{prefix + "-helper.exe", prefix + "-new.exe",
                                                  prefix + ".json", prefix + ".json.ready", prefix + ".json.started"});
    settings.sync();
}
}
void CleanupWindowsUpdateFiles() {
    QSettings settings("LGInputSwitch", "LGInputSwitchDesktop");
    const auto parent = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    QStringList remaining;
    for (const auto &path : settings.value("updates/cleanup").toStringList()) {
        const QFileInfo info(path);
        if (info.absolutePath() != parent || info.isSymLink() ||
            !QRegularExpression("^\\.lginputswitch-update-[0-9a-f-]{36}(-helper\\.exe|-new\\.exe|\\.json|\\.json\\.ready|\\.json\\.started)$")
                 .match(info.fileName()).hasMatch()) continue;
        if (info.exists() && !QFile::remove(path)) remaining.push_back(path);
    }
    settings.setValue("updates/cleanup", remaining); settings.sync();
}
QString WindowsExecutableVersion(const QString &path) {
    DWORD unused = 0;
    const auto length = GetFileVersionInfoSizeW(reinterpret_cast<LPCWSTR>(path.utf16()), &unused);
    if (!length || length > 1024 * 1024) return {};
    std::vector<char> data(length);
    if (!GetFileVersionInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, length, data.data())) return {};
    VS_FIXEDFILEINFO *info = nullptr; UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &bytes) || bytes < sizeof(*info) ||
        info->dwSignature != 0xfeef04bd || LOWORD(info->dwProductVersionLS) != 0) return {};
    return QString("%1.%2.%3").arg(HIWORD(info->dwProductVersionMS)).arg(LOWORD(info->dwProductVersionMS)).arg(HIWORD(info->dwProductVersionLS));
}
bool StartWindowsUpdate(const UpdateOffer &offer, const QString &download, QString &error) {
    if (!VerifyUpdateFile(download, offer.sha256, offer.size, error)) return false;
    if (WindowsExecutableVersion(download) != offer.version ||
        QVersionNumber::fromString(offer.version) <= QVersionNumber::fromString(WindowsExecutableVersion(QCoreApplication::applicationFilePath()))) {
        error = "The executable version is not the announced newer release."; return false;
    }
    const auto target = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const auto directory = QFileInfo(target).absolutePath();
    QTemporaryFile writable(directory + "/.lginputswitch-write-XXXXXX");
    if (!writable.open()) { error = "This folder is not writable. Move the portable app to a folder you own, then try again."; return false; }
    writable.close();
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto prefix = directory + "/.lginputswitch-update-" + token;
    const auto staged = prefix + "-new.exe", helper = prefix + "-helper.exe", plan = prefix + ".json";
    if (!QFile::copy(download, staged) || !QFile::copy(target, helper)) {
        QFile::remove(staged); QFile::remove(helper); error = "Could not stage the update in the app folder."; return false;
    }
    const auto oldHash = UpdateFileHash(target);
    const auto creation = Creation(GetCurrentProcess());
    QJsonObject value{{"format", 1}, {"target", target}, {"staged", staged}, {"helper", helper},
        {"pid", QString::number(QCoreApplication::applicationPid())}, {"creation", creation},
        {"version", offer.version}, {"sha256", offer.sha256}, {"size", QString::number(offer.size)}, {"old_sha256", oldHash}};
    QSaveFile file(plan);
    if (oldHash.isEmpty() || creation.isEmpty() || !file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(value).toJson()) < 0 || !file.commit() ||
        !QProcess::startDetached(helper, {"--apply-update", plan}, directory)) {
        QFile::remove(staged); QFile::remove(helper); QFile::remove(plan);
        error = "Could not start the update helper. The installed app is unchanged."; return false;
    }
    RememberCleanup(prefix);
    // Do not close the app until the owned helper has validated its plan.
    for (int i = 0; i < 250; ++i) {
        if (QFileInfo::exists(plan + ".started")) return true;
        QThread::msleep(20);
    }
    QFile::remove(plan); // A late helper must not install after reporting failure.
    error = "The update helper did not become ready. The running app was kept open.";
    return false;
}
int ApplyWindowsUpdate(const QString &planPath) {
    const auto helper = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const auto parent = QFileInfo(helper).absolutePath();
    const auto base = QFileInfo(helper).fileName();
    if (!QRegularExpression("^\\.lginputswitch-update-[0-9a-f-]{36}-helper\\.exe$").match(base).hasMatch()) return 2;
    const auto prefix = helper.left(helper.size() - QString("-helper.exe").size());
    if (QFileInfo(planPath).absoluteFilePath() != prefix + ".json") return 2;
    QFile file(planPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return 2;
    const auto plan = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    const auto target = plan["target"].toString(), staged = plan["staged"].toString();
    if (plan["format"].toInt() != 1 || plan["helper"] != helper || staged != prefix + "-new.exe" ||
        QFileInfo(target).absolutePath() != parent || !target.endsWith(".exe", Qt::CaseInsensitive) ||
        QFileInfo(target).fileName().startsWith('.') || target == helper || QFileInfo(target).isSymLink() ||
        !ValidUpdateVersion(plan["version"].toString())) return 2;
    RememberCleanup(prefix);
    QLockFile lock(parent + "/.lginputswitch-update.lock");
    if (!lock.tryLock()) return 3;
    bool pidOk = false; const auto pid = plan["pid"].toString().toUInt(&pidOk);
    if (!pidOk || !pid || pid == GetCurrentProcessId()) return 2;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        wchar_t image[32768]{}; DWORD count = 32768;
        const bool same = QueryFullProcessImageNameW(process, 0, image, &count) &&
            QFileInfo(QString::fromWCharArray(image, count)).canonicalFilePath() == target &&
            Creation(process) == plan["creation"].toString();
        QSaveFile ready(planPath + ".started");
        const bool signalled = same && ready.open(QIODevice::WriteOnly) && ready.write("started") == 7 && ready.commit();
        const auto waited = signalled ? WaitForSingleObject(process, 60000) : WAIT_FAILED;
        CloseHandle(process);
        if (!same || waited != WAIT_OBJECT_0) return 3;
    } else return 3;
    if (!QFileInfo::exists(planPath)) return 3;
    QString error;
    const auto expectedSize = plan["size"].toString().toLongLong();
    if (UpdateFileHash(helper) != plan["old_sha256"].toString() ||
        UpdateFileHash(target) != plan["old_sha256"].toString() ||
        !VerifyUpdateFile(staged, plan["sha256"].toString(), expectedSize, error) ||
        WindowsExecutableVersion(staged) != plan["version"].toString()) {
        Result("Update verification failed. The previous app was kept."); Restart(target); return 4;
    }
    const auto backup = target + ".previous";
    if (!Move(target, backup, true)) {
        Result("Update could not replace the executable. " + WinError()); Restart(target); return 5;
    }
    if (!Move(staged, target, false)) {
        const bool restored = Move(backup, target, false);
        Result(restored ? "Update failed; the previous executable was restored."
                        : "Update failed. Restore the .previous executable in the app folder.");
        if (restored) Restart(target);
        else MessageBoxW(nullptr, L"Restore the .previous executable in the app folder.", L"LG Input Switch", MB_OK | MB_ICONERROR);
        return 6;
    }
    qint64 newPid = 0;
    const bool launched = QProcess::startDetached(target, {"--update-ready", planPath}, parent, &newPid);
    HANDLE restarted = launched ? OpenProcess(SYNCHRONIZE, FALSE, DWORD(newPid)) : nullptr;
    bool healthy = false, exited = !launched;
    for (int i = 0; launched && i < 200; ++i) {
        if (QFileInfo::exists(planPath + ".ready")) { healthy = true; break; }
        if (restarted && WaitForSingleObject(restarted, 0) == WAIT_OBJECT_0) { exited = true; break; }
        QThread::msleep(50);
    }
    if (restarted) CloseHandle(restarted);
    if (!healthy && exited) {
        QFile::remove(target);
        const bool restored = Move(backup, target, false);
        Result(restored ? "The new app did not start; the previous executable was restored."
                        : "The new app did not start. Restore the .previous executable in the app folder.");
        if (restored) Restart(target);
        else MessageBoxW(nullptr, L"Update recovery needs attention. Restore the .previous executable in the app folder.", L"LG Input Switch", MB_OK | MB_ICONERROR);
        return 7;
    }
    Result("Updated to " + plan["version"].toString() +
           (healthy ? ". The previous executable is retained as .previous."
                    : ". Startup confirmation timed out; the .previous executable is available for recovery."));
    return 0;
}
#else
QString WindowsExecutableVersion(const QString &) { return {}; }
bool StartWindowsUpdate(const UpdateOffer &, const QString &, QString &error) { error = "Windows updates are unavailable on this platform."; return false; }
int ApplyWindowsUpdate(const QString &) { return 2; }
void CleanupWindowsUpdateFiles() {}
#endif
