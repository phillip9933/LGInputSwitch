#define LG_DESKTOP_TEST
#include "../desktop/main.cpp"
#include <iostream>

static void check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
static bool waitUntil(const std::function<bool()> &predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents();
        if (predicate())
            return true;
        QThread::msleep(5);
    }
    return false;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitchTests");
    app.setApplicationName("Desktop");
    app.setStyle("Fusion");
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary directory unavailable");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, temporary.path());
#ifndef Q_OS_WIN
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    const auto startupPath = temporary.path() + "/autostart/lginputswitch.desktop";
    const auto executable = temporary.path() + "/LG Input ü \" $`%\\";
    QString startupError;
    check(SetDesktopAutostart(false, startupError, startupPath, executable), "Startup opt-out failed");
    check(!QFile::exists(startupPath), "Startup created a file before opt-in");
    check(SetDesktopAutostart(true, startupError, startupPath, executable), "Startup opt-in failed");
    check(DesktopAutostartEnabled(startupPath), "Startup state not read back");
    QFile startupFile(startupPath);
    check(startupFile.open(QIODevice::ReadOnly), "Startup entry not readable");
    const auto startupContents = startupFile.readAll();
    check(startupContents.contains(" --background\n"), "Background startup argument missing");
    check(startupContents.contains("%%"), "Literal percent was not escaped");
    check(startupContents.contains("\\\\$"), "Exec dollar quoting lost a layer");
    startupFile.close();
    check(!SetDesktopAutostart(true, startupError, startupPath, "/invalid\npath"),
          "Newline injection accepted in startup entry");
    check(DesktopAutostartEnabled(startupPath), "Rejected update damaged startup entry");
    check(SetDesktopAutostart(false, startupError, startupPath, executable), "Startup disable failed");
    check(!DesktopAutostartEnabled(startupPath), "Startup disable not reflected");
    QFile blocker(temporary.path() + "/not-a-directory");
    check(blocker.open(QIODevice::WriteOnly), "Could not create failure fixture");
    blocker.close();
    check(!SetDesktopAutostart(true, startupError, blocker.fileName() + "/entry", executable),
          "Unwritable startup path was accepted");
#endif
    {
        DesktopWindow preview(true);
        preview.show();
        app.processEvents();
        auto *appearance = preview.findChild<QComboBox *>("appearance");
        auto *table = preview.findChild<QTableWidget *>("inputs");
        check(appearance && table && table->rowCount() == 4, "Inputs or appearance controls missing");
        appearance->setCurrentIndex(appearance->findData("dark"));
        app.processEvents();
        check(app.palette().color(QPalette::Window).lightness() < 60, "Dark palette not applied");
        appearance->setCurrentIndex(appearance->findData("light"));
        app.processEvents();
        check(app.palette().color(QPalette::Window).lightness() > 220, "Light palette not applied");
        check(table->isColumnHidden(2), "Advanced codes unexpectedly exposed by default");
        check(table->visualItemRect(table->item(3, 1)).bottom() <= table->viewport()->height(),
              "Last input is clipped");
        auto *diagnosticRun = preview.findChild<QPushButton *>("diagnosticRun");
        auto *diagnosticReport = preview.findChild<QPlainTextEdit *>("diagnosticReport");
        check(diagnosticRun && diagnosticReport, "Diagnostics controls missing");
        diagnosticRun->click();
        check(waitUntil([&] { return diagnosticRun->isEnabled() && diagnosticReport->toPlainText().contains("report_format"); }),
              "Preview diagnostics did not finish");
        check(diagnosticReport->toPlainText().contains("no settings or hardware accessed"),
              "Preview diagnostics accessed live hardware");
        auto *status = preview.findChild<QLabel *>("status");
        for (auto *button : preview.findChildren<QPushButton *>())
            if (button->text() == "Switch")
            {
                button->click();
                break;
            }
        check(status->text().contains("preview", Qt::CaseInsensitive), "Preview attempted a live operation");
        preview.close();
    }
