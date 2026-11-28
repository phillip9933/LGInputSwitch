#include "updates.h"
#include "monitor_input_editor.h"
#include "workspace_icons.h"
#include "content_table.h"
#include "tray_quick_panel.h"
#include "../core/input_value.h"
#include "diagnostics.h"
#include "kde_shortcuts.h"
#include "monitor_inputs.h"
#include "profiles.h"
#include "windows_monitors.h"
#ifndef Q_OS_WIN
#include "linux_autostart.h"
#endif
#include <QtConcurrent>
#include <QtWidgets>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <algorithm>
#include <atomic>
#ifdef Q_OS_WIN
#include "windows_hotkeys.h"
#include "../platform/windows/startup.h"
#endif

static const QColor IconBlue("#3268e8");

static QIcon MonitorIcon()
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256})
    {
        QPixmap image(size, size);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 32.0, size / 32.0);
        p.setPen(Qt::NoPen);
        p.setBrush(IconBlue);
        p.drawRoundedRect(QRectF(1, 1, 30, 30), 8, 8);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRoundedRect(QRectF(6, 7, 20, 14), 2, 2);
        p.drawLine(QPointF(16, 21), QPointF(16, 25));
        p.drawLine(QPointF(11, 25), QPointF(21, 25));
        p.drawLine(QPointF(11, 14), QPointF(21, 14));
        p.drawLine(QPointF(18, 11), QPointF(21, 14));
        p.drawLine(QPointF(18, 17), QPointF(21, 14));
        icon.addPixmap(image);
    }
    return icon;
}

static QPixmap WorkspaceMark(const QColor &color)
{
    QPixmap image(24, 24);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawRoundedRect(QRectF(3, 4, 18, 13), 2, 2);
    painter.drawLine(QPointF(12, 17), QPointF(12, 21));
    painter.drawLine(QPointF(8, 21), QPointF(16, 21));
    return image;
}

struct MonitorSetup
{
    Discovery discovery;
    QVector<QPair<QString, MonitorInputRead>> inputs;
};

class WorkspacePicker final : public QComboBox
{
  public:
    WorkspacePicker() { setProperty("workspacePicker", true); }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const auto center = style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                    QStyle::SC_ComboBoxArrow, this).center();
        const auto color = option.palette.color(QPalette::WindowText);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPolygon arrow{center + QPoint(-3, -1), center + QPoint(0, 2), center + QPoint(3, -1)};
        painter.drawPolyline(arrow);
    }
};

