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

struct DesktopWindowProbe
{
    static void compactLayout(DesktopWindow &window)
    {
        const auto originalSize = window.size();
        const int originalPage = window.tabs_->currentIndex();
        const bool originalEditing = window.editProfile_->isChecked();
        for (const auto &requested : {QSize(640, 480), QSize(800, 600)})
            for (bool editing : {false, true})
            {
                window.editProfile_->setChecked(editing);
                window.resize(requested);
                QCoreApplication::processEvents();
                check(window.size() == requested, "Window refuses a laptop-sized viewport");
                for (int page = 0; page < window.tabs_->count(); ++page)
                {
                    window.tabs_->setCurrentIndex(page);
                    QCoreApplication::processEvents();
                    const auto save = QRect(window.save_->mapTo(&window, QPoint()), window.save_->size());
                    const auto controls =
                        QRect(window.controls_->mapTo(&window, QPoint()), window.controls_->size());
                    const auto status =
                        QRect(window.status_->mapTo(&window, QPoint()), window.status_->size());
                    check(window.rect().contains(save) && controls.bottom() < status.top() &&
                              status.bottom() < save.top(),
                          "Page/status/footer overlap at compact size");
                    check(window.monitorCard_->isVisibleTo(&window) == (page == 0),
                          "Profile monitor controls remain on a global settings page");
                    if (page == 0 && editing)
                    {
                        check(window.profileScroll_->verticalScrollBar()->maximum() > 0,
                              "Compact profile editing compresses content instead of scrolling");
                        window.profileScroll_->verticalScrollBar()->setValue(
                            window.profileScroll_->verticalScrollBar()->maximum());
                        QCoreApplication::processEvents();
                        check(window.save_->isVisible(), "Scrolling hides global Save settings");
                    }
                }
            }
        window.tabs_->setCurrentIndex(originalPage);
        window.editProfile_->setChecked(originalEditing);
        window.resize(originalSize);
        QCoreApplication::processEvents();
    }
    static void run(DesktopWindow &window, const QString &temp, const QString &log, const QString &failMarker,
                    const QString &missingMarker)
    {
        check(waitUntil([&] { return !window.busy_ && window.discovery_.monitors.size() == 2; }),
              "Async discovery failed");
        check(window.draft_.profiles.size() == 1 && window.draft_.profiles[0].actions.size() == 2,
              "Legacy Linux migration lost inputs");
        check(window.members_->findChildren<QComboBox *>().isEmpty() &&
                  window.members_->item(0, 1)->text() == "Connection needed",
              "Monitor row still offers an ambiguous selector or silently chose a monitor");
        window.selectConnection("monitor-1", 0);
        window.save();
        check(!window.dirty_, "Initial profile did not save");
        const auto firstProfile = window.saved_.profiles[0].id;
        const auto firstAction = window.saved_.profiles[0].actions[0].id;
        const auto secondAction = window.saved_.profiles[0].actions[1].id;
        auto side = window.draft_.monitors[0];
        side.id = "synthetic-side";
        side.name = "Side";
        side.route = window.discovery_.monitors[1];
        side.identity = side.route.identity;
        side.source = "0x50";
        side.method = "standard";
        side.inputs[0].code = "0x0f";
        side.inputs[1].code = "0x11";
        window.draft_.monitors.push_back(side);
        MonitorProfile group;
        group.id = "group";
        group.name = "Both monitors";
        group.monitors = {"monitor-1", side.id};
        group.actions = {
            {"linux", "Linux PC", {}, true, {{"monitor-1", side.inputs[0].id}, {side.id, side.inputs[1].id}}},
            {"windows",
             "Windows PC",
             {},
             true,
             {{"monitor-1", side.inputs[1].id}, {side.id, side.inputs[0].id}}}};
        window.captureDraft();
        window.draft_.profiles.push_back(group);
        window.rebuildTabs();
        window.dirty();
        window.save();
        check(!window.dirty_, "Group configuration failed to save");
        QTimer::singleShot(0, &window, [&window] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog != nullptr, "Reconnect dialog missing");
            auto *choice = dialog->findChild<QComboBox *>("reconnectChoice");
            check(choice && choice->count() == 1 && choice->itemData(0).toInt() == 0,
                  "Reconnect offers a connection assigned to another saved monitor");
            dialog->accept();
        });
        window.reconnectMonitor("monitor-1");
        window.save();
        // Dispatch a group while the single-monitor tab remains visible.
        window.sendInput("group", "linux");
        check(waitUntil([&] { return !window.busy_; }), "Grouped async send did not finish");
        QFile record(log);
        check(record.open(QIODevice::ReadOnly), "Group sends not recorded");
        const auto bytes = record.readAll();
        record.close();
        check(bytes == "--bus 29 setvcp 0xf4 0xd0 --i2c-source-addr=0x50 --noverify\n"
                       "--bus 31 setvcp 0x60 0x11 --noverify\n",
              "Wrong group targets or distinct input/source values");
        window.profileTabs_->setCurrentIndex(1);
        QCoreApplication::processEvents();
        check(window.activeId_ == "group", "Could not select saved group tab");
        check(window.results_->toPlainText().contains("Side") &&
                  window.results_->toPlainText().contains("Command sent"),
              "Per-monitor results missing");
        QFile marker(failMarker);
        check(marker.open(QIODevice::WriteOnly), "Could not create failure marker");
        marker.close();
        window.sendInput("group", "windows");
        check(waitUntil([&] { return !window.busy_; }), "Partial group send did not finish");
        check(window.retry_->isEnabled() && window.results_->toPlainText().contains("87"),
              "Partial result or retry missing");
        check(window.session_.previous("group") == "linux", "Partial group lost previous complete action");
        check(QFile::remove(failMarker), "Could not clear failure marker");
        check(record.open(QIODevice::ReadOnly), "Command log unavailable");
        const auto beforeRetry = record.readAll();
        record.close();
        window.retry_->click();
        check(waitUntil([&] { return !window.busy_; }), "Retry did not finish");
        check(record.open(QIODevice::ReadOnly), "Retry not recorded");
        const auto afterRetry = record.readAll();
        record.close();
        check(afterRetry.mid(beforeRetry.size()) == "--bus 31 setvcp 0x60 0xf --noverify\n" &&
                  !window.retry_->isEnabled(),
              "Retry resent successful monitor or remained active");
        window.diagnostics();
        check(waitUntil([&] { return !window.busy_; }), "Group diagnostics did not finish");
        const auto report = window.diagnosticReport_->toPlainText();
        check(report.contains("profile_monitors") && report.contains("advertised_vcp_codes") &&
                  report.contains("HDMI 1") && !report.contains("PRIVATE") && !report.contains("Linux PC") &&
                  !report.contains(side.identity),
              "Group report lost information or leaked identity/labels");
        const auto doc = QJsonDocument::fromJson(report.toUtf8()).object();
        check(doc["checks"].toObject()["monitors"].toArray().size() == 2 &&
                  doc["profile_monitors"].toArray().size() == 2,
              "Diagnostics skipped a group member");
        window.sendInput(firstProfile, firstAction);
        check(waitUntil([&] { return !window.busy_; }), "Other profile send failed");
        check(window.session_.state("group").currentAction.isEmpty() && !window.retry_->isEnabled(),
              "Other tab retained stale shared history");
        window.syncShortcut("group", "linux", QKeySequence("Ctrl+Alt+F10"));
        const int shortcutColumn = 2 + window.active().monitors.size();
        check(window.table_->item(0, shortcutColumn)->data(Qt::UserRole).toString() == "CTRL+ALT+F10",
              "Read-only shortcut display did not synchronize");
        window.captureDraft();
        check(window.active().actions[0].shortcut == "CTRL+ALT+F10",
              "View collection overwrote the external shortcut");
        // External Plasma changes must persist to the right profile without
        // overwriting a dirty form on any tab.
        window.table_->item(0, 1)->setText("Draft group name");
        window.syncShortcut(firstProfile, firstAction, QKeySequence("Ctrl+Alt+F9"));
        check(window.table_->item(0, 1)->text() == "Draft group name" && window.dirty_,
              "External shortcut discarded draft edits");
        QString warning;
        const auto persisted = LoadDesktopWorkspace(warning);
        check(persisted.profiles[0].actions[0].shortcut == "CTRL+ALT+F9" &&
                  persisted.profiles[1].actions[0].name == "Linux PC",
              "External shortcut was saved to wrong profile or saved unrelated draft");
        window.save();
        check(!window.dirty_, "Draft did not save");
        QTimer::singleShot(0, &window, [&window] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog != nullptr, "Monitor editor did not open");
            auto *method = dialog->findChild<QComboBox *>("monitorMethod");
            check(method && method->currentData() == "standard",
                  "Monitor method did not roundtrip to editor");
            auto *read = dialog->findChild<QPushButton *>("readMonitorInputs");
            read->click();
            check(waitUntil([&] { return read->isEnabled(); }), "Advertised-input import did not finish");
            auto *inputs = dialog->findChild<QTableWidget *>("monitorInputs");
            check(inputs->rowCount() == 3,
                  "Advertised input import removed configured inputs or did not append new value");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        window.editMonitor("synthetic-side");
        check(window.dirty_ && window.draft_.monitors[1].inputs.size() == 3,
              "Monitor editor did not retain advertised inputs");
        window.save();
        QFile offline(missingMarker);
        check(offline.open(QIODevice::WriteOnly), "Could not create disconnect fixture");
        offline.close();
        window.sendInput("group", "linux");
        check(waitUntil([&] { return !window.busy_; }), "Offline group did not finish");
        check(window.retry_->isEnabled() && window.results_->toPlainText().contains("Disconnected"),
              "Missing monitor did not produce a retryable result");
        // Existing profiles migrate only on deliberate save, and reloading uses
        // version 2 rather than stale legacy preferences.
        const auto reloaded = LoadDesktopWorkspace(warning);
        check(reloaded.profiles.size() == 2 && reloaded.monitors.size() == 2 &&
                  reloaded.monitors[1].method == "standard",
              "Saved workspace lost shared monitor settings");
        compactLayout(window);
        Q_UNUSED(temp);
        Q_UNUSED(secondAction);
    }
};

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
        auto *profiles = preview.findChild<QTabBar *>("profileTabs");
        check(appearance && table && table->rowCount() == 4 && profiles && profiles->count() == 3,
              "Inputs or profile tabs missing");
        appearance->setCurrentIndex(appearance->findData("dark"));
        app.processEvents();
        check(app.palette().color(QPalette::Window).lightness() < 60, "Dark palette not applied");
        appearance->setCurrentIndex(appearance->findData("light"));
        app.processEvents();
        check(app.palette().color(QPalette::Window).lightness() > 220, "Light palette not applied");
        check(!table->cellWidget(0, 2) && table->item(0, 2)->text() == "DisplayPort" &&
                  table->isColumnHidden(0),
              "Normal switching view still exposes input editors");
        preview.findChild<QPushButton *>("editProfile")->setChecked(true);
        auto *input = qobject_cast<QComboBox *>(table->cellWidget(0, 2));
        check(input && !input->currentText().contains("0x"), "Codes unexpectedly exposed by default");
        check(table->visualItemRect(table->item(3, 1)).bottom() <= table->viewport()->height(),
              "Last input is clipped");
        table->item(0, 1)->setText("Unsaved label");
        profiles->setCurrentIndex(2);
        app.processEvents();
        check(table->rowCount() == 2 && table->columnCount() == 6 && !table->isColumnHidden(3),
              "Group lacks per-monitor mappings");
        auto *first = qobject_cast<QComboBox *>(table->cellWidget(0, 2));
        auto *second = qobject_cast<QComboBox *>(table->cellWidget(0, 3));
        check(first->currentText() == "DisplayPort" && second->currentText() == "HDMI1",
              "Group mappings not independent");
        table->item(0, 1)->setText("Unsaved group label");
        profiles->setCurrentIndex(0);
        app.processEvents();
        check(table->item(0, 1)->text() == "Unsaved label", "Tab switch discarded single-monitor draft");
        profiles->setCurrentIndex(2);
        app.processEvents();
        check(table->item(0, 1)->text() == "Unsaved group label", "Tab switch discarded group draft");
        auto *navigation = preview.findChild<QTabWidget *>("mainTabs");
        check(navigation->tabText(0) == "Profiles", "Global navigation is not above the profile page");
        navigation->setCurrentIndex(3);
        auto *diagnosticProfile = preview.findChild<QComboBox *>("diagnosticProfile");
        diagnosticProfile->setCurrentIndex(0);
        app.processEvents();
        check(profiles->currentIndex() == 0 && table->item(0, 1)->text() == "Unsaved label",
              "Diagnostics profile selector lost draft edits or changed the wrong target");
        diagnosticProfile->setCurrentIndex(2);
        app.processEvents();
        auto *diagnosticRun = preview.findChild<QPushButton *>("diagnosticRun");
        auto *report = preview.findChild<QPlainTextEdit *>("diagnosticReport");
        diagnosticRun->click();
        check(waitUntil([&] {
                  return diagnosticRun->isEnabled() && report->toPlainText().contains("profile_monitors");
              }),
              "Group preview diagnostics failed");
        check(report->toPlainText().contains("no settings or hardware accessed") &&
                  !report->toPlainText().contains("Unsaved"),
              "Preview report used hardware or leaked labels");
        navigation->setCurrentIndex(0);
        auto *status = preview.findChild<QLabel *>("status");
        qobject_cast<QPushButton *>(table->cellWidget(0, table->columnCount() - 1))->click();
        check(status->text().contains("preview", Qt::CaseInsensitive), "Preview attempted a live operation");
        QTimer::singleShot(0, &preview, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            check(dialog != nullptr, "Add profile dialog missing");
            dialog->setTextValue("New profile");
            dialog->accept();
        });
        preview.findChild<QPushButton *>("addProfile")->click();
        check(profiles->count() == 4 && table->rowCount() == 4, "Add profile did not create independent tab");
        QTimer::singleShot(0, &preview, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog != nullptr, "Add monitor dialog missing");
            dialog->accept();
        });
        preview.findChild<QPushButton *>("addMonitor")->click();
        check(table->columnCount() == 6 &&
                  qobject_cast<QComboBox *>(table->cellWidget(0, 3))->currentIndex() == -1,
              "Add monitor guessed a group input instead of requesting selection");
        DesktopWindowProbe::compactLayout(preview);
        preview.close();
    }
