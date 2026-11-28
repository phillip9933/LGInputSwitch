#define LG_DESKTOP_TEST
#include "../desktop/main.cpp"
#include <iostream>
void TestUpdatePanel();

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
    static void blockedStartup(DesktopWindow &window)
    {
        check(window.configurationBlocked_ && !window.save_->isEnabled(),
              "Unreadable configuration did not block switching/saving");
        if (window.shouldShowAtStartup(true, true)) window.show();
        check(window.isVisible() && !window.tray_->isVisible() &&
                  waitUntil([&] { return window.status_->text().contains("Saved profiles could not be read"); }),
              "Background startup hid the configuration recovery message");
    }
    static void recoveryDraft()
    {
        DesktopWindow window(true);
        window.editProfile_->setChecked(true);
        auto &monitor = window.draft_.monitors[0];
        monitor.autoInputs = true;
        const auto id = monitor.id;
        const auto inputs = monitor.inputs;
        ApplyMonitorInputs(window.draft_, id, {inputs.mid(1), "Subset", true});
        window.populateProfile(); window.captureDraft();
        check(window.active().actions[0].autoDisabled && !window.active().actions[0].missingInputs.isEmpty(),
              "Collecting a draft lost automatic recovery state");
        window.table_->item(0, 0)->setCheckState(Qt::Checked);
        window.table_->item(0, 0)->setCheckState(Qt::Unchecked);
        window.captureDraft();
        ApplyMonitorInputs(window.draft_, id, {inputs, "Full", true});
        check(!window.active().actions[0].enabled && !window.active().actions[0].autoDisabled,
              "Automatic recovery undid an explicit user disable");
    }
    static void monitorPicker()
    {
        DesktopWindow window(true);
        DesktopMonitor target{"LG 40U990A", -1, 3, false, QString(64, 'e'), "lg-40u990a"};
        target.transport = WindowsDdcTransport;
        target.nativeDisplay = "\\\\.\\DISPLAY3";
        target.hardware["adapter_kind"] = "intel";
        const auto initial = window.draft_.monitors.size();
        window.discovery_.monitors.push_back(target);
        QTimer::singleShot(0, &window, [&] {
            window.discovery_.monitors.clear();
            qobject_cast<QDialog *>(QApplication::activeModalWidget())->accept();
        });
        window.addMonitor(false);
        check(window.draft_.monitors.size() == initial, "Disconnected choice was saved");
        window.discovery_.monitors = {target};
        QTimer::singleShot(0, &window, [&] {
            auto other = target;
            other.identity = QString(64, 'b');
            other.nativeDisplay += "1";
            window.discovery_.monitors.prepend(other);
            qobject_cast<QDialog *>(QApplication::activeModalWidget())->accept();
        });
        window.addMonitor(false);
        const auto &added = window.draft_.monitors.back();
        check(added.identity == target.identity && added.method == "standard" &&
                  added.inputs.front().code == "0x0f", "Intel picker used a stale index or LG codes");
    }
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
                    check(window.monitorCard_->isVisibleTo(&window) ==
                              (page == 0),
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
        check(waitUntil([&] { return !window.busy_; }), "Automatic connection input read did not finish");
        window.save();
        check(!window.dirty_, "Initial profile did not save");
        QTimer::singleShot(0, &window, [&window] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog && dialog->width() >= 540 && dialog->height() >= 300,
                  "Add Monitor dialog is too small");
            auto *hint = dialog->findChild<QLabel *>("addMonitorHint");
            QCoreApplication::processEvents();
            const auto required =
                hint->fontMetrics()
                    .boundingRect(QRect(0, 0, hint->width(), 10000), Qt::TextWordWrap, hint->text())
                    .height();
            check(hint->height() >= required, "Add Monitor explanatory text is clipped");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        window.addMonitor();
        check(waitUntil([&] { return !window.busy_; }), "New monitor automatic input read did not finish");
        const auto addedId = window.active().monitors.back();
        const auto *added = FindMonitor(window.draft_, addedId);
        check(added && added->inputs.size() == 3 && added->inputs[2].label == "USB-C",
              "New monitor did not replace generic inputs with its own advertised ports");
        for (const auto &input : added->inputs)
            check(!input.label.contains("DisplayPort"), "Phantom DisplayPort remains in Dell input list");
        check(!QFile::exists(log), "Automatic setup sent a monitor write");
        window.confirmed_.remove(addedId);
        window.draft_ = window.saved_;
        {
            const QSignalBlocker block(window.editProfile_);
            window.editProfile_->setChecked(false);
        }
        window.populateProfile();
        window.dirty_ = false;
        window.updateActions();
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
        side.inputs = side.inputs.mid(0, 2);
        side.autoInputs = false;
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
        window.rebuildProfileTabs();
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
        check(waitUntil([&] { return !window.busy_; }), "Reconnect input read did not finish");
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
        window.diagnosticProfile_->setCurrentIndex(window.diagnosticProfile_->findData("group"));
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
        window.editProfile_->setChecked(true);
        window.table_->item(0, 1)->setText("Draft group name");
        window.syncShortcut(firstProfile, firstAction, QKeySequence("Ctrl+Alt+F9"));
        check(window.table_->item(0, 1)->text() == "Draft group name" && window.dirty_,
              "External shortcut discarded draft edits");
        QString warning;
        const auto persisted = LoadDesktopWorkspace(warning);
        check(persisted.profiles[0].actions[0].shortcut == "CTRL+ALT+F9" &&
                  persisted.profiles[1].actions[0].name == "Linux PC",
              "External shortcut was saved to wrong profile or saved unrelated draft");
        window.finishActionEdit(true);
        check(!window.dirty_, "Draft did not save");
        window.editProfile_->setChecked(true);
        const auto original = window.active().actions;
        window.table_->item(0, 1)->setText("Rename then cancel");
        window.table_->verticalHeader()->moveSection(0, 1);
        check(window.active().actions[1].id == original[0].id, "Drag lost action identity");
        auto *trash = qobject_cast<QPushButton *>(window.table_->cellWidget(0, window.table_->columnCount()-1));
        trash->click();
        check(window.active().actions.size() == 1, "Per-row trash removed the wrong number of actions");
        window.finishActionEdit(false);
        check(window.active().actions.size() == original.size() &&
              window.active().actions[0].id == original[0].id &&
              window.active().actions[0].name == original[0].name && !window.dirty_,
              "Cancel did not restore action contents and order");
        window.editProfile_->setChecked(true);
        window.table_->verticalHeader()->moveSection(0, 1);
        window.finishActionEdit(true);
        check(LoadDesktopWorkspace(warning).profiles[1].actions[0].id == original[1].id,
              "Save actions did not persist the reordered IDs");
        window.editMonitor("synthetic-side");
        auto *editor = window.monitorEditor_;
        check(editor && !QApplication::activeModalWidget(), "Monitor editing opened a dialog");
        auto *method = editor->findChild<QComboBox *>("monitorMethod");
        check(method && method->currentData() == "standard", "Monitor method lost in inline editor");
        auto *read = editor->findChild<QPushButton *>("readMonitorInputs");
        read->click();
        check(waitUntil([&] { return read->isEnabled(); }), "Advertised-input import did not finish");
        auto *inputs = editor->findChild<QTableWidget *>("monitorInputs");
        check(inputs->rowCount() == 4, "Advertised input import lost configured inputs");
        inputs->item(2, 0)->setCheckState(Qt::Unchecked);
        editor->findChild<QPushButton *>("primary")->click();
        check(!window.monitorEditor_ && window.draft_.monitors[1].inputs.size() == 4 &&
              !LoadDesktopWorkspace(warning).monitors[1].inputs[2].enabled,
              "Save inputs did not persist the edited list");
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
        QFile::remove(missingMarker);
        window.showQuickPanel();
        check(!window.busy_ && window.backgroundBrightness_, "Tray refresh blocked switching");
        window.sendInput(firstProfile, firstAction);
        check(window.busy_ && !window.backgroundBrightness_, "Switch did not preempt background brightness");
        check(waitUntil([&] { return !window.busy_; }), "Queued shortcut did not finish");
        window.quickPanel_->hide();
        window.dirty();
        window.selectedMonitorId_ = "synthetic-side";
        window.populateMonitorTabs();
        window.tabs_->setCurrentIndex(1);
        check(waitUntil([&] { return !window.backgroundBrightness_; }), "Workspace brightness read did not complete");
        auto *workspaceSlider = window.monitorInspector_->findChild<QSlider *>("brightness-synthetic-side");
        check(workspaceSlider && workspaceSlider->isEnabled() && workspaceSlider->value() == 55,
              "Pending profile edits blocked workspace brightness");
        window.brightness_.clear();
        window.scanInputs(true);
        window.showQuickPanel();
        check(waitUntil([&] { return !window.busy_ && !window.backgroundBrightness_; }),
              "Opening the tray during discovery lost the brightness read");
        auto *slider = window.quickPanel_->findChild<QSlider *>("brightness-synthetic-side");
        check(slider && slider->isEnabled() && slider->value() == 55,
              "Pending profile edits blocked tray brightness");
        slider->setValue(42);
        check(waitUntil([&] { return !window.busy_; }) && window.dirty_ &&
                  window.brightness_.value("synthetic-side").percent == 42,
              "Tray did not verify brightness independently of pending edits");
        auto *draftSide = window.monitor("synthetic-side");
        const auto originalRoute = draftSide->route;
        draftSide->route = window.discovery_.monitors.front();
        window.updateActions();
        check(!window.quickPanel_->findChild<QSlider *>("brightness-synthetic-side")->isEnabled(),
              "Tray allowed brightness on an unsaved connection");
        window.updateBrightness({"synthetic-side"}, 75);
        check(!window.busy_, "Queued a brightness write to an unsaved connection");
        draftSide->route = originalRoute;
        window.save();
        check(waitUntil([&] { return !window.backgroundBrightness_; }), "Post-save brightness refresh timed out");
        auto *trayMonitor = FindMonitor(window.saved_, "synthetic-side");
        check(trayMonitor != nullptr, "Saved tray monitor missing");
        QPushButton *button = nullptr;
        for (auto *candidate : window.quickPanel_->findChildren<QPushButton *>("direct-input"))
            if (candidate->accessibleName().startsWith(trayMonitor->name + " · "))
            {
                button = candidate;
                break;
            }
        check(button && button->isEnabled(), "Individual monitor controls missing from tray");
        QFile commands(log);
        check(commands.open(QIODevice::ReadOnly), "Could not read tray command log");
        const auto before = commands.readAll();
        commands.close();
        check(before.endsWith("--bus 31 setvcp 10 42\n"), "Brightness write targeted the wrong monitor");
        button->click();
        check(waitUntil([&] { return !window.busy_; }) && commands.open(QIODevice::ReadOnly),
              "Individual tray switch did not complete");
        const auto directCommands = commands.readAll().mid(before.size());
        commands.close();
        check(directCommands.startsWith("--bus 31 setvcp 0x60 ") && directCommands.count('\n') == 1,
              "Individual tray switch also wrote to another monitor");
        check(window.session_.state("group").pending.monitors.isEmpty(),
              "Individual monitor switch kept obsolete group retry state");
        const auto groupButtons = window.quickPanel_->findChildren<QPushButton *>("group-input");
        check(groupButtons.size() == 2,
              "Quick panel did not expose the configured group actions");
        groupButtons.front()->click();
        check(waitUntil([&] { return !window.busy_; }) && commands.open(QIODevice::ReadOnly),
              "Tray group switch did not complete");
        const auto groupCommands = commands.readAll().mid(before.size() + directCommands.size());
        check(groupCommands.startsWith("--bus 29 setvcp 0xf4 ") &&
                  groupCommands.contains("\n--bus 31 setvcp 0x60 ") && groupCommands.count('\n') == 2,
              "Tray group switch did not use both configured routes");
        window.quickPanel_->hide();
        compactLayout(window);
        Q_UNUSED(temp);
        Q_UNUSED(secondAction);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    check(QSslSocket::supportsSsl() && QSslSocket::activeBackend() == "schannel",
          "Portable update transport is missing Windows native TLS");
#endif
    app.setOrganizationName("LGInputSwitchTests");
    app.setApplicationName("Desktop");
    app.setStyle("Fusion");
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary directory unavailable");
    TestUpdatePanel();
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
    DesktopWindowProbe::recoveryDraft();
    DesktopWindowProbe::monitorPicker();
    const QRect area(-1280, 0, 1280, 800);
    for (const QRect icon : {QRect(-80, 800, 24, 24), QRect(-80, -24, 24, 24),
                             QRect(-1304, 30, 24, 24), QRect(0, 30, 24, 24)})
    {
        const QSize size(420, 346);
        const QRect popup(TrayQuickPanel::menuPosition(area, icon, size), size);
        check(area.contains(popup) && !popup.intersects(icon) &&
                  popup.adjusted(-1, -1, 1, 1).intersects(icon),
              "Quick panel missed its tray edge or left the clicked monitor");
    }
    // A profile editor must not leave another page locked after its section is saved.
    {
        DesktopWindow scenario(true);
        scenario.show();
        auto *actions = scenario.findChild<QPushButton *>("editProfile");
        auto *list = scenario.findChild<QTableWidget *>("inputs");
        scenario.resize(1150, 1020);
        QCoreApplication::processEvents();
        const auto listTop = list->mapTo(&scenario, QPoint()).y();
        actions->click();
        QCoreApplication::processEvents();
        auto *page = scenario.findChild<QScrollArea *>("profileScroll");
        check(list->mapTo(&scenario, QPoint()).y() == listTop, "Action list jumped when editing began");
        check(list->isVisible() && page->verticalScrollBar()->maximum() == 0 &&
                  list->verticalScrollBar()->maximum() == 0,
              "Tall action editor adds a scrollbar even when every row fits");
        list->item(0, 1)->setText("A discarded rename");
        for (auto *b : scenario.findChildren<QPushButton *>())
            if (b->accessibleName() == "Cancel action edits") b->click();
        auto *assign = scenario.findChild<QToolButton *>("monitorDetails");
        assign->click();
        for (auto *b : scenario.findChildren<QPushButton *>())
            if (b->text() == "Save monitors") b->click();
        check(!actions->isChecked() && !assign->isChecked(), "Profile editor remained open after saving");
        // Even unrelated unsaved preferences cannot change a saved physical input command.
        scenario.findChild<QCheckBox *>("systemAccent")->toggle();
        scenario.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto *direct = scenario.findChild<QPushButton *>("direct-input");
        check(direct && direct->isEnabled(), "Profile cancel/save or an unrelated preference locked monitor switching");
        direct->click();
        check(scenario.findChild<QLabel *>("status")->text().contains(" → "), "Saved monitor switch did not dispatch");
        scenario.findChild<QPushButton *>("editSavedMonitor")->click();
        auto *inputEditor = scenario.findChild<QWidget *>("inlineMonitorEditor");
        auto *ports = inputEditor->findChild<QTableWidget *>("monitorInputs");
        auto *automatic = inputEditor->findChild<QCheckBox *>("automaticMonitorInputs");
        const auto initial = ports->rowCount();
        auto *add = inputEditor->findChild<QPushButton *>("addMonitorInput");
        auto *remove = inputEditor->findChild<QPushButton *>("removeMonitorInput");
        check(add && remove && automatic->isChecked() && add->isEnabled(), "Automatic monitor cannot add custom inputs");
        add->click();
        check(ports->rowCount() == initial + 1 && ports->currentRow() == initial &&
                  !ports->isColumnHidden(1) && !automatic->isChecked(), "New input was not selected and editable");
        remove->click();
        check(ports->rowCount() == initial, "Remove input did not remove the selected custom input");
        ports->setCurrentCell(0, 0);
        auto answerRemoval = [&](QMessageBox::StandardButton choice) {
            QTimer::singleShot(0, inputEditor, [choice] {
                auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                check(prompt && prompt->text().contains("disable"), "Missing mapped-input removal warning");
                prompt->button(choice)->click();
            });
            remove->click();
        };
        answerRemoval(QMessageBox::Cancel);
        check(ports->rowCount() == initial, "Declining removal changed the list");
        answerRemoval(QMessageBox::Yes);
        check(ports->rowCount() == initial - 1, "Confirmed removal kept the selected input");
        inputEditor->findChild<QPushButton *>("primary")->click();
        auto *tabs = scenario.findChild<QTabBar *>("monitorTabs");
        check(tabs->isEnabled(), "Save inputs left monitor navigation locked");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        scenario.findChild<QPushButton *>("editSavedMonitor")->click();
        tabs->setCurrentIndex(1);
        check(tabs->currentIndex() == 1 && !scenario.findChild<QWidget *>("inlineMonitorEditor")->isVisible(),
              "An untouched input editor trapped monitor navigation");
        scenario.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(0);
        check(list->isRowHidden(0), "Saving a removed input left its mapped action active");
        scenario.findChild<QCheckBox *>("systemAccent")->toggle();
    }
    {
        DesktopWindow preview(true);
        preview.show();
        auto *updateCheck = preview.findChild<QPushButton *>("checkUpdates");
        check(updateCheck, "Update controls are missing");
        updateCheck->click();
        check(preview.findChild<QLabel *>("updateStatus")->text().contains("disabled"),
              "Design preview attempted an update request");
        app.processEvents();
        auto *appearance = preview.findChild<QComboBox *>("appearance");
        auto *table = preview.findChild<QTableWidget *>("inputs");
        auto *profiles = preview.findChild<QTabBar *>("profileTabs");
        check(appearance && table && table->rowCount() == 4 && profiles && profiles->count() == 4 && profiles->tabText(3) == "+",
              "Inputs or profile tabs missing");
        auto *systemAccent = preview.findChild<QCheckBox *>("systemAccent");
        check(systemAccent && !systemAccent->isChecked() &&
                  app.palette().color(QPalette::Highlight) == QColor("#3268e8"),
              "System appearance did not default to the icon blue");
        const auto systemWindow = app.palette().color(QPalette::Window);
        systemAccent->setChecked(true);
        check(!app.palette().isBrushSet(QPalette::Active, QPalette::Highlight) &&
                  app.palette().color(QPalette::Window) == systemWindow,
              "System accent override changed the surface or retained the brand highlight");
        systemAccent->setChecked(false);
        appearance->setCurrentIndex(appearance->findData("dark"));
        app.processEvents();
        check(app.palette().color(QPalette::Inactive, QPalette::Highlight) == QColor("#3268e8") &&
                  app.palette().color(QPalette::Window).lightness() < 60, "Dark palette not applied");
        appearance->setCurrentIndex(appearance->findData("light"));
        app.processEvents();
        check(app.palette().color(QPalette::Highlight) == QColor("#3268e8") &&
                  app.palette().color(QPalette::Window).lightness() > 220, "Light palette not applied");
        check(!table->cellWidget(0, 2) && table->item(0, 2)->text() == "DisplayPort" &&
                  table->isColumnHidden(0),
              "Normal switching view still exposes input editors");
        auto *switchAction = qobject_cast<QPushButton *>(table->cellWidget(0, table->columnCount() - 1));
        check(switchAction && switchAction->text() == "Switch", "Profile action lacks a Switch button");
        switchAction->click();
        check(preview.findChild<QLabel *>("status")->text().contains("switching is disabled"),
              "Switch button did not dispatch the preview action");
        preview.findChild<QPushButton *>("editProfile")->setChecked(true);
        auto *input = qobject_cast<QComboBox *>(table->cellWidget(0, 2));
        check(input && !input->currentText().contains("0x"), "Codes unexpectedly exposed by default");
        check(table->visualItemRect(table->item(3, 1)).bottom() <= table->viewport()->height(),
              "Last input is clipped");
        table->item(0, 1)->setText("Unsaved label");
        profiles->setCurrentIndex(2);
        app.processEvents();
        check(!preview.findChild<QPushButton *>("editProfile")->isChecked(), "Opening another profile inherited the previous editor");
        preview.findChild<QPushButton *>("editProfile")->click();
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
        check(navigation->count() == 3 && navigation->tabText(0) == "Input Profiles" &&
                  navigation->tabText(1) == "Monitors" && navigation->tabText(2) == "Settings" &&
                  preview.findChild<QListWidget *>("workspaceNavigation"),
              "Workspace navigation is missing");
        navigation->setCurrentIndex(1);
        auto *monitorTabs = preview.findChild<QTabBar *>("monitorTabs");
        check(monitorTabs && monitorTabs->count() == 3 && monitorTabs->tabText(2) == "+",
              "Monitors lack matching tabs and add control");
        monitorTabs->setCurrentIndex(1);
        app.processEvents();
        auto *editMonitor = preview.findChild<QPushButton *>("editSavedMonitor");
        check(editMonitor && editMonitor->isVisible(), "Monitor inspector lacks a direct edit button");
        editMonitor->click();
        auto *editor = preview.findChild<QWidget *>("inlineMonitorEditor");
        check(editor && !QApplication::activeModalWidget() &&
              editor->findChild<QLineEdit *>("monitorName")->text() == "Side monitor",
              "Monitor tab did not edit the selected monitor inline");
        editor->findChild<QLineEdit *>("monitorName")->setText("Cancel this change");
        editor->findChild<QPushButton *>("cancelInputs")->click();
        check(monitorTabs->tabText(1) == "Side monitor", "Cancel renamed the monitor");
        check(monitorTabs->currentIndex() == 1, "Rebuilding controls changed the selected monitor tab");
        monitorTabs->setCurrentIndex(2);
        check(monitorTabs->currentIndex() == 1, "Unavailable add-monitor action changed the selected monitor");
        navigation->setCurrentIndex(2);
        auto *diagnosticProfile = preview.findChild<QComboBox *>("diagnosticProfile");
        diagnosticProfile->setCurrentIndex(0);
        app.processEvents();
        check(profiles->currentIndex() == 2 && table->item(0, 1)->text() == "Unsaved group label",
              "Diagnostics changed the active profile or its draft");
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
        auto *tray = preview.findChild<QSystemTrayIcon *>();
        preview.hide();
        tray->activated(QSystemTrayIcon::Trigger);
        tray->activated(QSystemTrayIcon::DoubleClick);
        check(preview.isVisible() && !preview.findChild<QDialog *>("quickPanel")->isVisible(),
              "Double-click did not open the workspace");
        QTimer::singleShot(0, &preview, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            check(dialog != nullptr, "Add profile dialog missing");
            dialog->setTextValue("New profile");
            dialog->accept();
        });
        profiles->setCurrentIndex(profiles->count() - 1);
        check(profiles->count() == 5 && profiles->currentIndex() == 3 &&
                  profiles->tabText(3) == "New profile" && profiles->tabText(4) == "+" && table->rowCount() == 4,
              "Add profile did not create independent tab");
        QTimer::singleShot(0, &preview, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog != nullptr, "Add monitor dialog missing");
            dialog->accept();
        });
        preview.findChild<QToolButton *>("monitorDetails")->setChecked(true);
        preview.findChild<QPushButton *>("addMonitor")->click();
        check(table->columnCount() == 6 && qobject_cast<QComboBox *>(table->cellWidget(0, 3)) != nullptr,
              "Add monitor did not expose independent per-monitor mappings");
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
    fake.write(
        ("#!/bin/sh\n"
         "if [ \"$1\" = detect ]; then\n"
         "printf 'Invalid display\\n I2C bus: /dev/i2c-4\\n Monitor: LCD:Internal:PRIVATE\\n"
         "Display 1\\n I2C bus: /dev/i2c-29\\n Monitor: GSM:Synthetic main:MAINPRIVATE\\n'\n"
         "if [ ! -f '" +
         missingMarker +
         "' ]; then printf 'Display 2\\n I2C bus: /dev/i2c-31\\n Monitor: DEL:DELL Synthetic "
         "side:SIDEPRIVATE\\n'; fi\n"
         "elif [ \"$1\" = --version ]; then echo 'ddcutil 2.2.0'\n"
         "else\ncase \"$*\" in\n"
         "'--bus 31 setvcp 10 '*) printf '%s\\n' \"$*\" >> '" + log +
         "'; printf '%s\\n' \"$5\" > '" + temporary.filePath("brightness") + "';;\n"
         "*setvcp*) printf '%s\\n' \"$*\" >> '" +
         log + "'; if [ \"$2\" = 31 ] && [ -f '" + failMarker +
         "' ]; then exit 87; fi;;\n"
         "'--bus 31 getvcp 10 --terse') value=55; if [ -f '" + temporary.filePath("brightness") +
         "' ]; then read -r value < '" + temporary.filePath("brightness") +
         "'; fi; printf 'VCP 10 C %s 100\\n' \"$value\";;\n"
         "*'getvcp 10 --terse') echo 'VCP 10 C 55 100';;\n"
         "*'getvcp 60 --terse') echo 'VCP 60 SNC x0f';;\n"
         "'--bus 31 capabilities --terse') echo '(model(PRIVATE)vcp(10 60(1B 11 12))mccs_ver(2.2))';;\n"
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
    for (const QByteArray blocked : {QByteArray("{broken"), QByteArray("{\"version\":999}")})
    {
        legacy.setValue("profilesV2", blocked); legacy.sync();
        DesktopWindow recovery(false);
        DesktopWindowProbe::blockedStartup(recovery);
    }
    legacy.remove("profilesV2"); legacy.sync();
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