class DesktopWindow final : public QWidget, public QAbstractNativeEventFilter
{
#ifdef LG_DESKTOP_TEST
    friend struct DesktopWindowProbe;
#endif
    DesktopWorkspace saved_, draft_;
    DesktopConfig config_; // app-wide saved preferences (also used by the live KDE probe)
    ProfileSession session_;
    KdeShortcuts kdeShortcuts_;
    const bool kdeAvailable_ = KdeShortcuts::available();
    bool preview_, busy_ = false, dirty_ = false, quitting_ = false, populating_ = false,
                   recordingShortcut_ = false;
    bool configurationBlocked_ = false, registeringShortcuts_ = false;
    QString activeId_;
    Discovery discovery_;
    QMap<QString, DesktopMonitor> confirmed_;
    QThreadPool worker_;
    QElapsedTimer lastSend_;
    QTabWidget *tabs_;
    ContentTable *members_, *table_;
    QComboBox *appearance_, *diagnosticProfile_;
    QCheckBox *notifications_, *startup_, *globalShortcuts_, *codes_;
    QSpinBox *debounce_;
    QKeySequenceEdit *cycleShortcut_, *previousShortcut_;
    QPushButton *save_, *cycle_, *previous_, *retry_, *addMonitor_, *editProfile_;
    QAction *renameProfile_, *removeProfile_;
    QWidget *controls_, *monitorCard_, *editTools_, *shortcutPanel_, *monitorTools_;
    QToolButton *monitorDetails_;
    QLabel *monitorSummary_;
    QScrollArea *profileScroll_, *settingsScroll_;
    QSystemTrayIcon *tray_;
    QPointer<QWidget> notificationPage_;
    QMenu *menu_;
    QListWidget *navigation_ = nullptr;
    QTabBar *monitorTabs_ = nullptr;
    QString selectedMonitorId_;
    QTabBar *profileTabs_ = nullptr;
    QComboBox *headerAppearance_ = nullptr;
    QMap<QString, QString> lastSentInput_;
    QWidget *monitorInspector_ = nullptr;
    QWidget *diagnosticsSection_ = nullptr;
    TrayQuickPanel *quickPanel_ = nullptr;
    QVBoxLayout *quickItems_ = nullptr;
    QLabel *quickStatus_ = nullptr;
    QMap<QString, MonitorProfile> beforeActions_, beforeAssignments_;
    bool advancedConnectionOpen_ = false;
    QWidget *monitorEditorHost_ = nullptr;
    MonitorInputEditor *monitorEditor_ = nullptr;
    QMap<QString, MonitorBrightness> brightness_;
    std::shared_ptr<std::atomic_bool> backgroundBrightness_;
    void cancelBrightnessRead()
    {
        if (backgroundBrightness_)
        {
            backgroundBrightness_->store(true);
            backgroundBrightness_.reset();
        }
    }
    QCheckBox *systemAccent_;
    QLabel *status_;
    QPlainTextEdit *results_, *diagnosticReport_;
    QPushButton *diagnosticRun_, *diagnosticCopy_, *diagnosticSave_;
    QJsonObject lastCommand_;
#ifdef Q_OS_WIN
    std::map<int, QPair<QString, QString>> hotkeys_;
#endif
    MonitorProfile &active()
    {
        for (auto &p : draft_.profiles)
            if (p.id == activeId_)
                return p;
        return draft_.profiles.front();
    }
    SavedMonitor *monitor(const QString &id)
    {
        for (auto &m : draft_.monitors)
            if (m.id == id)
                return &m;
        return nullptr;
    }
    void showWindow()
    {
        showNormal();
        raise();
        activateWindow();
    }
    void message(const QString &text)
    {
        status_->setText(text.size() > 220 ? text.left(217) + "…" : text);
        status_->setToolTip(text.toHtmlEscaped());
        if (quickStatus_)
            quickStatus_->setText(text.size() > 220 ? text.left(217) + "…" : text);
    }
    void dirty()
    {
        if (populating_ || dirty_)
            return;
        dirty_ = true;
        save_->setText("Save changes");
        updateActions();
    }
    bool shortcutEditingAvailable() const
    {
#ifdef Q_OS_WIN
        return true;
#else
        return preview_ || (kdeAvailable_ && globalShortcuts_->isChecked());
#endif
    }
    void captureDraft()
    {
        if (populating_)
            return;
        auto &p = active();
        if (table_->property("editingActions").toBool())
        {
            QVector<ProfileAction> actions;
            for (int row = 0; row < table_->rowCount(); ++row)
            {
                ProfileAction a;
                a.id = table_->item(row, 0)->data(Qt::UserRole).toString();
                for (const auto &previous : p.actions)
                    if (previous.id == a.id)
                        a = previous;
                a.name = table_->item(row, 1)->text().trimmed();
                a.enabled = table_->item(row, 0)->checkState() == Qt::Checked;
                for (int i = 0; i < p.monitors.size(); ++i)
                {
                    auto *combo = qobject_cast<QComboBox *>(table_->cellWidget(row, 2 + i));
                    const auto *item = table_->item(row, 2 + i);
                    const auto before = a.inputs.value(p.monitors[i]);
                    a.inputs[p.monitors[i]] = combo  ? combo->currentData().toString()
                                              : item ? item->data(Qt::UserRole).toString()
                                                     : QString{};
                    if (before != a.inputs.value(p.monitors[i]))
                        a.missingInputs.remove(p.monitors[i]);
                }
                if (a.enabled) a.autoDisabled = false;
                auto *key = qobject_cast<QKeySequenceEdit *>(table_->cellWidget(row, 2 + p.monitors.size()));
                const auto *keyItem = table_->item(row, 2 + p.monitors.size());
                a.shortcut = key       ? key->keySequence().toString(QKeySequence::PortableText).toUpper()
                             : keyItem ? keyItem->data(Qt::UserRole).toString()
                                       : QString{};
                actions.push_back(a);
            }
            p.actions = actions;
            p.cycleShortcut = cycleShortcut_->keySequence().toString(QKeySequence::PortableText).toUpper();
            p.previousShortcut = previousShortcut_->keySequence().toString(QKeySequence::PortableText).toUpper();
        }
        auto &c = draft_.preferences;
        c.appearance = appearance_->currentData().toString();
        c.systemAccent = systemAccent_->isChecked();
        c.notifications = notifications_->isChecked();
        c.startup = startup_->isChecked();
        c.globalShortcuts = globalShortcuts_->isChecked();
        c.debounce = debounce_->value();
        draft_.activeProfile = activeId_;
    }
    void setBusy(bool value)
    {
        if (value)
            cancelBrightnessRead();
        busy_ = value;
        controls_->setEnabled(!value);
        monitorCard_->setEnabled(!value);
        save_->setEnabled(!value && !configurationBlocked_);
        updateActions();
    }
    void rebuildProfileTabs()
    {
        const auto diagnosticTarget = diagnosticProfile_->currentData().toString();
        const QSignalBlocker diagnosticBlock(diagnosticProfile_);
        const QSignalBlocker tabBlock(profileTabs_);
        while (profileTabs_->count())
            profileTabs_->removeTab(0);
        diagnosticProfile_->clear();
        int selected = 0;
        for (const auto &p : draft_.profiles)
        {
            diagnosticProfile_->addItem(p.name, p.id);
            const int index = profileTabs_->addTab(p.name);
            profileTabs_->setTabData(index, p.id);
            profileTabs_->setTabToolTip(index, p.name);
            if (p.id == activeId_)
                selected = index;
        }
        const int add = profileTabs_->addTab("+");
        profileTabs_->setTabToolTip(add, "Add profile");
        profileTabs_->setAccessibleTabName(add, "Add profile");
        profileTabs_->setTabEnabled(add, draft_.profiles.size() < 32);
        profileTabs_->setCurrentIndex(selected);
        const int reportIndex = diagnosticProfile_->findData(diagnosticTarget);
        diagnosticProfile_->setCurrentIndex(reportIndex >= 0 ? reportIndex : selected);
    }
    void selectConnection(const QString &id, int selected)
    {
        if (populating_ || selected < 0 || selected >= discovery_.monitors.size())
            return;
        captureDraft();
        auto *m = monitor(id);
        if (!m)
            return;
        const auto target = discovery_.monitors[selected];
        for (const auto &other : draft_.monitors)
        {
            const auto resolved = ResolveSavedMonitor(other, discovery_, confirmed_);
            if (other.id != id && resolved.available &&
                MonitorRouteKey(resolved.target) == MonitorRouteKey(target))
            {
                message("That monitor is already saved as “" + other.name +
                        "”. Add the saved monitor to this profile instead.");
                populateMembers();
                return;
            }
        }
        // The initial unbound profile has LG presets. Convert only automatic presets
        // when first pairing it with a connection that can only send standard commands.
        if (m->route.display < 0 && m->autoInputs && m->method == "lg" &&
            !SupportsLgAlternate(target) && IsPresetInputList(*m))
        {
            m->method = "standard";
            const auto presets = DefaultMonitorInputs(m->method);
            for (auto &input : m->inputs)
                for (const auto &preset : presets)
                    if (InputNameKey(input.label) == InputNameKey(preset.label))
                        input.code = preset.code;
            ApplyMonitorInputs(draft_, id, {presets, {}, true});
            populateProfile();
        }
        cancelBrightnessRead();
        brightness_.remove(id);
        m->route = target;
        int matches = 0;
        for (const auto &candidate : discovery_.monitors)
            if (!target.identity.isEmpty() && candidate.identity == target.identity)
                ++matches;
        // Duplicate/absent identities are explicitly confirmed for this session.
        m->identity = matches == 1 ? target.identity : QString{};
        confirmed_[id] = target;
        dirty();
        populateMembers();
        message("Connection selected. Reading this monitor’s inputs…");
        scanInputs(false, {id});
    }
    void reconnectMonitor(const QString &id)
    {
        if (busy_)
            return;
        const auto *definition = FindMonitor(draft_, id);
        if (!definition)
            return;
        const auto savedMonitor = *definition;
        QDialog dialog(this);
        dialog.setWindowTitle("Connect “" + savedMonitor.name + "”");
        dialog.resize(520, 220);
        auto *layout = new QVBoxLayout(&dialog);
        auto *hint =
            new QLabel("Reconnect this saved monitor to a detected connection. This updates every profile "
                       "using it. To use another saved monitor, add it with + Monitor instead.");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *choice = new QComboBox;
        choice->setObjectName("reconnectChoice");
        choice->setPlaceholderText("Choose the monitor’s connection…");
        layout->addWidget(choice);
        auto *scan = new QPushButton("Refresh connected monitors");
        layout->addWidget(scan);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        auto populate = [&, choice] {
            const QSignalBlocker block(choice);
            choice->clear();
            const auto current = ResolveSavedMonitor(savedMonitor, discovery_, confirmed_);
            int selected = -1;
            for (int i = 0; i < discovery_.monitors.size(); ++i)
            {
                const auto &candidate = discovery_.monitors[i];
                bool assigned = false;
                for (const auto &other : draft_.monitors)
                {
                    if (other.id == id)
                        continue;
                    const auto resolved = ResolveSavedMonitor(other, discovery_, confirmed_);
                    if (resolved.available && MonitorRouteKey(resolved.target) == MonitorRouteKey(candidate))
                        assigned = true;
                }
                if (assigned)
                    continue;
                choice->addItem(candidate.label, i);
                if (current.available && MonitorRouteKey(current.target) == MonitorRouteKey(candidate))
                    selected = choice->count() - 1;
            }
            choice->setCurrentIndex(selected);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(selected >= 0);
        };
        populate();
        connect(choice, &QComboBox::currentIndexChanged, &dialog, [choice, buttons](int index) {
            buttons->button(QDialogButtonBox::Ok)->setEnabled(index >= 0);
        });
        connect(scan, &QPushButton::clicked, &dialog, [&, populate] {
            scan->setEnabled(false);
            choice->setEnabled(false);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            auto *watcher = new QFutureWatcher<Discovery>(&dialog);
            connect(watcher, &QFutureWatcher<Discovery>::finished, &dialog, [&, watcher, populate] {
                discovery_ = watcher->result();
                watcher->deleteLater();
                populate();
                scan->setEnabled(true);
                choice->setEnabled(true);
            });
            watcher->setFuture(QtConcurrent::run(&worker_, [] { return DiscoverDesktopMonitors(); }));
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        cancelBrightnessRead();
        busy_ = true;
        updateActions();
        const bool accepted = dialog.exec() == QDialog::Accepted;
        busy_ = false;
        if (accepted && choice->currentIndex() >= 0)
            selectConnection(id, choice->currentData().toInt());
        else
            populateMembers();
        updateActions();
    }
    void populateMembers()
    {
        const QScopedValueRollback<bool> populating(populating_, true);
        const QSignalBlocker block(members_);
        const auto &p = active();
        const bool editing = monitorDetails_->isChecked();
        members_->setRowCount(0);
        for (const auto &m : draft_.monitors)
        {
            const bool assigned = p.monitors.contains(m.id);
            if (!editing && !assigned)
                continue;
            const int row = members_->rowCount();
            members_->insertRow(row);
            auto *name = new QTableWidgetItem(m.name);
            name->setData(Qt::UserRole, m.id);
            name->setFlags(Qt::ItemIsEnabled);
            if (editing)
            {
                name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                name->setCheckState(assigned ? Qt::Checked : Qt::Unchecked);
            }
            members_->setItem(row, 0, name);
            const auto resolved = ResolveSavedMonitor(m, discovery_, confirmed_);
            auto *status = new QTableWidgetItem(resolved.available ? "Connected" : "Connection needed");
            status->setToolTip(resolved.available ? resolved.target.label : resolved.reason);
            members_->setItem(row, 1, status);
            members_->setRowHeight(row, 32);
        }
        members_->fitRows();
        if (editing && !beforeAssignments_.contains(p.id))
            beforeAssignments_.insert(p.id, p);
        monitorTools_->setVisible(editing);
    }
    void populateProfile()
    {
        populating_ = true;
        populateMembers();
        const auto &p = active();
        const bool editing = editProfile_->isChecked();
        cycle_->setVisible(!editing);
        previous_->setVisible(!editing);
        table_->setProperty("editingActions", editing);
        editTools_->setVisible(editing);
        shortcutPanel_->setVisible(editing && shortcutEditingAvailable());
        if (editing && !beforeActions_.contains(p.id))
            beforeActions_.insert(p.id, p);
        monitorDetails_->setEnabled(!editing);
        table_->verticalHeader()->setVisible(editing);
        table_->setEditTriggers(editing ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                        : QAbstractItemView::NoEditTriggers);
        const QSignalBlocker block(table_);
        table_->clear();
        table_->setColumnCount(4 + p.monitors.size());
        for (int col = 0; col < table_->columnCount(); ++col)
            table_->setColumnHidden(col, false);
        QStringList headings{"Use", "Action"};
        for (const auto &id : p.monitors)
        {
            const auto *m = FindMonitor(draft_, id);
            headings.push_back(m ? m->name : "Missing monitor");
        }
        headings << "Shortcut" << "";
        table_->setHorizontalHeaderLabels(headings);
        table_->setRowCount(p.actions.size());
        table_->setColumnWidth(0, 40);
        table_->setColumnWidth(1, 150);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        for (int row = 0; row < p.actions.size(); ++row)
        {
            const auto &a = p.actions[row];
            table_->setRowHidden(row, !editing && !a.enabled);
            auto *enabled = new QTableWidgetItem;
            enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            enabled->setData(Qt::UserRole, a.id);
            enabled->setCheckState(a.enabled ? Qt::Checked : Qt::Unchecked);
            table_->setItem(row, 0, enabled);
            auto *name = new QTableWidgetItem(a.name);
            table_->setItem(row, 1, name);
            if (editing)
            {
                auto *field = new QLineEdit(a.name);
                field->setMaxLength(100);
                field->setAccessibleName("Action name");
                connect(field, &QLineEdit::textEdited, table_, [name](const QString &text) { name->setText(text); });
                table_->setCellWidget(row, 1, field);
            }
            for (int col = 0; col < p.monitors.size(); ++col)
            {
                const auto id = p.monitors[col];
                if (!editing)
                {
                    const auto *m = FindMonitor(draft_, id);
                    const auto *input = m ? FindMonitorInput(*m, a.inputs.value(id)) : nullptr;
                    const auto text =
                        input ? input->label + (codes_->isChecked() ? " (" + input->code + ")" : "")
                              : "Choose an input in Edit actions";
                    auto *item = new QTableWidgetItem(text);
                    item->setData(Qt::UserRole, a.inputs.value(id));
                    item->setToolTip(text);
                    table_->setItem(row, 2 + col, item);
                    table_->setColumnWidth(2 + col, 124);
                    continue;
                }
                auto *combo = new QComboBox;
                combo->setAccessibleName(a.name + " input for " + headings[2 + col]);
                combo->setPlaceholderText("Choose input…");
                if (const auto *m = FindMonitor(draft_, id))
                    for (const auto &input : m->inputs)
                        combo->addItem(codes_->isChecked() ? input.label + " (" + input.code + ")"
                                                           : input.label,
                                       input.id);
                combo->setCurrentIndex(combo->findData(a.inputs.value(id)));
                connect(combo, &QComboBox::currentIndexChanged, this, [this, actionId = a.id, id] {
                    if (!populating_)
                        for (auto &action : active().actions)
                            if (action.id == actionId)
                            {
                                action.missingInputs.remove(id);
                                action.autoDisabled = false;
                            }
                    dirty();
                });
                table_->setCellWidget(row, 2 + col, combo);
                table_->setColumnWidth(2 + col, 124);
            }
            const auto sequence = QKeySequence::fromString(a.shortcut, QKeySequence::PortableText);
            if (editing)
            {
                auto *key = new QKeySequenceEdit(sequence);
                key->setMaximumSequenceLength(1);
                key->setEnabled(shortcutEditingAvailable());
                connect(key, &QKeySequenceEdit::keySequenceChanged, this, [this] { dirty(); });
                table_->setCellWidget(row, 2 + p.monitors.size(), key);
            }
            else
            {
                auto *key = new QTableWidgetItem(sequence.toString(QKeySequence::NativeText));
                key->setData(Qt::UserRole, a.shortcut);
                table_->setItem(row, 2 + p.monitors.size(), key);
            }
            auto *grip = new QTableWidgetItem(QStringLiteral("⠿"));
            grip->setToolTip("Drag to reorder. Use Alt+Up or Alt+Down on the action to move it with the keyboard.");
            table_->setVerticalHeaderItem(row, grip);
            auto *send = new QPushButton;
            if (editing)
            {
                configureIconButton(send, "Delete " + a.name, true);
                send->setEnabled(p.actions.size() > 1);
                if (p.actions.size() == 1) send->setToolTip("A profile needs at least one action.");
                connect(send, &QPushButton::clicked, this, [this, key = a.id] {
                    captureDraft();
                    if (active().actions.size() <= 1) return;
                    active().actions.removeIf([&](const ProfileAction &action) { return action.id == key; });
                    dirty();
                    populateProfile();
                });
            }
            else
            {
                send->setText("Switch");
                send->setAccessibleName("Switch to " + a.name);
                send->setProperty("switchAction", true);
                connect(send, &QPushButton::clicked, this,
                        [this, id = p.id, key = a.id] { sendInput(id, key); });
            }
            table_->setCellWidget(row, 3 + p.monitors.size(), send);
            table_->setRowHeight(row, 46);
        }
        table_->setColumnHidden(0, !editing);
        const int shortcutCol = 2 + p.monitors.size();
        table_->setColumnWidth(shortcutCol, 130);
        table_->setColumnWidth(shortcutCol + 1, 76);
#ifndef Q_OS_WIN
        table_->setColumnHidden(shortcutCol, !kdeAvailable_ && !preview_);
#endif
        {
            const QSignalBlocker b(cycleShortcut_);
            cycleShortcut_->setKeySequence(
                QKeySequence::fromString(p.cycleShortcut, QKeySequence::PortableText));
        }
        {
            const QSignalBlocker b(previousShortcut_);
            previousShortcut_->setKeySequence(
                QKeySequence::fromString(p.previousShortcut, QKeySequence::PortableText));
        }
        cycleShortcut_->setEnabled(shortcutEditingAvailable());
        previousShortcut_->setEnabled(shortcutEditingAvailable());
        table_->fitRows();
        populating_ = false;
        showResults();
        updateActions();
    }
    void addProfile()
    {
        captureDraft();
        bool ok = false;
        const auto name =
            QInputDialog::getText(this, "Add profile", "Profile name", QLineEdit::Normal, "", &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        for (const auto &p : draft_.profiles)
            if (p.name == name)
            {
                message("Choose a unique profile name.");
                return;
            }
        MonitorProfile p;
        p.id = NewDesktopId();
        p.name = name;
        // Reuse the first saved monitor, with independent actions and no
        // duplicate default shortcuts. Membership is explicitly editable.
        const auto &m = draft_.monitors.front();
        p.monitors = {m.id};
        for (const auto &input : m.inputs)
            p.actions.push_back({NewDesktopId(), input.label, {}, true, {{m.id, input.id}}});
        draft_.profiles.push_back(p);
        activeId_ = p.id;
        {
            const QSignalBlocker actions(editProfile_), members(monitorDetails_);
            monitorDetails_->setChecked(false);
            editProfile_->setEnabled(true);
            editProfile_->setChecked(true);
        }
        rebuildProfileTabs();
        populateProfile();
        dirty();
    }
    void addMonitor(bool assignToProfile = true)
    {
        captureDraft();
        QDialog dialog(this);
        dialog.setWindowTitle("Add monitor");
        dialog.setMinimumSize(540, 300);
        dialog.resize(640, 340);
        auto *layout = new QVBoxLayout(&dialog);
        auto *choice = new QComboBox(&dialog);
        choice->setObjectName("addMonitorChoice");
        choice->setMinimumContentsLength(24);
        choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        const auto choices = discovery_.monitors;
        if (assignToProfile)
            for (const auto &m : draft_.monitors)
                if (!active().monitors.contains(m.id))
                    choice->addItem("Saved: " + m.name, m.id);
        for (int i = 0; i < choices.size(); ++i)
        {
            const auto &target = choices[i];
            bool known = false;
            for (const auto &m : draft_.monitors)
            {
                const auto resolved = ResolveSavedMonitor(m, discovery_, confirmed_);
                if (resolved.available && MonitorRouteKey(resolved.target) == MonitorRouteKey(target))
                    known = true;
            }
            if (!known && draft_.monitors.size() < 16)
                choice->addItem("New: " + target.label, i);
        }
        if (choice->count() == 0)
        {
            message("No other monitors available. Connect a monitor and click Refresh.");
            return;
        }
        auto *hint = new QLabel(assignToProfile
            ? "Choose a saved monitor or add a connected monitor to “" + active().name + "”."
            : "Choose a connected monitor. Its advertised inputs will be read automatically.");
        hint->setTextFormat(Qt::PlainText);
        hint->setObjectName("addMonitorHint");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        layout->addWidget(choice);
        auto *method = new QComboBox;
        method->addItem("Standard DDC/CI input source (0x60)", "standard");
        method->addItem("LG alternate input switching (0xF4)", "lg");
        auto *advanced = new QCheckBox("Customize switching method");
        auto *methodNote = new QLabel("This connection can send standard DDC/CI commands only. "
                                     "If this LG monitor requires alternate switching, use a compatible GPU connection.");
        methodNote->setWordWrap(true);
        auto methodVisibility = [choices, choice, method, advanced, methodNote] {
            const bool newMonitor = choice->currentData().metaType().id() != QMetaType::QString;
            const auto target = newMonitor ? choices.at(choice->currentData().toInt()) : DesktopMonitor{};
            const bool alternate = SupportsLgAlternate(target);
            advanced->setVisible(newMonitor && alternate);
            method->setVisible(newMonitor && alternate && advanced->isChecked());
            methodNote->setVisible(newMonitor && !alternate && !target.inputProfile.isEmpty());
            if (newMonitor && (!alternate || !advanced->isChecked()))
                method->setCurrentIndex(method->findData(alternate && !target.inputProfile.isEmpty()
                                                             ? "lg" : "standard"));
        };
        connect(choice, &QComboBox::currentIndexChanged, &dialog, methodVisibility);
        connect(advanced, &QCheckBox::toggled, &dialog, methodVisibility);
        methodVisibility();
        layout->addWidget(advanced);
        layout->addWidget(method);
        layout->addWidget(methodNote);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        bool checkingConnection = false;
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, choice, buttons, hint, advanced, method] {
            if (busy_)
            {
                hint->setText("A monitor command is in progress. Click OK when it finishes.");
                return;
            }
            if (preview_ || choice->currentData().metaType().id() == QMetaType::QString)
            {
                dialog.accept();
                return;
            }
            // Recheck the physical connection even if nothing else refreshed discovery
            // while the picker was open. Cancel remains available during the probe.
            checkingConnection = true;
            setBusy(true);
            choice->setEnabled(false);
            advanced->setEnabled(false);
            method->setEnabled(false);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            hint->setText("Checking the selected monitor connection…");
            auto *watcher = new QFutureWatcher<Discovery>(&dialog);
            connect(watcher, &QFutureWatcherBase::finished, &dialog, [&, watcher] {
                discovery_ = watcher->result();
                watcher->deleteLater();
                dialog.accept();
            });
            watcher->setFuture(QtConcurrent::run(&worker_, DiscoverDesktopMonitors));
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        const bool accepted = dialog.exec() == QDialog::Accepted;
        if (checkingConnection)
            setBusy(false);
        if (!accepted)
            return;
        QString id;
        const bool newMonitor = choice->currentData().metaType().id() != QMetaType::QString;
        if (!newMonitor)
            id = choice->currentData().toString();
        else
        {
            const auto &picked = choices.at(choice->currentData().toInt());
            // Resolve the exact route the user saw, including session-only identities.
            const auto resolved = ResolveMonitorConnection(picked, discovery_);
            if (!resolved.available || (method->currentData() == "lg" && !SupportsLgAlternate(resolved.target)))
            {
                message("The selected monitor connection changed. Refresh and select the monitor again.");
                return;
            }
            const auto target = resolved.target;
            SavedMonitor m;
            m.id = NewDesktopId();
            m.name = target.label.section(" · ", 0, 0);
            m.route = target;
            int suffix = 2;
            const auto base = m.name;
            auto exists = [&] {
                for (const auto &other : draft_.monitors)
                    if (other.name == m.name)
                        return true;
                return false;
            };
            while (exists())
                m.name = base + " " + QString::number(suffix++);
            int matches = 0;
            for (const auto &candidate : discovery_.monitors)
                if (!target.identity.isEmpty() && candidate.identity == target.identity)
                    ++matches;
            if (matches == 1)
                m.identity = target.identity;
            m.method = method->currentData().toString();
            m.inputs = DefaultMonitorInputs(m.method, target.inputProfile);
            confirmed_[m.id] = target;
            id = m.id;
            draft_.monitors.push_back(m);
        }
        if (assignToProfile)
        {
            active().monitors.push_back(id);
            for (auto &a : active().actions)
            {
                a.inputs[id] = {};
                for (const auto &input : FindMonitor(draft_, id)->inputs)
                    if (InputNameKey(a.name) == InputNameKey(input.label))
                        a.inputs[id] = input.id;
            }
            for (auto &action : active().actions)
                if (action.inputs.value(id).isEmpty()) action.enabled = false;
        }
        populateProfile();
        if (!assignToProfile)
        {
            const QSignalBlocker block(monitorTabs_);
            for (int tab = 0; tab < monitorTabs_->count(); ++tab)
                if (monitorTabs_->tabData(tab).toString() == id)
                {
                    monitorTabs_->setCurrentIndex(tab);
                    selectedMonitorId_ = id;
                }
            populateMonitorInspector();
        }
        dirty();
        if (newMonitor)
            scanInputs(false, {id});
    }
    void editMonitor(const QString &id)
    {
        captureDraft();
        const auto *original = FindMonitor(draft_, id);
        if (!original || monitorEditor_ || busy_)
            return;
        auto resolve = [this, id] {
            if (const auto *savedMonitor = monitor(id))
                return ResolveSavedMonitor(*savedMonitor, discovery_, confirmed_);
            return MonitorResolution{false, {}, "Monitor no longer available."};
        };
        auto commit = [this, id](const SavedMonitor &next, QString &problem) {
            auto apply = [&](DesktopWorkspace &workspace) {
                auto found = std::find_if(workspace.monitors.begin(), workspace.monitors.end(),
                                          [&](const SavedMonitor &m) { return m.id == id; });
                if (found == workspace.monitors.end()) workspace.monitors.append(next);
                else *found = next;
                for (auto &profile : workspace.profiles)
                    for (auto &action : profile.actions)
                        if (!action.inputs.value(id).isEmpty() && !FindMonitorInput(next, action.inputs.value(id)))
                        {
                            action.inputs[id] = {};
                            action.missingInputs.remove(id);
                            action.autoDisabled = false;
                            action.enabled = false;
                        }
            };
            captureDraft();
            auto candidate = saved_;
            apply(candidate);
            if (!commitSection(candidate)) {
                problem = status_->text();
                return false;
            }
            apply(draft_);
            refreshDirtyState();
            closeMonitorEditor();
            populateProfile();
            message("Monitor inputs saved.");
            return true;
        };
        monitorEditor_ = new MonitorInputEditor(monitorEditorHost_, *original, draft_, resolve, &worker_,
                                                preview_, commit, [this] { closeMonitorEditor(); });
        monitorEditorHost_->layout()->addWidget(monitorEditor_);
        updateActions();
    }
    void closeMonitorEditor()
    {
        if (!monitorEditor_) return;
        monitorEditor_->hide();
        monitorEditor_->deleteLater();
        monitorEditor_ = nullptr;
        updateActions();
    }
    bool leaveMonitorEditor()
    {
        if (!monitorEditor_) return true;
        if (monitorEditor_->hasChanges())
        {
            const auto choice = QMessageBox::question(this, "Save input changes?",
                "Keep your changes to this monitor’s inputs?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (choice == QMessageBox::Cancel) return false;
            if (choice == QMessageBox::Save)
            {
                monitorEditor_->saveChanges();
                return !monitorEditor_;
            }
        }
        closeMonitorEditor();
        return true;
    }
    bool monitorIsAssigned(const QString &id) const
    {
        return std::any_of(draft_.profiles.cbegin(), draft_.profiles.cend(),
                           [&id](const MonitorProfile &profile) { return profile.monitors.contains(id); });
    }
    void deleteUnusedMonitor(const QString &id)
    {
        if (monitorIsAssigned(id))
            return;
        const auto *selected = FindMonitor(draft_, id);
        if (!selected || QMessageBox::question(this, "Delete monitor?",
                "Delete the saved settings for “" + selected->name + "”?") != QMessageBox::Yes)
            return;
        draft_.monitors.removeIf([&id](const SavedMonitor &monitor) { return monitor.id == id; });
        confirmed_.remove(id);
        brightness_.remove(id);
        lastSentInput_.remove(id);
        dirty();
        updateActions();
    }
    void save()
    {
        if (busy_)
        {
            message("Wait for monitor input detection to finish before saving.");
            return;
        }
        if (preview_)
        {
            message("Design preview · nothing is saved and no monitor commands are sent.");
            return;
        }
        captureDraft();
        QString error;
        if (configurationBlocked_ || !SaveDesktopWorkspace(draft_, error))
        {
            message(error.isEmpty() ? "Saving is blocked to preserve the unreadable configuration." : error);
            return;
        }
        session_.configurationChanged(saved_, draft_);
        lastResults_.clear();
        lastSentInput_.clear();
        saved_ = draft_;
        beforeActions_.clear();
        beforeAssignments_.clear();
        config_ = saved_.preferences;
        dirty_ = false;
        save_->setText("Saved");
        message("Profiles saved. Each shortcut controls its named profile, whichever tab is open.");
        registerHotkeys(true);
        showResults();
        updateActions();
        refreshVisibleBrightness();
#ifdef Q_OS_WIN
        if (!SetStartWithWindows(config_.startup))
            message("Profiles saved, but Windows startup could not be updated.");
#else
        if (!SetDesktopAutostart(config_.startup, error))
        {
            config_.startup = DesktopAutostartEnabled();
            saved_.preferences.startup = draft_.preferences.startup = config_.startup;
            const QSignalBlocker blocker(startup_);
            startup_->setChecked(config_.startup);
            message("Profiles saved, but startup could not be updated. " + error);
        }
#endif
    }
    void syncShortcut(const QString &profileId, const QString &actionId, const QKeySequence &key)
    {
        const auto text = key.toString(QKeySequence::PortableText).toUpper();
        auto binding = [&](MonitorProfile &profile) -> QString * {
            if (actionId == "cycle") return &profile.cycleShortcut;
            if (actionId == "previous") return &profile.previousShortcut;
            for (auto &action : profile.actions)
                if (action.id == actionId) return &action.shortcut;
            return nullptr;
        };
        QString previous;
        for (auto &profile : saved_.profiles)
            if (profile.id == profileId)
                if (auto *value = binding(profile)) {
                    previous = *value;
                    *value = text;
                }
        // Cancel should restore the current Plasma binding, including changes
        // received while an unrelated field or another profile was being edited.
        for (auto *snapshots : {&beforeActions_, &beforeAssignments_})
        {
            auto it = snapshots->find(profileId);
            if (it != snapshots->end())
                if (auto *value = binding(it.value())) *value = text;
        }
        for (auto &profile : draft_.profiles)
        {
            if (profile.id != profileId) continue;
            auto *value = binding(profile);
            if (!value || (*value != previous && dirty_)) break;
            QKeySequenceEdit *editor = nullptr;
            QTableWidgetItem *cell = nullptr;
            if (profileId == activeId_)
            {
                if (actionId == "cycle") editor = cycleShortcut_;
                else if (actionId == "previous") editor = previousShortcut_;
                else
                    for (int row = 0; row < table_->rowCount(); ++row)
                        if (table_->item(row, 0)->data(Qt::UserRole) == actionId)
                        {
                            const int column = 2 + profile.monitors.size();
                            editor = qobject_cast<QKeySequenceEdit *>(table_->cellWidget(row, column));
                            cell = table_->item(row, column);
                            break;
                        }
            }
            if (editor)
            {
                if (dirty_ && editor->keySequence() != QKeySequence::fromString(previous, QKeySequence::PortableText))
                    break;
                const QSignalBlocker block(editor);
                editor->setKeySequence(key);
            }
            if (cell)
            {
                const QSignalBlocker block(table_);
                cell->setText(key.toString(QKeySequence::NativeText));
                cell->setData(Qt::UserRole, text);
            }
            *value = text;
            break;
        }
        QString error;
        if (!preview_ && !configurationBlocked_ && !registeringShortcuts_ &&
            !SaveDesktopWorkspace(saved_, error))
            message(error);
    }
    QString shortcutId(const MonitorProfile &p, const QString &actionId) const
    {
        return p.id == "default" ? actionId : "profile-" + p.id + "-" + actionId;
    }
    void registerHotkeys(bool applyEdits = false)
    {
        if (preview_ || configurationBlocked_)
            return;
        const QScopedValueRollback<bool> registering(registeringShortcuts_, true);
#ifdef Q_OS_WIN
        Q_UNUSED(applyEdits);
        for (const auto &item : hotkeys_)
            UnregisterHotKey(nullptr, item.first);
        hotkeys_.clear();
        int id = 1;
        QStringList failed;
        auto bind = [&](const MonitorProfile &p, const QString &actionId, const QString &label,
                        const QString &text) {
            if (text.isEmpty())
                return;
            HotkeySpec spec{};
            if (SafeDesktopShortcut(text) && ParseHotkey(text.toStdString(), spec) &&
                RegisterHotKey(nullptr, id, spec.fsModifiers | MOD_NOREPEAT, spec.vk))
                hotkeys_[id] = {p.id, actionId};
            else
                failed.push_back(p.name + " → " + label);
            ++id;
        };
#else
        kdeShortcuts_.clear();
        if (!kdeAvailable_)
            return;
        QStringList failed;
        auto bind = [&](const MonitorProfile &p, const QString &actionId, const QString &label,
                        const QString &text) {
            const auto profileId = p.id;
            if (!SafeDesktopShortcut(text))
            {
                failed.push_back(p.name + " → " + label + " (add Ctrl, Alt or Meta)");
                return;
            }
            if (!kdeShortcuts_.bind(
                    shortcutId(p, actionId), p.name + " → " + label,
                    QKeySequence::fromString(text, QKeySequence::PortableText),
                    [this, profileId, actionId] { sendInput(profileId, actionId); },
                    [this, profileId, actionId](const QKeySequence &actual) {
                        syncShortcut(profileId, actionId, actual);
                    },
                    applyEdits, config_.globalShortcuts))
                failed.push_back(p.name + " → " + label);
        };
#endif
        // Callbacks can update saved keys synchronously: iterate a stable copy.
        const auto profiles = saved_.profiles;
        for (const auto &p : profiles)
        {
            bind(p, "cycle", "Cycle inputs", p.cycleShortcut);
            bind(p, "previous", "Previous input", p.previousShortcut);
            for (const auto &a : p.actions)
                if (a.enabled)
                    bind(p, a.id, a.name, a.shortcut);
        }
#ifndef Q_OS_WIN
        if (config_.globalShortcuts && failed.isEmpty())
            kdeShortcuts_.pruneInactive();
#endif
        if (!failed.isEmpty())
            message("Shortcut unavailable: " + failed.join(", ") +
                    ". Choose another shortcut; existing assignments were not stolen.");
    }
    bool canUseProfile(const QString &id) const
    {
        if (busy_ || configurationBlocked_) return false;
        if (!dirty_) return FindProfile(saved_, id) != nullptr;
        return !beforeActions_.contains(id) && !beforeAssignments_.contains(id) &&
               SameProfileCommands(saved_, draft_, id);
    }
    void sendInput(const QString &profileId, QString actionId, bool retry = false)
    {
        if (preview_)
        {
            message("Design preview · switching is disabled.");
            return;
        }
        if (busy_ || recordingShortcut_ || configurationBlocked_)
            return;
        if (!canUseProfile(profileId))
        {
            message("Save the profile or its monitor changes before switching.");
            showWindow();
            return;
        }
        const auto *profile = FindProfile(saved_, profileId);
        if (!profile)
            return;
        if (lastSend_.isValid() && lastSend_.elapsed() < config_.debounce)
            return;
        if (actionId == "cycle")
            actionId = session_.cycle(*profile);
        if (actionId == "previous")
            actionId = session_.previous(profileId);
        const auto pending = session_.state(profileId).pending;
        QStringList only;
        if (retry)
        {
            if (pending.monitors.isEmpty() || pending.failedMonitors().isEmpty())
                return;
            actionId = pending.actionId;
            only = pending.failedMonitors();
        }
        bool exists = false;
        for (const auto &a : profile->actions)
            if (a.enabled && a.id == actionId)
                exists = true;
        if (!exists)
        {
            message("No previous action is known yet. Send two different actions first.");
            return;
        }
        sendAction(saved_, profileId, actionId, only, retry, profile->name);
    }
    void sendIndividual(const QString &monitorId, const QString &inputId)
    {
        const auto *definition = FindMonitor(saved_, monitorId);
        const auto *input = definition ? FindMonitorInput(*definition, inputId) : nullptr;
        if (!input || !canSwitchInput(monitorId, inputId) || recordingShortcut_)
            return;
        if (preview_)
        {
            message("Design preview · " + definition->name + " → " + input->label);
            return;
        }
        if (lastSend_.isValid() && lastSend_.elapsed() < config_.debounce)
            return;
        auto snapshot = saved_;
        snapshot.profiles.push_back({{}, definition->name, {monitorId},
                                     {{"direct", input->label, {}, true, {{monitorId, inputId}}}}});
        sendAction(snapshot, {}, "direct", {}, false, definition->name);
    }
    void sendAction(const DesktopWorkspace &snapshot, const QString &profileId, const QString &actionId,
                    const QStringList &only, bool retry, const QString &label)
    {
        const auto confirmed = confirmed_;
        lastSend_.restart();
        setBusy(true);
        message("Sending profile commands…");
        auto *watcher = new QFutureWatcher<ProfileSwitchResult>(this);
        connect(watcher, &QFutureWatcher<ProfileSwitchResult>::finished, this,
                [this, watcher, snapshot, retry, label] {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    discovery_ = result.discovery;
                    populateMembers();
                    session_.accept(snapshot, result, retry);
                    for (const auto &entry : result.monitors)
                        if (entry.result.sent())
                            lastSentInput_[entry.monitorId] = entry.inputId;
                    auto display = result;
                    if (retry && lastResults_.contains(result.profileId))
                    {
                        display = lastResults_.value(result.profileId);
                        for (const auto &entry : result.monitors)
                            for (auto &old : display.monitors)
                                if (old.monitorId == entry.monitorId)
                                    old = entry;
                    }
                    for (auto it = lastResults_.begin(); it != lastResults_.end();)
                    {
                        const auto *other = FindProfile(snapshot, it.key());
                        bool overlaps = false;
                        if (other && it.key() != result.profileId)
                            for (const auto &entry : result.monitors)
                                if (entry.result.sent() && other->monitors.contains(entry.monitorId))
                                    overlaps = true;
                        if (overlaps)
                            it = lastResults_.erase(it);
                        else
                            ++it;
                    }
                    if (!result.profileId.isEmpty())
                        lastResults_[result.profileId] = display;
                    // Record the actual current attempt, not unsaved UI fields, and
                    // report every target without custom labels or identity hashes.
                    lastCommand_ = ProfileCommandDiagnostics(snapshot, result);
                    int sent = 0;
                    for (const auto &m : result.monitors)
                        if (m.result.sent())
                            ++sent;
                    setBusy(false);
                    showResults();
                    const auto text =
                        QString("%1: %2/%3 command(s) sent. %4")
                            .arg(label)
                            .arg(sent)
                            .arg(result.monitors.size())
                            .arg(result.complete()
                                     ? "Physical input changes are unverified."
                                     : result.profileId.isEmpty() && !result.monitors.isEmpty()
                                         ? result.monitors.front().status
                                         : "See per-monitor results; retry failed monitors when ready.");
                    message(text);
                    notificationPage_.clear();
                    if (config_.notifications && tray_->isVisible())
                        tray_->showMessage("LG Input Switch", text,
                                           result.complete() ? QSystemTrayIcon::Information
                                                             : QSystemTrayIcon::Warning);
                });
        watcher->setFuture(QtConcurrent::run(&worker_, [snapshot, profileId, actionId, confirmed, only] {
            // Re-enumerate immediately before dispatch; never send to an index
            // that now belongs to a different monitor after reconnect.
            return SendProfileAction(snapshot, profileId, actionId, {}, confirmed, only, SendDesktopInput,
                                     DiscoverDesktopMonitors);
        }));
    }
    void showResults()
    {
        const auto state = session_.state(activeId_);
        const auto result = !state.pending.monitors.isEmpty() ? state.pending : ProfileSwitchResult{};
        // Successful results also remain visible when returning to a tab.
        const auto display = lastResults_.contains(activeId_) ? lastResults_.value(activeId_) : result;
        QStringList lines;
        for (const auto &entry : display.monitors)
        {
            const auto *m = FindMonitor(saved_, entry.monitorId);
            const auto *input = m ? FindMonitorInput(*m, entry.inputId) : nullptr;
            lines.push_back((m ? m->name : "Monitor") + " → " + (input ? input->label : "Input") + ": " +
                            entry.status);
        }
        results_->setPlainText(lines.join('\n'));
        results_->setVisible(!lines.isEmpty());
        updateActions();
    }
    QMap<QString, ProfileSwitchResult> lastResults_;
    void scanInputs(bool discover, const QStringList &only = {})
    {
        if (busy_ || preview_)
            return;
        captureDraft();
        const auto snapshot = draft_;
        const auto known = discovery_;
        const auto confirmed = confirmed_;
        setBusy(true);
        message(discover ? "Finding monitors and reading their inputs…" : "Reading this monitor’s inputs…");
        auto *watcher = new QFutureWatcher<MonitorSetup>(this);
        connect(watcher, &QFutureWatcher<MonitorSetup>::finished, this, [this, watcher] {
            const auto setup = watcher->result();
            watcher->deleteLater();
            discovery_ = setup.discovery;
            bool changed = false, unavailable = false;
            for (const auto &entry : setup.inputs)
            {
                changed = ApplyMonitorInputs(draft_, entry.first, entry.second) || changed;
                unavailable = unavailable || !entry.second.complete;
            }
            if (changed)
                dirty();
            populateProfile();
            setBusy(false);
            refreshVisibleBrightness();
            message(
                changed ? "Monitor input lists updated. Save settings to apply them. Actions for missing "
                          "inputs are disabled for review."
                : unavailable
                    ? "Some monitors did not report a usable input list. Their existing mappings were kept."
                : setup.inputs.isEmpty() ? discovery_.message
                                         : "Each monitor’s input list is up to date.");
        });
        watcher->setFuture(QtConcurrent::run(&worker_, [snapshot, known, confirmed, discover, only] {
            MonitorSetup setup;
            setup.discovery = discover ? DiscoverDesktopMonitors() : known;
            for (const auto &m : snapshot.monitors)
            {
                if (!m.autoInputs || (!only.isEmpty() && !only.contains(m.id)))
                    continue;
                const auto resolved = ResolveSavedMonitor(m, setup.discovery, confirmed);
                if (resolved.available)
                    setup.inputs.push_back({m.id, ReadMonitorInputs(m, resolved.target)});
            }
            return setup;
        }));
    }
    void refresh()
    {
        scanInputs(true);
    }
    DiagnosticSnapshot diagnosticSnapshot()
    {
        captureDraft();
        const auto *selected = FindProfile(draft_, diagnosticProfile_->currentData().toString());
        const auto &profile = selected ? *selected : active();
        DiagnosticSnapshot snapshot;
        snapshot.unsaved = dirty_;
        snapshot.lastCommand = lastCommand_;
        snapshot.profileCount = draft_.profiles.size();
        for (const auto &id : profile.monitors)
            if (const auto *m = FindMonitor(draft_, id))
            {
                const auto resolution = ResolveSavedMonitor(*m, discovery_, confirmed_);
                DesktopConfig c = draft_.preferences;
                c.adapter = resolution.available ? resolution.target.adapter : m->route.adapter;
                c.display = resolution.available ? resolution.target.display : m->route.display;
                c.bus = resolution.available ? resolution.target.bus : m->route.bus;
                c.method = m->method;
                c.source = m->method == "standard" ? "0x51" : m->source;
                c.inputs = m->inputs;
                const auto route = resolution.available ? resolution.target : m->route;
                c.transport = route.transport;
                c.nativeDisplay = route.nativeDisplay;
                c.targetIdentity = route.identity;
                int index = -1;
                for (int i = 0; i < draft_.monitors.size(); ++i)
                    if (draft_.monitors[i].id == id)
                        index = i;
                snapshot.monitors.push_back({c, resolution.available, index, route.hardware,
                                             route.inputProfile, m->autoInputs});
            }
        for (const auto &action : profile.actions)
        {
            QJsonArray targets;
            for (int i = 0; i < draft_.monitors.size(); ++i)
            {
                const auto &m = draft_.monitors[i];
                if (!action.inputs.contains(m.id))
                    continue;
                const auto *input = FindMonitorInput(m, action.inputs.value(m.id));
                unsigned int code;
                targets.append(
                    QJsonObject{{"monitor_index", i},
                                {"feature", m.method == "standard" ? "0x60" : "0xf4"},
                                {"input", input && ParseInputValue(input->code.toStdString(), 0xffff, code)
                                              ? QString("0x%1").arg(code, 4, 16, QChar('0'))
                                              : "invalid"}});
            }
            snapshot.actions.append(QJsonObject{{"enabled", action.enabled}, {"targets", targets}});
        }
        if (!snapshot.monitors.isEmpty())
        {
            snapshot.config = snapshot.monitors.front().config;
            snapshot.selected = snapshot.monitors.front().selected;
        }
        return snapshot;
    }
    void diagnostics()
    {
        if (busy_)
            return;
        const auto snapshot = diagnosticSnapshot();
        diagnosticReport_->show();
        diagnosticReport_->setPlainText("Reading diagnostic information…");
        QTimer::singleShot(0, this, [this] { scrollToDiagnostics(); });
        diagnosticCopy_->setEnabled(false);
        diagnosticSave_->setEnabled(false);
        setBusy(true);
        message("Reading monitor information. No input-switching commands are sent.");
        auto *watcher = new QFutureWatcher<QJsonObject>(this);
        connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, snapshot] {
            const auto checks = watcher->result();
            watcher->deleteLater();
            diagnosticReport_->setPlainText(FormatDesktopDiagnostics(snapshot, checks, preview_));
            setBusy(false);
            diagnosticCopy_->setEnabled(true);
            diagnosticSave_->setEnabled(true);
            message("Diagnostic report ready. Review it, then copy or save it to share.");
        });
        watcher->setFuture(QtConcurrent::run(&worker_, [snapshot, preview = preview_] {
            return CollectDesktopDiagnostics(snapshot, preview);
        }));
    }
    bool hasSavedConnection(const QString &id) const
    {
        const auto *saved = FindMonitor(saved_, id);
        const auto *draft = FindMonitor(draft_, id);
        return saved && draft && saved->identity == draft->identity &&
               MonitorRouteKey(saved->route) == MonitorRouteKey(draft->route) &&
               (!saved->identity.isEmpty() || saved->route.label == draft->route.label);
    }
    bool canSwitchInput(const QString &id, const QString &inputId) const
    {
        if (busy_ || configurationBlocked_ || !hasSavedConnection(id)) return false;
        const auto *saved = FindMonitor(saved_, id);
        const auto *draft = FindMonitor(draft_, id);
        const auto *original = FindMonitorInput(*saved, inputId);
        const auto *current = FindMonitorInput(*draft, inputId);
        return original && current && original->enabled && current->enabled &&
            original->code == current->code && saved->source == draft->source && saved->method == draft->method;
    }
    void refreshVisibleBrightness()
    {
        QStringList ids;
        if (quickPanel_ && quickPanel_->isVisible())
            for (const auto &m : saved_.monitors)
                ids.append(m.id);
        else if (tabs_->currentIndex() == 1)
            ids.append(selectedMonitorId_);
        updateBrightness(ids);
    }
    void updateBrightness(QStringList ids, int percent = -1)
    {
        ids.removeIf([this](const QString &id) { return !hasSavedConnection(id); });
        if (busy_ || configurationBlocked_ || ids.isEmpty())
            return;
        if (preview_)
        {
            if (percent >= 0)
                brightness_[ids.front()] = {percent, {}};
            updateActions();
            return;
        }
        const auto snapshot = saved_;
        const auto confirmed = confirmed_;
        // Keep driver calls serialized. A switch can cancel the unread part of a
        // tray refresh and proceed as soon as the current driver call finishes.
        cancelBrightnessRead();
        std::shared_ptr<std::atomic_bool> cancelled;
        if (percent < 0)
            backgroundBrightness_ = cancelled = std::make_shared<std::atomic_bool>(false);
        else
            setBusy(true);
        using BrightnessRead = QPair<Discovery, QMap<QString, MonitorBrightness>>;
        auto *watcher = new QFutureWatcher<BrightnessRead>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, ids, percent, cancelled] {
            watcher->deleteLater();
            if (cancelled && cancelled->load())
                return;
            const auto [live, values] = watcher->result();
            if (cancelled)
                backgroundBrightness_.reset();
            discovery_ = live;
            populateMembers();
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                brightness_[it.key()] = it.value();
            if (percent < 0)
                updateActions();
            else
            {
                setBusy(false);
                const auto value = values.value(ids.front());
                message(value.status.isEmpty() ? QString("Brightness set to %1%.").arg(value.percent) : value.status);
            }
        });
        watcher->setFuture(QtConcurrent::run(&worker_, [snapshot, confirmed, ids, percent, cancelled] {
            QMap<QString, MonitorBrightness> results;
            if (cancelled && cancelled->load())
                return BrightnessRead{};
            const auto live = DiscoverDesktopMonitors();
            for (const auto &id : ids)
            {
                if (cancelled && cancelled->load())
                    break;
                const auto *definition = FindMonitor(snapshot, id);
                if (!definition)
                    continue;
                const auto resolved = ResolveSavedMonitor(*definition, live, confirmed);
                results[id] = resolved.available ? AccessMonitorBrightness(resolved.target, percent)
                                                 : MonitorBrightness{-1, resolved.reason};
            }
            return BrightnessRead{live, results};
        }));
    }
    void addMonitorControls(QVBoxLayout *layout, const SavedMonitor &m, bool quick)
    {
        const auto connection = ResolveSavedMonitor(m, discovery_, confirmed_);
        const bool savedConnection = hasSavedConnection(m.id);
        const auto value = savedConnection ? brightness_.value(m.id)
            : MonitorBrightness{-1, "Save the monitor connection to adjust brightness."};
        auto *row = new QWidget;
        auto *brightnessRow = new QHBoxLayout(row);
        brightnessRow->setContentsMargins(0, 0, 0, 0);
        auto *label = displayLabel("Brightness", "muted");
        brightnessRow->addWidget(label);
        if (!quick)
            brightnessRow->addStretch();
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setObjectName("brightness-" + m.id);
        slider->setProperty("monitorBrightness", true);
        slider->setAccessibleName(m.name + " brightness");
        slider->setRange(0, 100);
        slider->setTracking(false);
        slider->setValue(qMax(0, value.percent));
        slider->setEnabled(connection.available && !busy_ && !configurationBlocked_ && value.percent >= 0);
        if (quick)
            brightnessRow->addWidget(slider, 1);
        auto *level = displayLabel(value.percent >= 0 ? QString("%1%").arg(value.percent) : "—", "brightnessValue");
        level->setWordWrap(false);
        level->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        level->setMinimumWidth(36);
        brightnessRow->addWidget(level);
        row->setToolTip(value.status.isEmpty() && value.percent < 0
                            ? QString("Brightness has not been read.") : value.status);
        const auto addBrightness = [&] {
            layout->addWidget(row);
            if (!quick)
                layout->addWidget(slider);
        };
        addBrightness();
        if (!quick)
        {
            auto *heading = new QHBoxLayout;
            heading->addWidget(displayLabel("Inputs", "section"));
            if (!monitorEditor_)
            {
                auto *edit = new QPushButton;
                edit->setObjectName("editSavedMonitor");
                configureIconButton(edit, "Edit inputs");
                edit->setEnabled(!busy_ && !configurationBlocked_);
                connect(edit, &QPushButton::clicked, this, [this, id = m.id] { editMonitor(id); });
                heading->addWidget(edit);
            }
            heading->addStretch();
            heading->addWidget(displayLabel(m.autoInputs ? "Automatic detection" : "Custom mapping", "muted"));
            layout->addWidget(divider(Qt::Horizontal));
            layout->addLayout(heading);
        }
        connect(slider, &QSlider::sliderMoved, level, [level](int v) { level->setText(QString("%1%").arg(v)); });
        connect(slider, &QSlider::valueChanged, this, [this, id = m.id](int v) { updateBrightness({id}, v); });
        if (!quick && monitorEditor_) return;
        auto *buttons = new QWidget;
        buttons->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto *grid = new QGridLayout(buttons);
        grid->setContentsMargins(0, 0, 0, 0);
        int index = 0;
        for (const auto &input : m.inputs)
        {
            if (!input.enabled)
                continue;
            auto *button = new QPushButton(quick ? input.label : "Switch");
            button->setMinimumHeight(32);
            button->setSizePolicy(quick ? QSizePolicy::Ignored : QSizePolicy::Preferred, QSizePolicy::Fixed);
            button->setToolTip(input.label);
            button->setAccessibleName(m.name + " · " + input.label);
            button->setObjectName("direct-input");
            button->setProperty("lastSent", lastSentInput_.value(m.id) == input.id);
            button->setEnabled(connection.available && canSwitchInput(m.id, input.id));
            if (quick) grid->addWidget(button, index / 2, index % 2);
            else {
                grid->addWidget(displayLabel(input.label, "section"), index, 0);
                grid->setColumnStretch(0, 1);
                button->setProperty("switchAction", true);
                grid->addWidget(button, index, 1);
            }
            ++index;
            connect(button, &QPushButton::clicked, this,
                    [this, id = m.id, key = input.id] { sendIndividual(id, key); });
        }
        if (index)
            layout->addWidget(buttons);
        else
            delete buttons;
        if (!quick)
        {
            if (!value.status.isEmpty())
            {
                auto *detail = new QLabel(value.status);
                detail->setTextFormat(Qt::PlainText);
                detail->setWordWrap(true);
                layout->addWidget(detail);
            }
        }
    }
    void populateMonitorInspector()
    {
        if (!monitorInspector_)
            return;
        auto *layout = static_cast<QVBoxLayout *>(monitorInspector_->layout());
        clearPanel(layout);
        const auto *m = FindMonitor(draft_, monitorTabs_->tabData(monitorTabs_->currentIndex()).toString());
        if (!m)
            return;
        auto *heading = new QWidget;
        auto *header = new QHBoxLayout(heading);
        header->setContentsMargins(0, 0, 0, 0);
        auto *identity = new QVBoxLayout;
        identity->setSpacing(2);
        const auto resolved = ResolveSavedMonitor(*m, discovery_, confirmed_);
        identity->addWidget(displayLabel(resolved.available ? resolved.target.label : resolved.reason, "muted"));
        header->addLayout(identity, 1);
        header->addWidget(displayLabel(resolved.available ? "● Connected" : "Connection needed", "muted"));
        layout->addWidget(heading);
        auto *toolbar = new QWidget;
        auto *actions = new QHBoxLayout(toolbar);
        actions->setContentsMargins(0, 0, 0, 0);
        if (!resolved.available)
        {
            auto *reconnect = new QPushButton("Connect…");
            connect(reconnect, &QPushButton::clicked, this, [this, id = m->id] { reconnectMonitor(id); });
            actions->addWidget(reconnect);
        }
        else
        {
            auto *connection = new QToolButton;
            connection->setText("Connection");
            connection->setPopupMode(QToolButton::InstantPopup);
            auto *menu = new QMenu(connection);
            connection->setMenu(menu);
            connect(menu->addAction("Choose another connection…"), &QAction::triggered,
                    this, [this, id = m->id] { reconnectMonitor(id); });
            actions->addWidget(connection);
        }
        actions->addStretch();
        layout->addWidget(divider(Qt::Horizontal));
        addMonitorControls(layout, *m, false);
        if (monitorEditor_) { delete toolbar; return; }
        auto *read = new QPushButton("Refresh brightness");
        read->setEnabled(resolved.available && hasSavedConnection(m->id) && !busy_ && !configurationBlocked_);
        connect(read, &QPushButton::clicked, this, [this, id = m->id] { updateBrightness({id}); });
        actions->insertWidget(0, read);
        if (m->autoInputs)
        {
            auto *detect = new QPushButton("Detect inputs");
            detect->setEnabled(resolved.available && !busy_ && !configurationBlocked_);
            connect(detect, &QPushButton::clicked, this, [this, id = m->id] { scanInputs(false, {id}); });
            actions->insertWidget(0, detect);
        }
        layout->addWidget(divider(Qt::Horizontal));
        auto *more = new QToolButton;
        more->setText("Advanced");
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        more->setArrowType(Qt::RightArrow);
        more->setCheckable(true);
        const bool expanded = advancedConnectionOpen_ || !resolved.available;
        toolbar->setVisible(expanded);
        more->setChecked(expanded);
        more->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        connect(more, &QToolButton::toggled, toolbar, [this, toolbar, more](bool open) {
            advancedConnectionOpen_ = open;
            toolbar->setVisible(open);
            more->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        });
        layout->addWidget(more, 0, Qt::AlignLeft);
        layout->addWidget(toolbar);
        if (!monitorIsAssigned(m->id))
        {
            auto *remove = new QPushButton("Delete unused monitor…");
            connect(remove, &QPushButton::clicked, this, [this, id = m->id] { deleteUnusedMonitor(id); });
            layout->addWidget(remove, 0, Qt::AlignLeft);
        }
        layout->addStretch();
    }
    QString lastInput(const SavedMonitor &m) const
    {
        const auto *input = FindMonitorInput(m, lastSentInput_.value(m.id));
        return input ? input->label : QString{};
    }
    static QLabel *displayLabel(const QString &text, const char *style = "")
    {
        auto *label = new QLabel;
        label->setTextFormat(Qt::PlainText);
        label->setText(text);
        label->setObjectName(style);
        label->setWordWrap(true);
        return label;
    }
    void populateMonitorTabs()
    {
        if (!monitorTabs_)
            return;
        const QSignalBlocker blocker(monitorTabs_);
        const auto selected = selectedMonitorId_;
        while (monitorTabs_->count())
            monitorTabs_->removeTab(0);
        int selectedTab = 0;
        for (const auto &monitor : draft_.monitors)
        {
            const int tab = monitorTabs_->addTab(monitor.name);
            monitorTabs_->setTabData(tab, monitor.id);
            monitorTabs_->setTabToolTip(tab, monitor.name);
            if (monitor.id == selected)
                selectedTab = tab;
        }
        const int add = monitorTabs_->addTab("+");
        monitorTabs_->setAccessibleTabName(add, "Add monitor");
        monitorTabs_->setTabToolTip(add, "Add connected monitor");
        monitorTabs_->setTabEnabled(add, !busy_ && draft_.monitors.size() < 16);
        monitorTabs_->setCurrentIndex(selectedTab);
        selectedMonitorId_ = monitorTabs_->tabData(selectedTab).toString();
        populateMonitorInspector();
    }
    void moveAction(int from, int to)
    {
        if (!editProfile_->isChecked() || from < 0 || to < 0 ||
            from >= table_->rowCount() || to >= table_->rowCount() || from == to) return;
        captureDraft();
        active().actions.move(from, to);
        dirty();
        populateProfile();
        table_->setCurrentCell(to, 1);
    }
    void refreshDirtyState()
    {
        auto baseline = saved_;
        baseline.activeProfile = draft_.activeProfile;
        dirty_ = WorkspaceJson(draft_) != WorkspaceJson(baseline);
        save_->setText(dirty_ ? "Save changes" : "Saved");
    }
    bool commitSection(const DesktopWorkspace &candidate)
    {
        QString error;
        if (busy_ || configurationBlocked_)
        {
            message(busy_ ? "Wait for monitor detection to finish, then save again."
                          : "Saving is blocked because the configuration could not be read.");
            return false;
        }
        if (!(preview_ ? ValidateWorkspace(candidate, error) : SaveDesktopWorkspace(candidate, error)))
        {
            message(error);
            return false;
        }
        session_.configurationChanged(saved_, candidate);
        saved_ = candidate;
        refreshDirtyState();
        registerHotkeys(true);
        return true;
    }
    bool saveActiveProfile()
    {
        // A newly created profile may also introduce monitor definitions.
        auto candidate = saved_;
        const auto &p = active();
        for (const auto &id : p.monitors)
            if (!FindMonitor(candidate, id))
                candidate.monitors.append(*FindMonitor(draft_, id));
        auto it = std::find_if(candidate.profiles.begin(), candidate.profiles.end(),
                               [&](const MonitorProfile &other) { return other.id == p.id; });
        if (it == candidate.profiles.end()) candidate.profiles.append(p);
        else *it = p;
        return commitSection(candidate);
    }
    void finishAssignmentEdit(bool commit)
    {
        captureDraft();
        auto &p = active();
        if (commit && !saveActiveProfile()) return;
        if (!commit && beforeAssignments_.contains(p.id))
        {
            const auto initial = beforeAssignments_.value(p.id);
            p.monitors = initial.monitors;
            p.actions = initial.actions;
        }
        beforeAssignments_.remove(activeId_);
        const QSignalBlocker block(monitorDetails_);
        monitorDetails_->setChecked(false);
        editProfile_->setEnabled(true);
        refreshDirtyState();
        populateProfile();
        message(commit ? "Monitor assignments saved." : "Assignment edits cancelled.");
    }
    void finishActionEdit(bool commit)
    {
        captureDraft();
        auto &p = active();
        if (commit && !saveActiveProfile()) return;
        if (!commit && beforeActions_.contains(p.id))
        {
            const auto initial = beforeActions_.value(p.id);
            p.actions = initial.actions;
            p.cycleShortcut = initial.cycleShortcut;
            p.previousShortcut = initial.previousShortcut;
        }
        beforeActions_.remove(activeId_);
        const QSignalBlocker block(editProfile_);
        editProfile_->setChecked(false);
        refreshDirtyState();
        populateProfile();
        message(commit ? "Actions saved." : "Action edits cancelled.");
    }
    static void clearPanel(QLayout *layout)
    {
        while (auto *item = layout->takeAt(0))
        {
            if (auto *widget = item->widget())
            {
                widget->hide();
                widget->setParent(nullptr);
                widget->deleteLater();
            }
            else if (auto *nested = item->layout())
                clearPanel(nested);
            delete item;
        }
    }
    static QWidget *divider(Qt::Orientation orientation)
    {
        auto *line = new QWidget;
        line->setObjectName("workspaceDivider");
        if (orientation == Qt::Horizontal)
            line->setFixedHeight(1);
        else
            line->setFixedWidth(1);
        return line;
    }
    void populateQuickPanel()
    {
        clearPanel(quickItems_);
        auto heading = [this](const QString &name) {
            auto *label = new QLabel(name);
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            label->setObjectName("trayHeading");
            quickItems_->addWidget(label);
        };
        auto separator = [this] {
            quickItems_->addSpacing(5);
            quickItems_->addWidget(divider(Qt::Horizontal));
            quickItems_->addSpacing(5);
        };
        bool first = true;
        for (const auto &m : saved_.monitors)
        {
            if (!first)
                separator();
            first = false;
            heading(m.name);
            addMonitorControls(quickItems_, m, true);
        }
        bool grouped = false;
        const auto isGroup = [](const MonitorProfile &p) {
            return p.monitors.size() > 1 && std::any_of(p.actions.cbegin(), p.actions.cend(),
                [](const ProfileAction &a) { return a.enabled; });
        };
        const auto groups = std::count_if(saved_.profiles.cbegin(), saved_.profiles.cend(), isGroup);
        for (const auto &p : saved_.profiles)
        {
            if (!isGroup(p))
                continue;
            if (!grouped)
            {
                separator();
                heading("Switch monitors together");
                grouped = true;
            }
            auto *row = new QWidget;
            auto *grid = new QGridLayout(row);
            grid->setContentsMargins(0, 0, 0, 0);
            int index = 0;
            for (const auto &a : p.actions)
            {
                if (!a.enabled)
                    continue;
                auto *button = new QPushButton(a.name);
                button->setMinimumHeight(32);
                button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                button->setToolTip(p.name);
                button->setAccessibleName(p.name + " · " + a.name);
                button->setObjectName("group-input");
                button->setEnabled(canUseProfile(p.id));
                grid->addWidget(button, index / 2, index % 2);
                ++index;
                connect(button, &QPushButton::clicked, this,
                        [this, profileId = p.id, actionId = a.id] { sendInput(profileId, actionId); });
            }
            if (groups > 1)
                heading(p.name);
            quickItems_->addWidget(row);
        }
        quickItems_->addStretch();
        quickItems_->parentWidget()->ensurePolished();
        quickItems_->invalidate();
        quickItems_->activate();
    }
    void showQuickPanel()
    {
        if (quickPanel_->isVisible())
        {
            quickPanel_->hide();
            return;
        }
        quickStatus_->clear();
        const auto trayGeometry = tray_->geometry();
        const auto anchor = trayGeometry.isValid() ? trayGeometry.center() : QCursor::pos();
        populateQuickPanel();
        quickPanel_->ensurePolished();
        auto *screen = QGuiApplication::screenAt(anchor);
        const auto area = (screen ? screen : QGuiApplication::primaryScreen())->availableGeometry();
        const int width = qMin(420, area.width());
        const int height = qMin(qMax(220, quickItems_->sizeHint().height() + 78), area.height());
        quickPanel_->resize(width, height);
        const QRect icon = trayGeometry.isValid() ? trayGeometry : QRect(anchor, QSize(1, 1));
        quickPanel_->openAtTray(TrayQuickPanel::menuPosition(area, icon, {width, height}));
        if (!preview_)
            refreshVisibleBrightness();
    }
    void updateActions()
    {
        if (!menu_)
            return;
        const bool usable = canUseProfile(activeId_);
        const bool inlineEditing = editProfile_->isChecked() || monitorDetails_->isChecked() || monitorEditor_;
        save_->setEnabled(!inlineEditing && !busy_ && !configurationBlocked_ &&
                          (dirty_ || tabs_->currentIndex() == 2));
        save_->setToolTip(inlineEditing ? "Save or cancel the open editor first." : QString{});
        bool hasActions = false;
        for (const auto &action : active().actions)
            if (action.enabled)
                hasActions = true;
        cycle_->setEnabled(usable && hasActions);
        cycle_->setToolTip(active().cycleShortcut.isEmpty() ? "Cycle enabled actions"
                                                            : "Cycle shortcut: " + active().cycleShortcut);
        previous_->setToolTip(active().previousShortcut.isEmpty()
                                  ? "Return to the previous accepted action"
                                  : "Previous shortcut: " + active().previousShortcut);
        previous_->setEnabled(usable && !session_.previous(activeId_).isEmpty());
        const bool hasFailures = !session_.state(activeId_).pending.failedMonitors().isEmpty();
        retry_->setVisible(hasFailures);
        retry_->setEnabled(usable && hasFailures);
        removeProfile_->setEnabled(!busy_ && draft_.profiles.size() > 1);
        addMonitor_->setEnabled(!busy_ && active().monitors.size() < 16);
        populateMonitorTabs();
        if (quickPanel_ && quickPanel_->isVisible())
            populateQuickPanel();
        if (!editProfile_->isChecked())
            for (int row = 0; row < table_->rowCount(); ++row)
                if (auto *button = table_->cellWidget(row, table_->columnCount() - 1))
                {
                    button->setEnabled(usable);
                    const bool last = session_.state(activeId_).currentAction == table_->item(row, 0)->data(Qt::UserRole);
                    button->setToolTip(last ? "Last action sent" : button->accessibleName());
                    const QSignalBlocker block(table_);
                    auto *name = table_->item(row, 1);
                    auto font = name->font();
                    font.setBold(last);
                    name->setFont(font);
                }
        for (auto *action : menu_->actions())
            if (action->menu())
                action->menu()->deleteLater();
        menu_->clear();
        connect(menu_->addAction("Open LG Input Switch"), &QAction::triggered, this,
                [this] { showWindow(); });
        menu_->addSeparator();
        for (const auto &p : saved_.profiles)
        {
            const bool profileUsable = canUseProfile(p.id);
            auto *submenu = menu_->addMenu(p.name);
            auto bind = [&](const QString &label, const QString &id, bool enabled) {
                auto *a = submenu->addAction(label);
                a->setEnabled(profileUsable && enabled);
                connect(a, &QAction::triggered, this,
                        [this, profileId = p.id, id] { sendInput(profileId, id); });
            };
            bool enabledActions = false;
            for (const auto &a : p.actions)
                if (a.enabled)
                    enabledActions = true;
            bind("Cycle inputs", "cycle", enabledActions);
            bind("Previous input", "previous", !session_.previous(p.id).isEmpty());
            const auto pending = session_.state(p.id).pending;
            auto *retry = submenu->addAction("Retry failed monitors");
            retry->setEnabled(profileUsable && !pending.failedMonitors().isEmpty());
            connect(retry, &QAction::triggered, this, [this, id = p.id] { sendInput(id, {}, true); });
            submenu->addSeparator();
            for (const auto &a : p.actions)
                if (a.enabled)
                    bind(a.name, a.id, true);
        }
        menu_->addSeparator();
        connect(menu_->addAction("Quit"), &QAction::triggered, this, [this] { quit(); });
    }
    void quit()
    {
        if (busy_)
        {
            message("Wait for the current monitor operation to finish.");
            showWindow();
            return;
        }
        if ((dirty_ || monitorEditor_) && QMessageBox::question(this, "Unsaved changes", "Quit without saving your changes?") !=
                          QMessageBox::Yes)
            return;
        quitting_ = true;
        qApp->quit();
    }
    void theme()
    {
        // Reapply style rules after the palette changes. Qt caches palette()
        // references when it polishes a widget.
        qApp->setStyleSheet({});
        const auto mode = appearance_->currentData().toString();
        // Qt fills unresolved roles from the current platform palette, including
        // desktop theme changes while the app is running.
        QPalette palette;
        palette.setResolveMask(0);
        if (mode != "system")
        {
            const bool dark = mode == "dark";
            const QColor window(dark ? "#191a18" : "#f8f7f3");
            const QColor surface(dark ? "#232421" : "#efeee8");
            const QColor text(dark ? "#efefe9" : "#242824");
            const QColor muted(dark ? "#adada3" : "#515a52");
            palette.setColor(QPalette::Window, window);
            palette.setColor(QPalette::WindowText, text);
            palette.setColor(QPalette::Base, window);
            palette.setColor(QPalette::AlternateBase, surface);
            palette.setColor(QPalette::Text, text);
            palette.setColor(QPalette::Button, surface);
            palette.setColor(QPalette::ButtonText, text);
            palette.setColor(QPalette::Mid, QColor(dark ? "#393b35" : "#d5d7cd"));
            palette.setColor(QPalette::PlaceholderText, muted);
            palette.setColor(QPalette::ToolTipBase, surface);
            palette.setColor(QPalette::ToolTipText, text);
            for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
                palette.setColor(QPalette::Disabled, role, muted);
        }
        if (!systemAccent_->isChecked())
        {
            for (auto group : {QPalette::Active, QPalette::Inactive})
            {
                palette.setColor(group, QPalette::Highlight, IconBlue);
                palette.setColor(group, QPalette::HighlightedText, Qt::white);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
                palette.setColor(group, QPalette::Accent, IconBlue);
#endif
            }
        }
        qApp->setPalette(palette);
        applyWorkspaceStyle();
    }

