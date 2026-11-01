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
    if (WindowsExecutableVersion(download) != offer.version) { error = "The executable version does not match this release."; return false; }
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
    return true;
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
        const auto waited = same ? WaitForSingleObject(process, 60000) : WAIT_FAILED;
        CloseHandle(process);
        if (!same || waited != WAIT_OBJECT_0) return 3;
    } else if (GetLastError() != ERROR_INVALID_PARAMETER) return 3;
    QString error;
    const auto expectedSize = plan["size"].toString().toLongLong();
    if (UpdateFileHash(target) != plan["old_sha256"].toString() ||
        !VerifyUpdateFile(staged, plan["sha256"].toString(), expectedSize, error) ||
        WindowsExecutableVersion(staged) != plan["version"].toString()) {
        Result("Update verification failed. The previous app was kept."); Restart(target); return 4;
    }
    const auto backup = target + ".previous";
    if (!Move(target, backup, true)) {
        Result("Update could not replace the executable. " + WinError()); Restart(target); return 5;
    }
    if (!Move(staged, target, false)) {
        Move(backup, target, false); Result("Update failed; the previous executable was restored."); Restart(target); return 6;
    }
    if (!Restart(target)) {
        QFile::remove(target); Move(backup, target, false);
        Result("The new executable could not start; the previous executable was restored."); Restart(target); return 7;
    }
    Result("Updated to " + plan["version"].toString() + ". The previous executable is retained as .previous.");
    QFile::remove(planPath);
    // Windows removes the old helper after its process has exited; schedule it.
    MoveFileExW(reinterpret_cast<LPCWSTR>(helper.utf16()), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}
#else
QString WindowsExecutableVersion(const QString &) { return {}; }
bool StartWindowsUpdate(const UpdateOffer &, const QString &, QString &error) { error = "Windows updates are unavailable on this platform."; return false; }
int ApplyWindowsUpdate(const QString &) { return 2; }
#endif