#ifndef Q_OS_WIN
    // A fake process provides discovery and records writes. No I2C hardware is used.
    const auto log = temporary.path() + "/commands";
    QFile fake(temporary.path() + "/ddcutil");
    check(fake.open(QIODevice::WriteOnly), "Could not create fake backend");
    fake.write(("#!/bin/sh\nif [ \"$1\" = detect ]; then\nprintf 'Invalid display\\n   I2C bus: "
                "/dev/i2c-4\\n   Monitor: SDC:Internal panel:REDACTED\\n"
                "Display 1\\n   I2C bus: /dev/i2c-29\\n   Model: Synthetic Dell\\n"
                "Display 2\\n   I2C bus: /dev/i2c-31\\n   Monitor: GSM:Synthetic "
                "LG:REDACTED\\n'\nelse\nprintf '%s\\n' \"$@\" > '" +
                log + "'\nfi\n")
                   .toUtf8());
    fake.close();
    fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("PATH", temporary.path().toUtf8());
    {
        DesktopWindow live(false);
        live.show();
        auto *monitors = live.findChild<QComboBox *>("monitors");
        check(waitUntil([&] { return monitors->count() == 2 && monitors->isEnabled(); }),
              "Asynchronous discovery did not finish");
        check(monitors->currentIndex() == -1, "Discovery silently selected an unconfigured target");
        check(monitors->itemText(1) == "Synthetic LG · I²C 31", "Brief monitor model/route not displayed");
        monitors->setCurrentIndex(1);
        live.findChild<QSpinBox *>()->setValue(0);
        auto *table = live.findChild<QTableWidget *>("inputs");
        table->item(0, 2)->setText("0xD0");
        for (auto *button : live.findChildren<QPushButton *>())
            if (button->accessibleName() == "Save settings")
            {
                button->click();
                break;
            }
        check(live.findChild<QLabel *>("status")->text().contains("saved", Qt::CaseInsensitive),
              "Settings did not save");
        for (auto *button : live.findChildren<QPushButton *>())
            if (button->text() == "Switch")
            {
                button->click();
                break;
            }
        check(waitUntil([&] { return QFile::exists(log) && table->isEnabled(); }),
              "Asynchronous input send did not finish");
        QFile record(log);
        check(record.open(QIODevice::ReadOnly), "No backend command recorded");
        check(record.readAll() == "--bus\n31\nsetvcp\n0xf4\n0xd0\n--i2c-source-addr=0x50\n--noverify\n",
              "GUI sent wrong target or command");
        record.close();
        QFile diagnosticsBackend(temporary.path() + "/ddcutil");
        check(diagnosticsBackend.open(QIODevice::WriteOnly | QIODevice::Truncate), "Could not replace diagnostic fixture");
        diagnosticsBackend.write(("#!/bin/sh\nprintf '%s\\n' \"$*\" >> '" + log + ".reads'\n"
            "case \"$*\" in\n--version) echo 'ddcutil 2.2.0';;\n"
            "'--bus 31 getvcp 10 --terse') printf 'VCP 10 C 55 100\\nSerial number: PRIVATE\\n';;\n"
            "'--bus 31 getvcp 60 --terse') echo 'VCP 60 SNC x0f';;\n*) exit 92;;\nesac\n").toUtf8());
        diagnosticsBackend.close();
        auto *runReport = live.findChild<QPushButton *>("diagnosticRun");
        auto *report = live.findChild<QPlainTextEdit *>("diagnosticReport");
        runReport->click();
        check(waitUntil([&] { return runReport->isEnabled() && report->toPlainText().contains("standard_vcp_reads"); }),
              "Live diagnostic report failed");
        const auto reportText = report->toPlainText();
        check(!reportText.contains("PRIVATE") && reportText.contains("last_command") && reportText.contains("0x00d0"),
              "Report leaked raw output or lost command context");
        QFile reads(log + ".reads");
        check(reads.open(QIODevice::ReadOnly), "Diagnostic commands not recorded");
        check(reads.readAll() == "--version\n--bus 31 getvcp 10 --terse\n--bus 31 getvcp 60 --terse\n",
              "Diagnostics issued an unexpected command");
        QFile baseline(log);
        check(baseline.open(QIODevice::ReadOnly), "Switch record unavailable");
        check(baseline.readAll().contains("setvcp\n0xf4\n0xd0"), "Diagnostics changed the last switch");
        // Restore the switch fixture for subsequent tests.
        check(diagnosticsBackend.open(QIODevice::WriteOnly | QIODevice::Truncate), "Could not restore backend fixture");
        diagnosticsBackend.write(("#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + log + "'\n").toUtf8());
        diagnosticsBackend.close();
        auto switches = live.findChildren<QPushButton *>();
        int seen = 0;
        for (auto *button : switches)
            if (button->text() == "Switch" && ++seen == 2)
            {
                button->click();
                break;
            }
        check(waitUntil([&] { return table->isEnabled(); }), "Second selection did not complete");
        QAction *previous = nullptr;
        for (auto *action : live.findChildren<QAction *>())
            if (action->text() == "Previous input")
                previous = action;
        check(previous && previous->isEnabled(), "Previous input was not made available");
        previous->trigger();
        check(waitUntil([&] { return table->isEnabled(); }), "Previous selection did not complete");
        check(record.open(QIODevice::ReadOnly), "Previous input command not recorded");
        check(record.readAll().contains("0xd0"), "Previous input did not select prior accepted input");
        record.close();
        QString warning;
        auto saved = LoadDesktopConfig(warning);
        check(saved.display == 31 && saved.bus && saved.inputs[0].code == "0xD0",
              "Config roundtrip changed selected target/custom code");
        saved.globalShortcuts = true;
        saved.cycleShortcut = "META+SHIFT+F8";
        saved.previousShortcut = "META+SHIFT+F10";
        saved.inputs[0].shortcut = "META+SHIFT+F9";
        QString error;
        check(SaveDesktopConfig(saved, error), "Shortcut preferences failed to save");
        const auto shortcutRoundtrip = LoadDesktopConfig(warning);
        check(shortcutRoundtrip.globalShortcuts && shortcutRoundtrip.cycleShortcut == "META+SHIFT+F8" &&
                  shortcutRoundtrip.previousShortcut == "META+SHIFT+F10" &&
                  shortcutRoundtrip.inputs[0].shortcut == "META+SHIFT+F9",
              "Linux shortcut preferences were lost");
        QTimer::singleShot(0, &live, [&live] { live.close(); });
        QTimer::singleShot(3000, &app, [&app] { app.exit(91); });
        check(app.exec() == 0, "Closing without a tray failed to quit");
    }
#endif
    std::cout << "Desktop theme, preview, layout and platform smoke checks passed\n";
}