    void scrollToDiagnostics()
    {
        settingsScroll_->verticalScrollBar()->setValue(diagnosticsSection_->y() - 10);
    }

    void applyWorkspaceStyle()
    {
        const auto palette = qApp->palette();
        const auto base = palette.color(QPalette::Window);
        const auto accent = palette.color(QPalette::Highlight);
        const auto blend = [base](const QColor &color, double amount) {
            return QColor::fromRgbF(base.redF() + (color.redF() - base.redF()) * amount,
                                   base.greenF() + (color.greenF() - base.greenF()) * amount,
                                   base.blueF() + (color.blueF() - base.blueF()) * amount).name();
        };
        const auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        const auto sheet = QString(R"QSS(
            QWidget#root, QWidget#workspaceHeader, QDialog#quickPanel { background: palette(window); }
            QWidget#workspaceFooter { background: %2; }
            QDialog#quickPanel { border: 1px solid palette(mid); }
            QLabel#title { font-size: 14px; font-weight: 700; }
            QLabel#pageTitle { font-size: 28px; font-weight: 700; }
            QLabel#columnHeading, QLabel#trayHeading { font-size: 14px; font-weight: 700; }
            QLabel#section { font-size: 14px; font-weight: 600; }
            QLabel#muted, QLabel#status { color: palette(placeholder-text); font-size: 12px; }
            QLabel#shortcutKey, QLabel#brightnessValue { font: 12px "%3"; }
            QLabel#shortcutKey, QLabel#actionArrow { color: palette(placeholder-text); }
            QLabel#actionArrow { font-size: 16px; }
            QFrame#card { background: palette(window); border: 0; }
            QWidget#workspaceDivider { background: palette(mid); }
            QScrollArea, QTabWidget::pane { border: 0; }
            QTabBar[workspaceTabs="true"]::tab {
                min-width: 22px; padding: 8px 12px; border: 1px solid transparent;
                border-bottom: 2px solid transparent; color: palette(placeholder-text);
            }
            QTabBar[workspaceTabs="true"]::tab:hover { background: %4; }
            QTabBar[workspaceTabs="true"]::tab:selected {
                background: %1; color: palette(window-text);
            }
            QTabBar[workspaceTabs="true"]::tab:last { font-size: 16px; font-weight: 600; }
            QTabBar[workspaceTabs="true"]::tab:focus { border-color: palette(highlight); }
            QListWidget#workspaceNavigation {
                background: %2; border: 0; border-right: 1px solid palette(mid); padding: 14px 10px;
            }
            QListWidget#workspaceNavigation::item {
                color: palette(placeholder-text); padding: 9px 12px; border-left: 2px solid transparent;
            }
            QListWidget#workspaceNavigation::item:hover { background: %4; }
            QListWidget#workspaceNavigation::item:selected {
                 background: %1; color: palette(window-text); font-weight: 600;
            }
            QTreeWidget[workspaceTable="true"] { border: 0; border-bottom: 1px solid palette(mid); }
            QTreeWidget[workspaceTable="true"]::item { padding: 5px 10px; border-bottom: 1px solid palette(mid); }
            QTreeWidget[workspaceTable="true"]::item:selected { background: %1; color: palette(window-text); }
            QWidget#root QPushButton, QWidget#root QToolButton, QDialog#quickPanel QPushButton {
                background: transparent; border: 1px solid palette(mid); border-radius: 3px; padding: 5px 10px;
            }
            QComboBox[workspacePicker="true"] {
                background: palette(window); border: 1px solid palette(mid); border-radius: 3px;
                padding: 5px 6px 5px 9px;
            }
            QComboBox[workspacePicker="true"]::drop-down { border: 0; width: 22px; }
            QComboBox[workspacePicker="true"]:hover, QComboBox[workspacePicker="true"]:focus,
            QWidget#root QPushButton:focus, QWidget#root QToolButton:focus,
            QDialog#quickPanel QPushButton:focus { border-color: palette(highlight); }
            QWidget#root QPushButton:hover:enabled, QWidget#root QToolButton:hover:enabled,
            QDialog#quickPanel QPushButton:hover:enabled { background: %4; }
            QWidget#root QPushButton#direct-input[lastSent=true],
            QDialog#quickPanel QPushButton#direct-input[lastSent=true] {
                background: %1;
            }
            QWidget#root QPushButton#textAction {
                border: 1px solid transparent; border-radius: 0; background: transparent; padding: 0;
            }
            QWidget#root QPushButton#textAction { padding: 1px 4px; }
            QWidget#root QPushButton#textAction:hover {
                background: %4;
            }
            QWidget#root QPushButton#textAction:focus {
                border-color: palette(highlight);
            }
            QWidget#root QPushButton#primary:enabled, QWidget#root QPushButton[switchAction=true]:enabled {
                background: palette(highlight); color: palette(highlighted-text);
                border: 1px solid palette(highlight); font-weight: 600;
            }
            QWidget#root QPushButton#primary:focus { border-color: palette(window-text); }
            QWidget#root QPushButton[iconOnly=true], QWidget#root QToolButton[iconOnly=true] {
                border: 1px solid transparent; padding: 3px;
            }
            QWidget#root QPushButton[iconOnly=true]:hover, QWidget#root QToolButton[iconOnly=true]:hover { background: %4; }
            QWidget#root QPushButton[iconOnly=true]:focus, QWidget#root QToolButton[iconOnly=true]:focus {
                border-color: palette(highlight);
            }
            QSlider[monitorBrightness="true"] { min-height: 18px; }
            QSlider[monitorBrightness="true"]::groove:horizontal { height: 3px; background: palette(mid); }
            QSlider[monitorBrightness="true"]::sub-page:horizontal:enabled { background: palette(highlight); }
            QSlider[monitorBrightness="true"]::handle:horizontal {
                background: palette(window-text); border: 1px solid palette(window); width: 10px; margin: -5px 0;
            }
            QSlider[monitorBrightness="true"]::handle:horizontal:disabled { background: palette(mid); }
            QSlider[monitorBrightness="true"]::handle:horizontal:focus { border-color: palette(highlight); }
            QTableWidget { gridline-color: palette(mid); }
            QTableWidget#profileMonitors, QTableWidget#inputs {
                background: palette(window); color: palette(window-text); border: 0;
            }
            QTableWidget#inputs::item { border-bottom: 1px solid palette(mid); padding: 3px; }
            QHeaderView::section {
                background: %2; color: palette(placeholder-text); padding: 8px;
                border: 0; border-bottom: 1px solid palette(mid);
            }
            QToolButton[compactMenu="true"]::menu-indicator { image: none; }
        )QSS").arg(blend(accent, 0.12), blend(palette.color(QPalette::WindowText), 0.04),
                     fixedFont, blend(palette.color(QPalette::WindowText), 0.08));
        if (sheet != qApp->styleSheet()) qApp->setStyleSheet(sheet);
        if (auto *mark = findChild<QLabel *>("workspaceMark"))
            mark->setPixmap(WorkspaceMark(palette.color(QPalette::WindowText)));
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
            QTimer::singleShot(0, this, &DesktopWindow::applyWorkspaceStyle);
        return QWidget::eventFilter(watched, event);
    }

  protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (preview_ || quitting_)
        {
            event->accept();
            return;
        }
        if (tray_->isVisible() && QSystemTrayIcon::isSystemTrayAvailable())
        {
            hide();
            event->ignore();
        }
        else
        {
            event->ignore();
            quit();
        }
    }
    bool nativeEventFilter(const QByteArray &, void *raw, qintptr *) override
    {
#ifdef Q_OS_WIN
        auto *msg = static_cast<MSG *>(raw);
        if (msg->message == WM_HOTKEY)
        {
            auto found = hotkeys_.find(int(msg->wParam));
            if (found != hotkeys_.end())
            {
                sendInput(found->second.first, found->second.second);
                return true;
            }
        }
#else
        Q_UNUSED(raw);
#endif
        return false;
    }

  public:
    bool shouldShowAtStartup(bool background, bool trayAvailable) const
    {
        return preview_ || configurationBlocked_ || !background || !trayAvailable;
    }
    explicit DesktopWindow(bool preview, const QString &component = KdeShortcuts::applicationComponent())
        : kdeShortcuts_(component), preview_(preview), menu_(nullptr)
    {
        QString warning;
        WorkspaceLoadStatus loadStatus = WorkspaceLoadStatus::Migrated;
        saved_ = preview ? MigrateDesktopConfig(DesktopConfig{}) : LoadDesktopWorkspace(warning, {}, &loadStatus);
        configurationBlocked_ = loadStatus == WorkspaceLoadStatus::Blocked;
        if (preview)
        {
            saved_.monitors[0].route = {"LG UltraGear · Design preview", 0, 1, true, QString(64, 'a')};
            saved_.monitors[0].identity = QString(64, 'a');
            saved_.preferences.globalShortcuts = true;
            saved_.monitors[0].name = "Main monitor";
            auto side = saved_.monitors[0];
            side.id = "preview-side";
            side.name = "Side monitor";
            side.method = "standard";
            for (auto &input : side.inputs)
            {
                if (input.label == "DisplayPort")
                    input.code = "0x0f";
                else if (input.label == "HDMI1")
                    input.code = "0x11";
                else if (input.label == "HDMI2")
                    input.code = "0x12";
            }
            side.route = {"Dell · I²C 9", 0, 9, true, QString(64, 'b')};
            side.identity = QString(64, 'b');
            saved_.monitors.push_back(side);
            auto solo = saved_.profiles[0];
            solo.id = "preview-side-profile";
            solo.name = "Side monitor";
            solo.monitors = {side.id};
            solo.cycleShortcut.clear();
            solo.previousShortcut.clear();
            for (auto &a : solo.actions)
            {
                a.shortcut.clear();
                a.inputs = {{side.id, a.inputs.value("monitor-1")}};
            }
            saved_.profiles.push_back(solo);
            MonitorProfile group;
            group.id = "preview-group";
            group.name = "Both monitors";
            group.monitors = {"monitor-1", side.id};
            group.actions = {{"linux-pc",
                              "Linux PC",
                              "CTRL+ALT+7",
                              true,
                              {{"monitor-1", side.inputs[0].id}, {side.id, side.inputs[2].id}}},
                             {"windows-pc",
                              "Windows PC",
                              "CTRL+ALT+8",
                              true,
                              {{"monitor-1", side.inputs[2].id}, {side.id, side.inputs[0].id}}}};
            saved_.profiles.push_back(group);
            discovery_.monitors = {saved_.monitors[0].route, side.route};
            brightness_ = {{"monitor-1", {62, {}}}, {side.id, {48, {}}}};
        }
        draft_ = saved_;
        config_ = saved_.preferences;
        activeId_ = draft_.activeProfile;
        worker_.setMaxThreadCount(1);
        setObjectName("root");
        setWindowTitle("LG Input Switch");
        resize(1000, 740);
        setMinimumSize(620, 460);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *topBar = new QWidget;
        topBar->setObjectName("workspaceHeader");
        auto *brand = new QHBoxLayout(topBar);
        brand->setContentsMargins(16, 8, 16, 8);
        auto *logo = new QLabel;
        logo->setObjectName("workspaceMark");
        auto *title = new QLabel("LG Input Switch");
        title->setObjectName("title");
        title->setFixedHeight(29);
        brand->addWidget(logo);
        brand->addWidget(title, 1);
        headerAppearance_ = new WorkspacePicker;
        headerAppearance_->setAccessibleName("Appearance");
        headerAppearance_->addItem("System", "system");
        headerAppearance_->addItem("Light", "light");
        headerAppearance_->addItem("Dark", "dark");
        brand->addWidget(displayLabel("Appearance", "muted"));
        brand->addWidget(headerAppearance_);
        layout->addWidget(topBar);
        layout->addWidget(divider(Qt::Horizontal));
        controls_ = new QWidget;
        auto *body = new QHBoxLayout(controls_);
        body->setContentsMargins(0, 0, 24, 0);
        body->setSpacing(24);
        tabs_ = new QTabWidget;
        tabs_->setObjectName("mainTabs");
        tabs_->tabBar()->hide();
        navigation_ = new QListWidget;
        navigation_->setObjectName("workspaceNavigation");
        navigation_->setAccessibleName("Workspace navigation");
        navigation_->setFixedWidth(165);
        navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        body->addWidget(navigation_);
        body->addWidget(tabs_, 1);
        auto addPage = [this](QWidget *page, const QString &name) {
            tabs_->addTab(page, name);
            navigation_->addItem(name);
        };
        connect(navigation_, &QListWidget::currentRowChanged, tabs_, &QTabWidget::setCurrentIndex);
        connect(tabs_, &QTabWidget::currentChanged, this, [this](int page) { navigation_->setCurrentRow(page); });
        auto scrollPage = [](QWidget *page, const QString &name) {
            auto *scroll = new QScrollArea;
            scroll->setObjectName(name);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setMinimumSize(0, 0);
            scroll->setWidget(page);
            return scroll;
        };
        auto *profiles = new QWidget;
        auto *profileLayout = new QVBoxLayout(profiles);
        profileLayout->setContentsMargins(0, 16, 0, 0);
        profileLayout->setSpacing(14);
        profileLayout->setSizeConstraint(QLayout::SetMinimumSize);
        profileTabs_ = new QTabBar;
        profileTabs_->setObjectName("profileTabs");
        profileTabs_->setProperty("workspaceTabs", true);
        profileTabs_->setAccessibleName("Switching profiles");
        profileTabs_->setExpanding(false);
        profileTabs_->setUsesScrollButtons(true);
        profileTabs_->setElideMode(Qt::ElideRight);
        profileTabs_->setDrawBase(false);
        auto *profileToolbar = new QHBoxLayout;
        profileToolbar->addWidget(profileTabs_, 1);
        editProfile_ = new QPushButton;
        editProfile_->setCheckable(true);
        editProfile_->setObjectName("editProfile");
        configureIconButton(editProfile_, "Edit actions");
        monitorDetails_ = new QToolButton;
        monitorDetails_->setCheckable(true);
        monitorDetails_->setObjectName("monitorDetails");
        configureIconButton(monitorDetails_, "Edit assigned monitors");
        auto *profileOptions = new QToolButton;
        profileOptions->setText("…");
        profileOptions->setProperty("compactMenu", true);
        profileOptions->setFixedWidth(32);
        profileOptions->setAccessibleName("Profile options");
        auto *profileMenu = new QMenu(profileOptions);
        renameProfile_ = profileMenu->addAction("Rename profile…");
        removeProfile_ = profileMenu->addAction("Remove profile");
        profileOptions->setMenu(profileMenu);
        profileOptions->setPopupMode(QToolButton::InstantPopup);
        profileToolbar->addWidget(profileOptions);
        profileLayout->addWidget(displayLabel("Input Profiles", "pageTitle"));
        auto *profileStrip = new QVBoxLayout;
        profileStrip->setSpacing(0);
        profileStrip->addLayout(profileToolbar);
        profileStrip->addWidget(divider(Qt::Horizontal));
        profileLayout->addLayout(profileStrip);

        monitorCard_ = new QFrame;
        monitorCard_->setObjectName("card");
        auto *monitorLayout = new QVBoxLayout(monitorCard_);
        monitorLayout->setContentsMargins(0, 4, 0, 0);
        auto *head = new QHBoxLayout;
        monitorSummary_ = new QLabel("Assigned monitors");
        monitorSummary_->setObjectName("section");
        head->addWidget(monitorSummary_);
        head->addWidget(monitorDetails_);
        head->addStretch();
        monitorLayout->addLayout(head);
        monitorTools_ = new QWidget;
        auto *monitorToolsLayout = new QHBoxLayout(monitorTools_);
        monitorToolsLayout->setContentsMargins(0, 0, 0, 0);
        addMonitor_ = new QPushButton("Add to profile…");
        addMonitor_->setObjectName("addMonitor");
        monitorToolsLayout->addWidget(addMonitor_);
        monitorToolsLayout->addStretch();
        auto *cancelMembers = new QPushButton("Cancel");
        cancelMembers->setAccessibleName("Cancel assignment edits");
        auto *saveMembers = new QPushButton("Save monitors");
        saveMembers->setObjectName("primary");
        monitorToolsLayout->addWidget(cancelMembers);
        monitorToolsLayout->addWidget(saveMembers);
        connect(saveMembers, &QPushButton::clicked, this, [this] { finishAssignmentEdit(true); });
        connect(cancelMembers, &QPushButton::clicked, this, [this] { finishAssignmentEdit(false); });
        members_ = new ContentTable(0, 2);
        members_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        members_->setObjectName("profileMonitors");
        members_->verticalHeader()->hide();
        members_->horizontalHeader()->hide();
        members_->setShowGrid(false);
        members_->setFrameShape(QFrame::NoFrame);
        members_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        members_->setSelectionMode(QAbstractItemView::NoSelection);
        members_->setColumnWidth(0, 150);

        members_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        monitorLayout->addWidget(members_);
        monitorLayout->addWidget(monitorTools_);
        profileLayout->addWidget(monitorCard_);
        auto *inputs = new QFrame;
        inputs->setObjectName("card");
        auto *inputLayout = new QVBoxLayout(inputs);
        inputLayout->setContentsMargins(0, 0, 0, 4);
        auto *actionHeader = new QHBoxLayout;
        actionHeader->addWidget(displayLabel("Actions", "section"));
        actionHeader->addWidget(editProfile_);
        actionHeader->addStretch();
        inputLayout->addLayout(actionHeader);
        table_ = new ContentTable;
        table_->setObjectName("inputs");
        table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table_->verticalHeader()->hide();
        table_->setShowGrid(false);
        table_->setSelectionMode(QAbstractItemView::NoSelection);
        connect(table_, &QTableWidget::cellClicked, this, [this](int row, int column) {
            if (!editProfile_->isChecked() && canUseProfile(activeId_) && column < table_->columnCount() - 1)
                sendInput(activeId_, table_->item(row, 0)->data(Qt::UserRole).toString());
        });
        auto *grips = table_->verticalHeader();
        grips->setSectionsMovable(true);
        grips->setFirstSectionMovable(true);
        grips->setFixedWidth(28);
        grips->setCursor(Qt::OpenHandCursor);
        connect(grips, &QHeaderView::sectionMoved, this, [this, grips](int logical, int from, int to) {
            // The model owns order, including shortcuts and mappings. Return the
            // view to logical order before rendering the reordered model.
            const QSignalBlocker block(grips);
            grips->moveSection(to, from);
            moveAction(logical, to);
        });
        for (const int delta : {-1, 1})
        {
            auto *key = new QShortcut(delta < 0 ? QKeySequence("Alt+Up") : QKeySequence("Alt+Down"), table_);
            key->setContext(Qt::WidgetWithChildrenShortcut);
            connect(key, &QShortcut::activated, this, [this, delta] {
                moveAction(table_->currentRow(), table_->currentRow() + delta);
            });
        }
        table_->setFrameShape(QFrame::NoFrame);
        table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        inputLayout->addWidget(table_);
        editTools_ = new QWidget;
        auto *actions = new QHBoxLayout(editTools_);
        actions->setContentsMargins(0, 0, 0, 0);
        auto *addAction = new QPushButton("+ Add action");
        addAction->setObjectName("addAction");
        auto *cancelActions = new QPushButton("Cancel");
        cancelActions->setAccessibleName("Cancel action edits");
        auto *saveActions = new QPushButton("Save actions");
        saveActions->setObjectName("primary");
        actions->addWidget(addAction);
        actions->addStretch();
        actions->addWidget(cancelActions);
        actions->addWidget(saveActions);
        connect(cancelActions, &QPushButton::clicked, this, [this] { finishActionEdit(false); });
        connect(saveActions, &QPushButton::clicked, this, [this] { finishActionEdit(true); });
        shortcutPanel_ = new QWidget;
        auto *shortcuts = new QFormLayout(shortcutPanel_);
        shortcuts->setContentsMargins(0, 6, 0, 6);
        cycleShortcut_ = new QKeySequenceEdit;
        cycleShortcut_->setObjectName("cycleShortcut");
        cycleShortcut_->setMaximumSequenceLength(1);
        previousShortcut_ = new QKeySequenceEdit;
        previousShortcut_->setObjectName("previousShortcut");
        previousShortcut_->setMaximumSequenceLength(1);
        shortcuts->addRow("Cycle shortcut", cycleShortcut_);
        shortcuts->addRow("Previous shortcut", previousShortcut_);
        inputLayout->addWidget(shortcutPanel_);
        inputLayout->addWidget(editTools_);
        results_ = new QPlainTextEdit;
        results_->setObjectName("switchResults");
        results_->setReadOnly(true);
        results_->setMaximumHeight(94);
        results_->setAccessibleName("Per-monitor switch results");
        inputLayout->addWidget(results_);
        auto *switches = new QHBoxLayout;
        retry_ = new QPushButton("Retry failed monitors");
        retry_->setObjectName("retryFailed");
        previous_ = new QPushButton("Previous input");
        cycle_ = new QPushButton("Cycle inputs");
        switches->addWidget(retry_);
        switches->addWidget(previous_);
        switches->addWidget(cycle_);
        switches->addStretch();
        inputLayout->addLayout(switches);
        profileLayout->addWidget(inputs);
        profileLayout->addStretch(1);
        profileScroll_ = scrollPage(profiles, "profileScroll");
        addPage(profileScroll_, "Input Profiles");

        auto *monitorPage = new QWidget;
        auto *monitorPageLayout = new QVBoxLayout(monitorPage);
        monitorPageLayout->setContentsMargins(0, 16, 0, 0);
        monitorPageLayout->setSpacing(14);
        monitorPageLayout->setSizeConstraint(QLayout::SetMinimumSize);
        auto *monitorHeading = new QHBoxLayout;
        auto *monitorTitle = new QLabel("Monitors");
        monitorTitle->setObjectName("pageTitle");
        monitorHeading->addWidget(monitorTitle, 1);
        auto *refreshMonitors = new QPushButton("Refresh");
        refreshMonitors->setObjectName("refreshMonitors");
        connect(refreshMonitors, &QPushButton::clicked, this, [this] {
            if (leaveMonitorEditor()) refresh();
        });
        monitorHeading->addWidget(refreshMonitors);
        monitorPageLayout->addLayout(monitorHeading);
        monitorTabs_ = new QTabBar;
        monitorTabs_->setObjectName("monitorTabs");
        monitorTabs_->setAccessibleName("Saved monitors");
        monitorTabs_->setProperty("workspaceTabs", true);
        monitorTabs_->setExpanding(false);
        monitorTabs_->setUsesScrollButtons(true);
        monitorTabs_->setElideMode(Qt::ElideRight);
        monitorTabs_->setDrawBase(false);
        auto *monitorStrip = new QVBoxLayout;
        monitorStrip->setSpacing(0);
        monitorStrip->addWidget(monitorTabs_);
        monitorStrip->addWidget(divider(Qt::Horizontal));
        monitorPageLayout->addLayout(monitorStrip);
        monitorInspector_ = new QWidget;
        auto *inspectorLayout = new QVBoxLayout(monitorInspector_);
        inspectorLayout->setContentsMargins(0, 0, 0, 0);
        inspectorLayout->setSpacing(12);
        inspectorLayout->setSizeConstraint(QLayout::SetMinimumSize);
        monitorPageLayout->addWidget(monitorInspector_);
        monitorEditorHost_ = new QWidget;
        auto *editorLayout = new QVBoxLayout(monitorEditorHost_);
        editorLayout->setContentsMargins(0, 0, 0, 0);
        monitorPageLayout->addWidget(monitorEditorHost_);
        monitorPageLayout->addStretch();
        addPage(scrollPage(monitorPage, "monitorsScroll"), "Monitors");
        connect(monitorTabs_, &QTabBar::currentChanged, this, [this](int index) {
            if (index < 0)
                return;
            const auto target = monitorTabs_->tabData(index).toString();
            if (monitorEditor_)
            {
                // Restore the current tab while validating. Closing the editor rebuilds the strip.
                populateMonitorTabs();
                if (!leaveMonitorEditor()) return;
            }
            if (target.isEmpty())
            {
                populateMonitorTabs();
                addMonitor(false);
                return;
            }
            selectedMonitorId_ = target;
            populateMonitorTabs();
            refreshVisibleBrightness();
        });
        connect(tabs_, &QTabWidget::currentChanged, this, [this] {
            updateActions();
            refreshVisibleBrightness();
        });

        auto *preferences = new QFrame;
        preferences->setObjectName("card");
        auto *settingsLayout = new QVBoxLayout(preferences);
        settingsLayout->setContentsMargins(0, 16, 0, 12);
        settingsLayout->setSpacing(12);
        auto *settingsHeader = new QHBoxLayout;
        settingsHeader->addWidget(displayLabel("Settings", "pageTitle"), 1);
        auto *jumpToDiagnostics = new QPushButton("Diagnostics ↓");
        jumpToDiagnostics->setObjectName("textAction");
        settingsHeader->addWidget(jumpToDiagnostics);
        settingsLayout->addLayout(settingsHeader);
        auto settingsHeading = [settingsLayout](const QString &name) {
            settingsLayout->addSpacing(7);
            settingsLayout->addWidget(divider(Qt::Horizontal));
            settingsLayout->addWidget(displayLabel(name, "columnHeading"));
        };
        auto settingsForm = [settingsLayout] {
            auto *form = new QFormLayout;
            form->setSpacing(12);
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
            settingsLayout->addLayout(form);
            return form;
        };
        settingsHeading("Appearance and startup");
        auto *appearanceForm = settingsForm();
        appearance_ = new WorkspacePicker;
        appearance_->setObjectName("appearance");
        appearance_->addItem("Follow system", "system");
        appearance_->addItem("Light", "light");
        appearance_->addItem("Dark", "dark");
        appearance_->setCurrentIndex(qMax(0, appearance_->findData(config_.appearance)));
        appearanceForm->addRow("Theme", appearance_);
        systemAccent_ = new QCheckBox("Use system accent color");
        systemAccent_->setObjectName("systemAccent");
        systemAccent_->setChecked(config_.systemAccent);
        appearanceForm->addRow(systemAccent_);
        headerAppearance_->setCurrentIndex(appearance_->currentIndex());
        connect(headerAppearance_, &QComboBox::currentIndexChanged, appearance_, &QComboBox::setCurrentIndex);
        connect(appearance_, &QComboBox::currentIndexChanged, headerAppearance_, &QComboBox::setCurrentIndex);
        notifications_ = new QCheckBox("Show notifications");
        notifications_->setChecked(config_.notifications);
        appearanceForm->addRow(notifications_);
        startup_ = new QCheckBox("Launch when I sign in");
        startup_->setObjectName("startup");
        startup_->setToolTip("Starts in the system tray. Without a tray, the window opens instead.");
        startup_->setChecked(config_.startup);
        appearanceForm->addRow(startup_);
        settingsHeading("Input controls");
        auto *inputForm = settingsForm();
        globalShortcuts_ = new QCheckBox("Enable global shortcuts while the app is running");
        globalShortcuts_->setObjectName("globalShortcuts");
        globalShortcuts_->setChecked(config_.globalShortcuts);
#ifdef Q_OS_WIN
        globalShortcuts_->hide();
#else
        if (kdeAvailable_ || preview_)
            inputForm->addRow(globalShortcuts_);
        else
            globalShortcuts_->hide();
#endif
        inputForm->addRow(displayLabel("Edit shortcuts with the pencil beside Actions in Input Profiles.",
                                        "muted"));
#ifndef Q_OS_WIN
        if (!kdeAvailable_ && !preview_)
            inputForm->addRow(displayLabel("Global shortcuts require KDE Plasma with KGlobalAccel.", "muted"));
#endif
        debounce_ = new QSpinBox;
        debounce_->setRange(0, 10000);
        debounce_->setSuffix(" ms");
        debounce_->setValue(config_.debounce);
        inputForm->addRow("Shortcut debounce", debounce_);
        codes_ = new QCheckBox("Show codes beside input names");
        inputForm->addRow(codes_);
        settingsLayout->addSpacing(7);
        settingsLayout->addWidget(divider(Qt::Horizontal));
#ifdef LG_DESKTOP_TEST
        auto *updates = new UpdatePanel(true, this);
#else
        auto *updates = new UpdatePanel(preview_, this);
#endif
        settingsLayout->addWidget(updates);
        updates->notify = [this, preferences](const QString &text) {
            if (config_.notifications && tray_ && tray_->isVisible()) {
                notificationPage_ = preferences;
                tray_->showMessage("LG Input Switch", text, QSystemTrayIcon::Information, 10000);
            }
        };
        updates->prepareInstall = [this] {
            if (busy_) { message("Wait for the current monitor operation to finish before updating."); return false; }
            if (monitorEditor_) { message("Save or cancel the monitor input edits before updating."); return false; }
            captureDraft();
            if (dirty_) {
                const auto choice = QMessageBox::question(this, "Install update", "Save your changes before updating?",
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
                if (choice == QMessageBox::Cancel) return false;
                if (choice == QMessageBox::Save) { save(); if (dirty_) return false; }
            }
            return true;
        };
        updates->installBusy = [this](bool value) { setBusy(value); tabs_->setEnabled(!value); };
        updates->quitForUpdate = [this] { quitting_ = true; qApp->quit(); };
        settingsLayout->addSpacing(7);
        settingsLayout->addWidget(divider(Qt::Horizontal));
        diagnosticsSection_ = new QWidget;
        auto *diagnosticsLayout = new QVBoxLayout(diagnosticsSection_);
        diagnosticsLayout->setContentsMargins(0, 0, 0, 0);
        diagnosticsLayout->setSpacing(10);
        diagnosticsLayout->addWidget(displayLabel("Diagnostics", "columnHeading"));
        auto *diagnosticTarget = new QHBoxLayout;
        diagnosticTarget->addWidget(new QLabel("Report for"));
        diagnosticProfile_ = new WorkspacePicker;
        diagnosticProfile_->setObjectName("diagnosticProfile");
        diagnosticProfile_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        diagnosticTarget->addWidget(diagnosticProfile_);
        diagnosticTarget->addStretch();
        diagnosticsLayout->addLayout(diagnosticTarget);
        auto *diagnosticsHint = new QLabel(
            "Create a local report for all monitors assigned to this profile, including per-monitor command "
            "results and advertised inputs. No settings are changed or uploaded. Review before sharing.");
        diagnosticsHint->setWordWrap(true);
        diagnosticsLayout->addWidget(diagnosticsHint);
        diagnosticReport_ = new QPlainTextEdit;
        diagnosticReport_->setObjectName("diagnosticReport");
        diagnosticReport_->setReadOnly(true);
        diagnosticReport_->setAccessibleName("Diagnostic report");
        auto *diagnosticButtons = new QHBoxLayout;
        diagnosticRun_ = new QPushButton("Create report");
        diagnosticRun_->setObjectName("diagnosticRun");
        diagnosticCopy_ = new QPushButton("Copy report");
        diagnosticSave_ = new QPushButton("Save report…");
        diagnosticCopy_->setEnabled(false);
        diagnosticSave_->setEnabled(false);
        diagnosticButtons->addWidget(diagnosticRun_);
        diagnosticButtons->addStretch();
        diagnosticButtons->addWidget(diagnosticCopy_);
        diagnosticButtons->addWidget(diagnosticSave_);
        diagnosticsLayout->addLayout(diagnosticButtons);
        diagnosticReport_->setFixedHeight(220);
        diagnosticReport_->hide();
        diagnosticsLayout->addWidget(diagnosticReport_);
        settingsLayout->addWidget(diagnosticsSection_);
        settingsHeading("About");
        auto *license = new QLabel;
        license->setObjectName("license");
        license->setOpenExternalLinks(true);
        license->setWordWrap(true);
        settingsLayout->addWidget(license);
        auto *aboutQt = new QPushButton("About Qt");
        connect(aboutQt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(this); });
        settingsLayout->addWidget(aboutQt, 0, Qt::AlignLeft);
        settingsLayout->addStretch();
        settingsScroll_ = scrollPage(preferences, "preferencesScroll");
        connect(jumpToDiagnostics, &QPushButton::clicked, this, [this] {
            diagnosticRun_->setFocus();
            scrollToDiagnostics();
        });
        addPage(settingsScroll_, "Settings");
        navigation_->setCurrentRow(0);
        layout->addWidget(controls_, 1);
        status_ = new QLabel;
        status_->setObjectName("status");
        status_->setWordWrap(true);
        status_->setTextFormat(Qt::PlainText);
        status_->setFixedHeight(34);
        status_->setMargin(10);
        status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(status_);
        auto *footerBar = new QWidget;
        footerBar->setObjectName("workspaceFooter");
        auto *footer = new QHBoxLayout(footerBar);
        footer->setContentsMargins(16, 6, 16, 8);
        auto *close = new QPushButton("Hide to tray");
        footer->addWidget(close);
        footer->addStretch();
        save_ = new QPushButton("Save settings");
        save_->setAccessibleName("Save settings");
        save_->setObjectName("primary");
        save_->setEnabled(!configurationBlocked_);
        footer->addWidget(save_);
        layout->addWidget(divider(Qt::Horizontal));
        layout->addWidget(footerBar);
        tray_ = new QSystemTrayIcon(this);
        menu_ = new QMenu(this);
        quickPanel_ = new TrayQuickPanel(this);
        quickPanel_->setObjectName("quickPanel");
        quickPanel_->setWindowTitle("LG Input Switch");
        auto *quickLayout = new QVBoxLayout(quickPanel_);
        auto *quickContent = new QWidget;
        quickItems_ = new QVBoxLayout(quickContent);
        quickItems_->setContentsMargins(0, 0, 4, 0);
        quickItems_->setSizeConstraint(QLayout::SetMinimumSize);
        auto *quickScroll = scrollPage(quickContent, "quickScroll");
        quickScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        quickLayout->addWidget(quickScroll, 1);
        quickStatus_ = new QLabel;
        quickStatus_->setTextFormat(Qt::PlainText);
        quickStatus_->setWordWrap(true);
        quickStatus_->setObjectName("muted");
        connect(quickPanel_, &QDialog::finished, quickStatus_, &QLabel::clear);
        quickLayout->addWidget(quickStatus_);
        auto *open = new QPushButton("Open LG Input Switch");
        connect(open, &QPushButton::clicked, this, [this] { quickPanel_->hide(); showWindow(); });
        quickLayout->addWidget(open);
        tray_->setContextMenu(menu_);
        tray_->setToolTip("LG Input Switch");
        auto *trayClick = new QTimer(this);
        trayClick->setSingleShot(true);
        connect(trayClick, &QTimer::timeout, this, &DesktopWindow::showQuickPanel);
        connect(tray_, &QSystemTrayIcon::activated, this, [this, trayClick](QSystemTrayIcon::ActivationReason reason) {
            // Plasma may deliver two Trigger events instead of DoubleClick.
            if (reason == QSystemTrayIcon::DoubleClick ||
                (reason == QSystemTrayIcon::Trigger && trayClick->isActive())) {
                trayClick->stop();
                quickPanel_->hide();
                showWindow();
            } else if (reason == QSystemTrayIcon::Trigger) {
                trayClick->start(QApplication::doubleClickInterval());
            } else {
                trayClick->stop();
            }
        });
        connect(tray_, &QSystemTrayIcon::messageClicked, this, [this] {
            if (notificationPage_) {
                for (int index = 0; index < tabs_->count(); ++index)
                    if (tabs_->widget(index)->isAncestorOf(notificationPage_) || tabs_->widget(index) == notificationPage_)
                        tabs_->setCurrentIndex(index);
            }
            showWindow();
        });
        connect(close, &QPushButton::clicked, this, [this] {
            if (tray_->isVisible() && QSystemTrayIcon::isSystemTrayAvailable())
                hide();
            else
                message("This desktop has no system tray. Keep the window open or close it to quit.");
        });
        rebuildProfileTabs();
        populateProfile();
        connect(profileTabs_, &QTabBar::currentChanged, this, [this](int index) {
            if (populating_ || index < 0)
                return;
            const auto profileId = profileTabs_->tabData(index).toString();
            if (profileId.isEmpty())
            {
                // Restore the selected profile before opening the dialog, including cancel.
                const QSignalBlocker block(profileTabs_);
                for (int tab = 0; tab < profileTabs_->count(); ++tab)
                    if (profileTabs_->tabData(tab).toString() == activeId_)
                        profileTabs_->setCurrentIndex(tab);
                addProfile();
                return;
            }
            captureDraft();
            activeId_ = profileId;
            {
                const QSignalBlocker actionBlock(editProfile_), monitorBlock(monitorDetails_);
                editProfile_->setChecked(beforeActions_.contains(profileId));
                monitorDetails_->setChecked(!editProfile_->isChecked() && beforeAssignments_.contains(profileId));
                editProfile_->setEnabled(!monitorDetails_->isChecked());
            }
            populateProfile();
        });
        connect(monitorDetails_, &QToolButton::toggled, this, [this](bool editing) {
            if (!editing && beforeAssignments_.contains(activeId_)) {
                finishAssignmentEdit(false);
                return;
            }
            editProfile_->setEnabled(!editing);
            populateMembers();
            updateActions();
        });
        connect(members_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
            if (populating_ || item->column() != 0) return;
            captureDraft();
            const auto id = item->data(Qt::UserRole).toString();
            auto &p = active();
            if (item->checkState() == Qt::Checked && !p.monitors.contains(id)) {
                p.monitors.append(id);
                for (auto &a : p.actions) {
                    const auto *m = monitor(id);
                    QString input;
                    for (const auto &v : m->inputs)
                        if (InputNameKey(a.name) == InputNameKey(v.label)) input = v.id;
                    a.inputs[id] = input;
                    if (input.isEmpty()) a.enabled = false;
                }
            } else if (item->checkState() == Qt::Unchecked && p.monitors.size() > 1) {
                p.monitors.removeAll(id);
                for (auto &a : p.actions) { a.inputs.remove(id); a.missingInputs.remove(id); }
            } else {
                populateMembers();
                message("Keep at least one monitor assigned to this profile.");
                return;
            }
            dirty();
            populateProfile();
        });
        connect(editProfile_, &QPushButton::toggled, this, [this](bool editing) {
            if (!editing && beforeActions_.contains(activeId_)) {
                finishActionEdit(false);
                return;
            }
            captureDraft();
            populateProfile();
        });
        connect(diagnosticProfile_, &QComboBox::currentIndexChanged, this, [this] {
            diagnosticReport_->hide();
            diagnosticReport_->clear();
            diagnosticCopy_->setEnabled(false);
            diagnosticSave_->setEnabled(false);
        });
        connect(renameProfile_, &QAction::triggered, this, [this] {
            captureDraft();
            bool ok = false;
            const auto name = QInputDialog::getText(this, "Rename profile", "Profile name", QLineEdit::Normal,
                                                    active().name, &ok)
                                  .trimmed();
            if (!ok || name.isEmpty())
                return;
            for (const auto &p : draft_.profiles)
                if (p.id != activeId_ && p.name == name)
                {
                    message("Choose a unique profile name.");
                    return;
                }
            active().name = name;
            rebuildProfileTabs();
            dirty();
        });
        connect(removeProfile_, &QAction::triggered, this, [this] {
            if (draft_.profiles.size() <= 1)
                return;
            if (QMessageBox::question(this, "Remove profile",
                                      "Remove “" + active().name +
                                          "”? Its saved monitors remain available.") != QMessageBox::Yes)
                return;
            for (int i = 0; i < draft_.profiles.size(); ++i)
                if (draft_.profiles[i].id == activeId_)
                {
                    draft_.profiles.removeAt(i);
                    break;
                }
            activeId_ = draft_.profiles.front().id;
            rebuildProfileTabs();
            populateProfile();
            dirty();
        });
        connect(addMonitor_, &QPushButton::clicked, this, [this] { addMonitor(); });
        connect(save_, &QPushButton::clicked, this, [this] { save(); });
        connect(cycle_, &QPushButton::clicked, this, [this] { sendInput(activeId_, "cycle"); });
        connect(previous_, &QPushButton::clicked, this, [this] { sendInput(activeId_, "previous"); });
        connect(retry_, &QPushButton::clicked, this, [this] { sendInput(activeId_, {}, true); });
        connect(addAction, &QPushButton::clicked, this, [this] {
            captureDraft();
            if (active().actions.size() >= 64)
                return;
            ProfileAction a;
            a.id = NewDesktopId();
            a.name = "New action";
            for (const auto &id : active().monitors)
                a.inputs[id] = {};
            active().actions.push_back(a);
            populateProfile();
            table_->selectRow(table_->rowCount() - 1);
            dirty();
        });
        connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
            if (!populating_ && item->column() == 0)
                for (auto &action : active().actions)
                    if (action.id == item->data(Qt::UserRole).toString())
                        action.autoDisabled = false; // The user now owns this choice.
            dirty();
        });
        connect(codes_, &QCheckBox::toggled, this, [this] {
            captureDraft();
            populateProfile();
        });
        const auto applyAppearance = [this] {
            theme();
            dirty();
        };
        connect(appearance_, &QComboBox::currentIndexChanged, this, applyAppearance);
        connect(systemAccent_, &QCheckBox::toggled, this, applyAppearance);
        connect(debounce_, &QSpinBox::valueChanged, this, [this] { dirty(); });
        connect(notifications_, &QCheckBox::toggled, this, [this] { dirty(); });
        connect(startup_, &QCheckBox::toggled, this, [this] { dirty(); });
        connect(cycleShortcut_, &QKeySequenceEdit::keySequenceChanged, this, [this] { dirty(); });
        connect(previousShortcut_, &QKeySequenceEdit::keySequenceChanged, this, [this] { dirty(); });
        connect(globalShortcuts_, &QCheckBox::toggled, this, [this] {
            captureDraft();
            populateProfile();
            dirty();
        });
        connect(diagnosticRun_, &QPushButton::clicked, this, [this] { diagnostics(); });
        connect(diagnosticCopy_, &QPushButton::clicked, this, [this] {
            QApplication::clipboard()->setText(diagnosticReport_->toPlainText());
            message("Report copied. Paste it into your support request when ready.");
        });
        connect(diagnosticSave_, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getSaveFileName(
                this, "Save diagnostic report", "lginputswitch-diagnostics.json", "JSON report (*.json)");
            if (path.isEmpty())
                return;
            QString error;
            message(SaveDesktopDiagnostics(path, diagnosticReport_->toPlainText(), error)
                        ? "Diagnostic report saved."
                        : "Could not save report: " + error);
        });
        auto recordingChanged = [this](QWidget *current) {
            bool capture = false;
            if (qApp->applicationState() == Qt::ApplicationActive)
                for (auto *w = current; w; w = w->parentWidget())
                    if (qobject_cast<QKeySequenceEdit *>(w))
                    {
                        capture = true;
                        break;
                    }
            if (capture == recordingShortcut_)
                return;
            recordingShortcut_ = capture;
            if (capture)
            {
#ifdef Q_OS_WIN
                for (const auto &item : hotkeys_)
                    UnregisterHotKey(nullptr, item.first);
                hotkeys_.clear();
#else
                if (!preview_)
                    kdeShortcuts_.clear();
#endif
            }
            else if (!preview_)
                registerHotkeys();
        };
        connect(qApp, &QApplication::focusChanged, this,
                [recordingChanged](QWidget *, QWidget *current) { recordingChanged(current); });
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
                [recordingChanged](Qt::ApplicationState) { recordingChanged(QApplication::focusWidget()); });

        qApp->installNativeEventFilter(this);
        qApp->installEventFilter(this);
        const auto icon = MonitorIcon();
        setWindowIcon(icon);
        tray_->setIcon(icon);
        theme();
        updateActions();
        if (!preview_ && !configurationBlocked_)
        {
            tray_->show();
            registerHotkeys();
            refresh();
        }
        else if (preview_)
            message("Design preview · no settings are saved and no hardware commands are sent.");
        if (!warning.isEmpty())
            QTimer::singleShot(100, this, [this, warning] { message(warning); });
    }
    ~DesktopWindow() override
    {
        qApp->removeNativeEventFilter(this);
        qApp->removeEventFilter(this);
        QObject::disconnect(qApp, nullptr, this, nullptr);
        QObject::disconnect(qApp->styleHints(), nullptr, this, nullptr);
#ifdef Q_OS_WIN
        for (const auto &item : hotkeys_)
            UnregisterHotKey(nullptr, item.first);
#endif
        cancelBrightnessRead();
        worker_.waitForDone();
    }
    void capture(const QString &directory)
    {
        QDir().mkpath(directory);
        if (preview_)
        {
            const auto snapshot = diagnosticSnapshot();
            diagnosticReport_->setPlainText(
                FormatDesktopDiagnostics(snapshot, CollectDesktopDiagnostics(snapshot, true), true));
            diagnosticCopy_->setEnabled(true);
            diagnosticSave_->setEnabled(true);
        }
        if (preview_)
        {
            auto &main = draft_.monitors[0];
            auto &side = draft_.monitors[1];
            main.name = "LG UltraGear";
            main.inputs[2].label = "HDMI 1";
            main.inputs[1].enabled = main.inputs[3].enabled = false;
            side.name = "Dell UltraSharp";
            side.inputs[1].code = "0x1b";
            side.inputs[2].enabled = side.inputs[3].enabled = false;
            side.inputs.swapItemsAt(0, 1);
            for (auto &p : draft_.profiles)
                if (p.id == "preview-group")
                {
                    p.actions[0].name = "Work laptop";
                    p.actions[1].name = "Desktop PC";
                    p.actions[0].inputs[side.id] = side.inputs[0].id;
                    p.actions[1].inputs[side.id] = side.inputs[1].id;
                    lastSentInput_ = p.actions[0].inputs;
                }
            saved_ = draft_;
            activeId_ = "preview-group";
            rebuildProfileTabs();
            populateProfile();
            resize(1000, 740);
        }
        for (const auto &mode : {QString("light"), QString("dark")})
        {
            appearance_->setCurrentIndex(appearance_->findData(mode));
            dirty_ = false;
            save_->setText("Save settings");
            updateActions();
            for (int tab = 0; tab < tabs_->count(); ++tab)
            {
                tabs_->setCurrentIndex(tab);
                qApp->processEvents();
                const auto suffix = tab == 0 ? QString{} : "-" + tabs_->tabText(tab).toLower();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                grab().save(directory + "/desktop-" + mode + suffix + ".png");
                if (tab == 1 && preview_) {
                    editMonitor(selectedMonitorId_);
                    qApp->processEvents();
                    grab().save(directory + "/monitors-" + mode + "-editing.png");
                    closeMonitorEditor();
                }
                if (tabs_->currentWidget() == settingsScroll_)
                {
                    diagnosticReport_->show();
                    qApp->processEvents();
                    scrollToDiagnostics();
                    grab().save(directory + "/desktop-" + mode + "-diagnostics.png");
                    diagnosticReport_->hide();
                    settingsScroll_->verticalScrollBar()->setValue(0);
                }
            }
            showQuickPanel();
            qApp->processEvents();
            quickPanel_->grab().save(directory + "/tray-" + mode + ".png");
            quickPanel_->hide();
        }
        tabs_->setCurrentIndex(0);
        if (preview_)
        {
            activeId_ = "preview-group";
            rebuildProfileTabs();
            populateProfile();
            for (const auto &mode : {QString("light"), QString("dark")})
            {
                appearance_->setCurrentIndex(appearance_->findData(mode));
                dirty_ = false;
                save_->setText("Save settings");
                updateActions();
                qApp->processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                message("Design preview · no settings are saved and no monitor commands are sent.");
                grab().save(directory + "/profiles-" + mode + ".png");
                editProfile_->setChecked(true);
                qApp->processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                grab().save(directory + "/profiles-" + mode + "-editing.png");
                const auto normalSize = size();
                resize(640, 480);
                qApp->processEvents();
                grab().save(directory + "/profiles-" + mode + "-compact.png");
                resize(normalSize);
                editProfile_->setChecked(false);
                qApp->processEvents();
            }
        }
    }
};
#ifndef LG_DESKTOP_TEST
int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    if (argc == 5 && std::strcmp(argv[1], "--windows-brightness") == 0)
    {
        QCoreApplication app(argc, argv);
        const auto args = app.arguments();
        bool valid = false;
        const int percent = args[4].toInt(&valid);
        if (!valid)
            return 2;
        std::cout << QJsonDocument(ProbeNativeWindowsBrightness(args[2], args[3], percent))
                         .toJson(QJsonDocument::Compact).constData();
        return 0;
    }
    if ((argc == 2 && std::strcmp(argv[1], "--windows-discovery") == 0) ||
        (argc == 4 && std::strcmp(argv[1], "--windows-input-probe") == 0) ||
        ((argc == 5 || argc == 6) && std::strcmp(argv[1], "--windows-switch") == 0))
    {
        QCoreApplication app(argc, argv);
        const auto arguments = app.arguments();
        const auto result = argc == 2 ? ProbeNativeWindowsDiscovery()
            : argc == 4 ? ProbeNativeWindowsCapabilities(arguments[2], arguments[3])
                        : ProbeNativeWindowsSwitch(arguments[2], arguments[3], arguments[4], arguments.value(5));
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData();
        return 0;
    }
    if (argc == 5 && std::strcmp(argv[1], "--input-probe") == 0)
    {
        QCoreApplication app(argc, argv);
        bool adapterOk = false, displayOk = false;
        const int adapter = QString::fromLocal8Bit(argv[2]).toInt(&adapterOk);
        const int display = QString::fromLocal8Bit(argv[3]).toInt(&displayOk);
        if (!adapterOk || !displayOk)
            return 2;
        std::cout << QJsonDocument(ProbeTargetCapabilities(adapter, display, QString::fromLocal8Bit(argv[4])))
                         .toJson(QJsonDocument::Compact)
                         .constData();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--diagnostic-probe") == 0)
    {
        QCoreApplication app(argc, argv);
        std::cout << QJsonDocument(ProbeWindowsMonitorControls()).toJson(QJsonDocument::Compact).constData();
        return 0;
    }
#endif
#ifdef Q_OS_WIN
    if (argc == 3 && std::strcmp(argv[1], "--apply-update") == 0) {
        QCoreApplication helper(argc, argv);
        return ApplyWindowsUpdate(helper.arguments().at(2));
    }
#endif
    QApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitch");
    app.setApplicationName("LGInputSwitchDesktop");
    app.setApplicationVersion("2.0.0-beta.1");
    app.setDesktopFileName("lginputswitch");
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription("LG monitor input switching with a tray and global shortcuts.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"update-ready", "Confirm a completed portable update (internal).", "plan"});
#ifdef LG_EMBEDDED_DISTRIBUTION
    parser.addOption({"licenses", "Export embedded license texts, source links and rebuild instructions.", "file"});
#endif
    parser.addOption({"preview", "Show synthetic data without accessing settings or hardware."});
    parser.addOption({"background", "Start in the tray when a tray host is available."});
    parser.addOption({"screenshot-dir", "Render design previews (requires --preview).", "directory"});
    parser.process(app);
#ifdef LG_EMBEDDED_DISTRIBUTION
    if (parser.isSet("licenses")) {
        QFile source(":/distribution/THIRD-PARTY-NOTICES.txt");
        QSaveFile destination(parser.value("licenses"));
        if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) return 2;
        const auto data = source.readAll();
        return destination.write(data) == data.size() && destination.commit() ? 0 : 3;
    }
#endif
    const bool preview = parser.isSet("preview");
    if (parser.isSet("screenshot-dir") && !preview)
        parser.showHelp(2);
    std::unique_ptr<QLockFile> lock;
    if (!preview)
    {
        auto path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(path);
        lock = std::make_unique<QLockFile>(path + "/desktop.lock");
        if (!lock->tryLock())
        {
            if (!parser.isSet("background"))
                QMessageBox::information(nullptr, "LG Input Switch",
                                         "The desktop app is already running. Open it from the tray.");
            return 0;
        }
    }
    DesktopWindow window(preview);
    if (window.shouldShowAtStartup(parser.isSet("background"), QSystemTrayIcon::isSystemTrayAvailable()))
        window.show();
    if (parser.isSet("update-ready")) {
        const auto plan = QFileInfo(parser.value("update-ready"));
        if (plan.absolutePath() == QFileInfo(QCoreApplication::applicationFilePath()).absolutePath() &&
            QRegularExpression("^\\.lginputswitch-update-[0-9a-f-]{36}\\.json$").match(plan.fileName()).hasMatch()) {
            QSaveFile ready(plan.absoluteFilePath() + ".ready");
            if (ready.open(QIODevice::WriteOnly)) { ready.write("ready"); ready.commit(); }
        }
    }
    if (preview && parser.isSet("screenshot-dir"))
        QTimer::singleShot(300, &window, [&] {
            window.capture(parser.value("screenshot-dir"));
            app.quit();
        });
    return app.exec();
}

#endif
