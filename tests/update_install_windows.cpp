#include "../desktop/update_install.h"
#include <iostream>

static void check(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
static bool waitFor(const QString &path) {
    for (int i = 0; i < 200; ++i) {
        if (QFileInfo::exists(path)) return true;
        QThread::msleep(50);
    }
    return false;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--apply-update")
        return ApplyWindowsUpdate(QString::fromLocal8Bit(argv[2]));
    if (argc == 5 && QString::fromLocal8Bit(argv[1]) == "--simulate-update") {
        UpdateOffer offer;
        const auto candidate = QString::fromLocal8Bit(argv[2]);
        offer.version = "99.0.0"; offer.sha256 = QString::fromLocal8Bit(argv[3]); offer.size = QFileInfo(candidate).size();
        QString error;
        const bool started = StartWindowsUpdate(offer, candidate, error);
        if (started && QString::fromLocal8Bit(argv[4]) == "hold") QThread::msleep(1800);
        return started ? 0 : 2;
    }
    if (QFileInfo(QCoreApplication::applicationFilePath()).fileName() == "portable-current.exe") {
        QFile file("old-started.marker"); file.open(QIODevice::WriteOnly); file.write("restarted"); return 0;
    }
    check(argc == 2, "Candidate argument missing");
    const auto candidate = QString::fromLocal8Bit(argv[1]);
    check(WindowsExecutableVersion(candidate) == "99.0.0", "Candidate version could not be read");
    for (bool tamper : {false, true}) {
        QTemporaryDir temp;
        const auto directory = temp.filePath("portable app"); check(QDir().mkpath(directory), "Fixture directory unavailable");
        const auto target = directory + "/portable-current.exe";
        check(QFile::copy(QCoreApplication::applicationFilePath(), target), "Fixture executable unavailable");
        const auto oldHash = UpdateFileHash(target), newHash = UpdateFileHash(candidate);
        const auto config = directory + "/desktop-profiles.json";
        QFile settings(config); check(settings.open(QIODevice::WriteOnly), "Fixture config unavailable"); settings.write("settings must survive"); settings.close();
        QProcess current;
        current.setWorkingDirectory(directory);
        current.start(target, {"--simulate-update", candidate, newHash, tamper ? "hold" : "exit"});
        if (tamper) {
            QString staged;
            for (int tries = 0; tries < 60 && staged.isEmpty(); ++tries) {
                const auto files = QDir(directory).entryList({".lginputswitch-update-*-new.exe"}, QDir::Files | QDir::Hidden);
                if (!files.isEmpty()) staged = directory + '/' + files.front();
                else QThread::msleep(20);
            }
            check(!staged.isEmpty(), "Staged candidate was not created");
            QFile file(staged); check(file.open(QIODevice::Append), "Could not tamper with synthetic candidate"); file.write("tampered"); file.close();
        }
        check(current.waitForFinished(5000) && current.exitCode() == 0, "Update helper did not start");
        check(waitFor(directory + (tamper ? "/old-started.marker" : "/update-installed.marker")), "Updated or retained app did not restart");
        check(UpdateFileHash(target) == (tamper ? oldHash : newHash), "Update transaction installed the wrong bytes");
        if (!tamper) check(UpdateFileHash(target + ".previous") == oldHash, "Previous executable was not kept");
        check(settings.open(QIODevice::ReadOnly) && settings.readAll() == "settings must survive", "Update touched configuration"); settings.close();
        QThread::msleep(250);
    }
    std::cout << "Native self-replacement, restart, backup, settings preservation and staged-tamper rejection passed\n";
}