#ifndef Q_OS_WIN
    // A fake process provides discovery, records every write and can fail one
    // monitor. Nothing in this test addresses real I2C hardware or native keys.
    const auto log = temporary.path() + "/commands";
    const auto failMarker = temporary.path() + "/fail-side";
    const auto missingMarker = temporary.path() + "/missing-side";
    QFile fake(temporary.path() + "/ddcutil");
    check(fake.open(QIODevice::WriteOnly), "Could not create fake backend");
    fake.write(("#!/bin/sh\n"
                "if [ \"$1\" = detect ]; then\n"
                "printf 'Invalid display\\n I2C bus: /dev/i2c-4\\n Monitor: LCD:Internal:PRIVATE\\n"
                "Display 1\\n I2C bus: /dev/i2c-29\\n Monitor: GSM:Synthetic main:MAINPRIVATE\\n'\n"
                "if [ ! -f '" +
                missingMarker +
                "' ]; then printf 'Display 2\\n I2C bus: /dev/i2c-31\\n Monitor: GSM:Synthetic "
                "side:SIDEPRIVATE\\n'; fi\n"
                "elif [ \"$1\" = --version ]; then echo 'ddcutil 2.2.0'\n"
                "else\ncase \"$*\" in\n"
                "*setvcp*) printf '%s\\n' \"$*\" >> '" +
                log + "'; if [ \"$2\" = 31 ] && [ -f '" + failMarker +
                "' ]; then exit 87; fi;;\n"
                "*'getvcp 10 --terse') echo 'VCP 10 C 55 100';;\n"
                "*'getvcp 60 --terse') echo 'VCP 60 SNC x0f';;\n"
                "*'capabilities --terse') echo '(model(PRIVATE)vcp(10 60(0F 11 12) F4)mccs_ver(2.2))';;\n"
                "*) exit 92;;\nesac\nfi\n")
                   .toUtf8());
    fake.close();
    fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("PATH", temporary.path().toUtf8());
    // Preserve legacy Linux input order, disabled rows and shortcuts on migration.
    QSettings legacy;
    legacy.setValue(
        "inputs",
        QJsonDocument(
            QJsonArray{
                QJsonObject{{"label", "DisplayPort"}, {"code", "0xd0"}, {"shortcut", ""}, {"enabled", true}},
                QJsonObject{{"label", "HDMI"}, {"code", "0x90"}, {"shortcut", ""}, {"enabled", true}}})
            .toJson());
    legacy.setValue("cycleShortcut", "");
    legacy.setValue("debounce", 0);
    legacy.sync();
    {
        DesktopWindow live(false);
        live.show();
        DesktopWindowProbe::run(live, temporary.path(), log, failMarker, missingMarker);
        QTimer::singleShot(0, &live, [&live] { live.close(); });
        QTimer::singleShot(3000, &app, [&app] { app.exit(91); });
        check(app.exec() == 0, "Closing without a tray failed to quit");
    }
#endif
    std::cout << "Desktop themes, profile drafts, grouped commands, retries and diagnostics checks passed\n";
}
