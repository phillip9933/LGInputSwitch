#include "../core/input_value.h"
#include "diagnostics.h"
#include "kde_shortcuts.h"
#include "monitor_inputs.h"
#include "profiles.h"
#ifndef Q_OS_WIN
#include "linux_autostart.h"
#endif
#include <QtConcurrent>
#include <QtWidgets>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#ifdef Q_OS_WIN
#include "../app/hotkeys.h"
#include "../platform/windows/startup.h"
#endif

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
        p.setBrush(QColor("#3268e8"));
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

struct MonitorSetup
{
    Discovery discovery;
    QVector<QPair<QString, MonitorInputRead>> inputs;
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
    QTabBar *profileTabs_;
    QTabWidget *tabs_;
    QTableWidget *members_, *table_;
    QComboBox *appearance_, *diagnosticProfile_;
    QCheckBox *notifications_, *startup_, *globalShortcuts_, *codes_;
    QSpinBox *debounce_;
    QKeySequenceEdit *cycleShortcut_, *previousShortcut_;
    QPushButton *refresh_, *save_, *cycle_, *previous_, *retry_, *addMonitor_, *addProfile_, *editProfile_;
    QAction *renameProfile_, *removeProfile_;
    QWidget *controls_, *monitorCard_, *editTools_, *shortcutPanel_, *monitorTools_;
    QToolButton *monitorDetails_;
    QLabel *monitorSummary_;
    bool monitorAttention_ = false;
    QScrollArea *profileScroll_;
    QSystemTrayIcon *tray_;
    QMenu *menu_;
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
        status_->setToolTip(text);
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
        QVector<ProfileAction> actions;
        for (int row = 0; row < table_->rowCount(); ++row)
        {
            ProfileAction a;
            a.id = table_->item(row, 0)->data(Qt::UserRole).toString();
            a.name = table_->item(row, 1)->text().trimmed();
            a.enabled = table_->item(row, 0)->checkState() == Qt::Checked;
            for (int i = 0; i < p.monitors.size(); ++i)
            {
                auto *combo = qobject_cast<QComboBox *>(table_->cellWidget(row, 2 + i));
                const auto *item = table_->item(row, 2 + i);
                a.inputs[p.monitors[i]] = combo  ? combo->currentData().toString()
                                          : item ? item->data(Qt::UserRole).toString()
                                                 : QString{};
            }
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
        auto &c = draft_.preferences;
        c.appearance = appearance_->currentData().toString();
        c.notifications = notifications_->isChecked();
        c.startup = startup_->isChecked();
        c.globalShortcuts = globalShortcuts_->isChecked();
        c.debounce = debounce_->value();
        draft_.activeProfile = activeId_;
    }
    void setBusy(bool value)
    {
        busy_ = value;
        controls_->setEnabled(!value);
        monitorCard_->setEnabled(!value);
        save_->setEnabled(!value && !configurationBlocked_);
        updateActions();
    }
    void rebuildTabs()
    {
        const QSignalBlocker block(profileTabs_);
        const QSignalBlocker diagnosticBlock(diagnosticProfile_);
        diagnosticProfile_->clear();
        while (profileTabs_->count())
            profileTabs_->removeTab(0);
        for (const auto &p : draft_.profiles)
        {
            const int index = profileTabs_->addTab(p.name);
            profileTabs_->setTabData(index, p.id);
            profileTabs_->setTabToolTip(index, p.name);
            diagnosticProfile_->addItem(p.name, p.id);
            if (p.id == activeId_)
            {
                profileTabs_->setCurrentIndex(index);
                diagnosticProfile_->setCurrentIndex(index);
            }
        }
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
        members_->setRowCount(p.monitors.size());
        members_->setColumnWidth(2, 44);
        int connected = 0;
        members_->setFixedHeight(qMin(3, qMax(1, int(p.monitors.size()))) * 46 + 4);
        for (int row = 0; row < p.monitors.size(); ++row)
        {
            const auto id = p.monitors[row];
            const auto *m = FindMonitor(draft_, id);
            if (!m)
                continue;
            members_->setItem(row, 0, new QTableWidgetItem(m->name));
            const auto resolved = ResolveSavedMonitor(*m, discovery_, confirmed_);
            if (resolved.available)
                ++connected;
            else
                members_->setColumnWidth(2, 104);
            const auto connection =
                resolved.available ? "Connected · " + resolved.target.label : "Connection needed";
            auto *status = new QTableWidgetItem(connection);
            status->setToolTip((resolved.available ? resolved.target.label : resolved.reason) +
                               (m->inputStatus.isEmpty() ? "" : "\n" + m->inputStatus));
            members_->setItem(row, 1, status);
            auto *button = new QToolButton;
            button->setText(resolved.available ? "…" : "Connect…");
            button->setObjectName("connect-" + id);
            button->setProperty("compactMenu", resolved.available);
            button->setAccessibleName(m->name +
                                      (resolved.available ? " monitor options" : " connect monitor"));
            auto *options = new QMenu(button);
            connect(options->addAction("Edit monitor & inputs…"), &QAction::triggered, this,
                    [this, id] { editMonitor(id); });
            connect(options->addAction("Reconnect…"), &QAction::triggered, this,
                    [this, id] { reconnectMonitor(id); });
            auto *remove = options->addAction("Remove from this profile");
            remove->setEnabled(p.monitors.size() > 1);
            connect(remove, &QAction::triggered, this, [this, id] {
                captureDraft();
                active().monitors.removeAll(id);
                for (auto &a : active().actions)
                    a.inputs.remove(id);
                dirty();
                populateProfile();
            });
            button->setMenu(options);
            button->setPopupMode(resolved.available ? QToolButton::InstantPopup
                                                    : QToolButton::MenuButtonPopup);
            if (!resolved.available)
                connect(button, &QToolButton::clicked, this, [this, id] { reconnectMonitor(id); });
            // The context menu also exposes configuration for disconnected monitors.
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QWidget::customContextMenuRequested, button,
                    [button, options](const QPoint &point) { options->popup(button->mapToGlobal(point)); });
            members_->setCellWidget(row, 2, button);
            members_->setRowHeight(row, 46);
        }
        const bool needsConnection = connected < p.monitors.size();
        if (needsConnection && !monitorAttention_)
        {
            const QSignalBlocker block(monitorDetails_);
            monitorDetails_->setChecked(true);
        }
        monitorAttention_ = needsConnection;
        monitorSummary_->setText(
            needsConnection
                ? QString("%1 of %2 monitors connected").arg(connected).arg(p.monitors.size())
                : QString("%1 monitor%2 connected").arg(connected).arg(connected == 1 ? "" : "s"));
        const bool expanded = monitorDetails_->isChecked();
        monitorDetails_->setText(expanded ? "Hide details" : "Manage monitors");
        members_->setVisible(expanded);
        monitorTools_->setVisible(expanded);
    }
    void populateProfile()
    {
        populating_ = true;
        populateMembers();
        const auto &p = active();
        const bool editing = editProfile_->isChecked();
        editTools_->setVisible(editing);
        shortcutPanel_->setVisible(editing && shortcutEditingAvailable());
        editProfile_->setText(editing ? "Done editing" : "Edit actions");
        table_->setEditTriggers(editing ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                        : QAbstractItemView::NoEditTriggers);
        table_->setMinimumHeight(qMin(6, qMax(2, int(p.actions.size()))) * 46 + 48);
        table_->setMaximumHeight(qMin(8, qMax(2, int(p.actions.size()))) * 46 + 48);
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
        table_->setColumnWidth(0, 42);
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
            table_->setItem(row, 1, new QTableWidgetItem(a.name));
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
                    table_->setColumnWidth(2 + col, 150);
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
                connect(combo, &QComboBox::currentIndexChanged, this, [this] { dirty(); });
                table_->setCellWidget(row, 2 + col, combo);
                table_->setColumnWidth(2 + col, 155);
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
            auto *send = new QPushButton("Switch");
            send->setEnabled(a.enabled);
            connect(send, &QPushButton::clicked, this,
                    [this, profileId = p.id, actionId = a.id] { sendInput(profileId, actionId); });
            table_->setCellWidget(row, 3 + p.monitors.size(), send);
            table_->setRowHeight(row, 46);
        }
        table_->setColumnHidden(0, !editing);
        const int shortcutCol = 2 + p.monitors.size();
        table_->setColumnWidth(shortcutCol, 160);
        table_->setColumnWidth(shortcutCol + 1, 85);
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
        {
            const QSignalBlocker block(diagnosticProfile_);
            diagnosticProfile_->setCurrentIndex(diagnosticProfile_->findData(activeId_));
        }
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
            const QSignalBlocker block(editProfile_);
            editProfile_->setChecked(true);
        }
        rebuildTabs();
        populateProfile();
        dirty();
    }
    void addMonitor()
    {
        captureDraft();
        QDialog dialog(this);
        dialog.setWindowTitle("Add monitor");
        dialog.setMinimumSize(540, 300);
        dialog.resize(640, 340);
        auto *layout = new QVBoxLayout(&dialog);
        auto *choice = new QComboBox;
        choice->setObjectName("addMonitorChoice");
        choice->setMinimumContentsLength(24);
        choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        for (const auto &m : draft_.monitors)
            if (!active().monitors.contains(m.id))
                choice->addItem("Saved: " + m.name, m.id);
        for (int i = 0; i < discovery_.monitors.size(); ++i)
        {
            const auto &target = discovery_.monitors[i];
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
        auto *hint = new QLabel("Add a monitor to “" + active().name +
                                "”. The app reads that monitor’s advertised inputs automatically. Saved "
                                "monitors reuse their existing configuration.");
        hint->setTextFormat(Qt::PlainText);
        hint->setObjectName("addMonitorHint");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        layout->addWidget(choice);
        auto *method = new QComboBox;
        method->addItem("Standard DDC/CI input source (0x60)", "standard");
        method->addItem("LG alternate input switching (0xF4)", "lg");
        auto methodVisibility = [choice, method] {
            method->setVisible(choice->currentData().metaType().id() != QMetaType::QString);
        };
        connect(choice, &QComboBox::currentIndexChanged, &dialog, methodVisibility);
        methodVisibility();
        layout->addWidget(method);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QString id;
        const bool newMonitor = choice->currentData().metaType().id() != QMetaType::QString;
        if (!newMonitor)
            id = choice->currentData().toString();
        else
        {
            const auto target = discovery_.monitors[choice->currentData().toInt()];
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
            m.inputs = DefaultMonitorInputs(m.method);
            confirmed_[m.id] = target;
            id = m.id;
            draft_.monitors.push_back(m);
        }
        active().monitors.push_back(id);
        for (auto &a : active().actions)
        {
            a.inputs[id] = {};
            for (const auto &input : FindMonitor(draft_, id)->inputs)
                if (InputNameKey(a.name) == InputNameKey(input.label))
                    a.inputs[id] = input.id;
        }
        {
            const QSignalBlocker block(editProfile_);
            editProfile_->setChecked(true);
        }
        populateProfile();
        dirty();
        if (newMonitor)
            scanInputs(false, {id});
    }
    void editMonitor(const QString &id)
    {
        captureDraft();
        const auto *original = FindMonitor(draft_, id);
        if (!original)
            return;
        auto next = *original;
        QDialog dialog(this);
        dialog.setWindowTitle("Monitor settings");
        dialog.resize(560, 470);
        auto *layout = new QVBoxLayout(&dialog);
        auto *hint = new QLabel("These settings are shared by every profile using this monitor.");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *form = new QFormLayout;
        auto *name = new QLineEdit(next.name);
        name->setMaxLength(100);
        name->setObjectName("monitorName");
        auto *method = new QComboBox;
        method->setObjectName("monitorMethod");
        method->addItem("LG alternate (0xF4)", "lg");
        method->addItem("Standard DDC/CI (0x60) · Dell and other compatible monitors", "standard");
        method->setCurrentIndex(method->findData(next.method));
        auto *source = new QLineEdit(next.source);
        source->setObjectName("monitorSource");
        form->addRow("Monitor name", name);
        form->addRow("Switching method", method);
        form->addRow("LG I²C source address", source);
        source->setEnabled(next.method == "lg");
        connect(method, &QComboBox::currentIndexChanged, &dialog,
                [method, source] { source->setEnabled(method->currentData() == "lg"); });
        auto *methodHint =
            new QLabel("Standard switching uses input control 0x60 and source 0x51. Input values still "
                       "depend on the monitor. After changing methods, review the codes or load presets; "
                       "custom LG codes are not translated automatically.");
        methodHint->setWordWrap(true);
        form->addRow(methodHint);
        layout->addLayout(form);
        auto *automatic = new QCheckBox("Read and set up this monitor’s inputs automatically");
        automatic->setObjectName("automaticMonitorInputs");
        automatic->setChecked(next.autoInputs);
        form->addRow(automatic);
        auto *inputs = new QTableWidget(0, 2);
        inputs->setObjectName("monitorInputs");
        inputs->setHorizontalHeaderLabels({"Input name", "Code"});
        inputs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        auto append = [&](const DesktopInput &input) {
            const int row = inputs->rowCount();
            inputs->insertRow(row);
            auto *label = new QTableWidgetItem(input.label);
            label->setData(Qt::UserRole, input.id);
            inputs->setItem(row, 0, label);
            inputs->setItem(row, 1, new QTableWidgetItem(input.code));
        };
        for (const auto &input : next.inputs)
            append(input);
        layout->addWidget(inputs);
        auto *row = new QHBoxLayout;
        auto *add = new QPushButton("Add input");
        auto *remove = new QPushButton("Remove input");
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        layout->addLayout(row);
        connect(add, &QPushButton::clicked, &dialog, [&, append] {
            if (inputs->rowCount() < 64)
                append({"New input", "0x00", {}, true, NewDesktopId()});
        });
        connect(remove, &QPushButton::clicked, &dialog, [&, id] {
            const int row = inputs->currentRow();
            if (row < 0 || inputs->rowCount() <= 1)
                return;
            const auto inputId = inputs->item(row, 0)->data(Qt::UserRole).toString();
            for (const auto &profile : draft_.profiles)
                for (const auto &a : profile.actions)
                    if (a.inputs.value(id) == inputId)
                    {
                        QMessageBox::information(
                            &dialog, "Input in use",
                            "Choose a different input in all actions using this input before removing it.");
                        return;
                    }
            inputs->removeRow(row);
        });
        auto *presets = new QPushButton("Load method presets");
        presets->setObjectName("loadMonitorPresets");
        auto *advertised = new QPushButton("Read advertised inputs");
        advertised->setObjectName("readMonitorInputs");
        auto *discoveryButtons = new QHBoxLayout;
        discoveryButtons->addWidget(presets);
        discoveryButtons->addWidget(advertised);
        layout->addLayout(discoveryButtons);
        auto *error = new QLabel;
        error->setWordWrap(true);
        layout->addWidget(error);
        connect(presets, &QPushButton::clicked, &dialog, [&, append] {
            if (QMessageBox::question(&dialog, "Load input presets",
                                      "Replace this monitor’s input list with the selected method’s common "
                                      "inputs? Existing action mappings that cannot be matched by input name "
                                      "will need to be selected again before saving.") != QMessageBox::Yes)
                return;
            auto defaults = DefaultMonitorInputs(method->currentData().toString());
            auto canonical = [](QString name) {
                name.remove(' ');
                if (name.compare("DisplayPort", Qt::CaseInsensitive) == 0)
                    name += '1';
                return name.toLower();
            };
            for (auto &input : defaults)
                for (int i = 0; i < inputs->rowCount(); ++i)
                    if (canonical(inputs->item(i, 0)->text()) == canonical(input.label))
                        input.id = inputs->item(i, 0)->data(Qt::UserRole).toString();
            inputs->setRowCount(0);
            for (const auto &input : defaults)
                append(input);
            error->setText("Presets loaded. Review these codes for your monitor, then review all affected "
                           "profile actions.");
        });
        connect(advertised, &QPushButton::clicked, &dialog, [&, append] {
            if (preview_)
            {
                error->setText("Design preview · monitor queries are disabled.");
                return;
            }
            const auto resolution = ResolveSavedMonitor(next, discovery_, confirmed_);
            if (!resolution.available)
            {
                error->setText("Select this monitor’s connection before reading advertised inputs.");
                return;
            }
            auto query = next;
            query.method = method->currentData().toString();
            advertised->setEnabled(false);
            error->setText("Reading this monitor’s advertised inputs…");
            method->setEnabled(false);
            auto *watcher = new QFutureWatcher<MonitorInputRead>(&dialog);
            connect(watcher, &QFutureWatcher<MonitorInputRead>::finished, &dialog, [&, watcher, append] {
                const auto read = watcher->result();
                watcher->deleteLater();
                advertised->setEnabled(true);
                method->setEnabled(true);
                if (read.complete)
                {
                    if (automatic->isChecked())
                    {
                        auto workspace = draft_;
                        for (auto &m : workspace.monitors)
                            if (m.id == id)
                            {
                                m.autoInputs = true;
                                m.method = method->currentData().toString();
                            }
                        ApplyMonitorInputs(workspace, id, read);
                        const auto *detected = FindMonitor(workspace, id);
                        inputs->setRowCount(0);
                        for (const auto &input : detected->inputs)
                            append(input);
                    }
                    else
                        for (auto input : read.inputs)
                        {
                            unsigned int wanted;
                            if (!ParseInputValue(input.code.toStdString(), 0xffff, wanted))
                                continue;
                            bool exists = false;
                            for (int i = 0; i < inputs->rowCount(); ++i)
                            {
                                unsigned int current;
                                if (ParseInputValue(inputs->item(i, 1)->text().toStdString(), 0xffff,
                                                    current) &&
                                    current == wanted)
                                    exists = true;
                            }
                            if (!exists && inputs->rowCount() < 64)
                            {
                                input.id = NewDesktopId();
                                append(input);
                            }
                        }
                }
                error->setText(read.status);
            });
            watcher->setFuture(QtConcurrent::run(
                &worker_, [query, target = resolution.target] { return ReadMonitorInputs(query, target); }));
        });
        auto editingState = [=] {
            inputs->setEditTriggers(automatic->isChecked() ? QAbstractItemView::NoEditTriggers
                                                           : QAbstractItemView::DoubleClicked |
                                                                 QAbstractItemView::EditKeyPressed);
            add->setEnabled(!automatic->isChecked());
            remove->setEnabled(!automatic->isChecked());
            presets->setEnabled(!automatic->isChecked());
        };
        connect(automatic, &QCheckBox::toggled, &dialog, editingState);
        editingState();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            next.name = name->text().trimmed();
            next.source = source->text().trimmed();
            next.method = method->currentData().toString();
            next.autoInputs = automatic->isChecked();
            next.inputs.clear();
            QSet<QString> labels;
            unsigned int value;
            if (next.name.isEmpty() ||
                (next.method == "lg" && !ParseInputValue(next.source.toStdString(), 0xff, value)))
            {
                error->setText("Enter a monitor name and a valid source address.");
                return;
            }
            for (const auto &m : draft_.monitors)
                if (m.id != id && m.name == next.name)
                {
                    error->setText("Monitor names must be unique.");
                    return;
                }
            for (int i = 0; i < inputs->rowCount(); ++i)
            {
                const auto label = inputs->item(i, 0)->text().trimmed(),
                           code = inputs->item(i, 1)->text().trimmed();
                if (label.isEmpty() || labels.contains(label) ||
                    !ParseInputValue(code.toStdString(), 0xffff, value))
                {
                    error->setText("Each input needs a unique name and a valid code.");
                    return;
                }
                labels.insert(label);
                next.inputs.push_back(
                    {label, code, {}, true, inputs->item(i, 0)->data(Qt::UserRole).toString()});
            }
            dialog.accept();
        });
        // Native shortcuts must not switch monitors while editing or reading
        // their mappings in this modal dialog.
        const bool wasBusy = busy_;
        busy_ = true;
        updateActions();
        const auto accepted = dialog.exec() == QDialog::Accepted;
        busy_ = wasBusy;
        if (accepted)
        {
            *monitor(id) = next;
            for (auto &profile : draft_.profiles)
                for (auto &action : profile.actions)
                    if (action.inputs.contains(id) && !FindMonitorInput(next, action.inputs.value(id)))
                    {
                        action.inputs[id] = {};
                        action.enabled = false;
                    }
            populateProfile();
            dirty();
        }
        updateActions();
        if (accepted && next.autoInputs)
            scanInputs(false, {id});
    }
    void manageMonitors()
    {
        captureDraft();
        QDialog dialog(this);
        dialog.setWindowTitle("Saved monitors");
        dialog.resize(540, 280);
        auto *layout = new QVBoxLayout(&dialog);
        auto *list = new QListWidget;
        layout->addWidget(list);
        auto rebuild = [&] {
            list->clear();
            for (const auto &m : draft_.monitors)
            {
                auto *item = new QListWidgetItem(m.name, list);
                item->setData(Qt::UserRole, m.id);
            }
        };
        rebuild();
        auto *row = new QHBoxLayout;
        auto *edit = new QPushButton("Edit…"), *remove = new QPushButton("Delete unused monitor");
        row->addWidget(edit);
        row->addWidget(remove);
        layout->addLayout(row);
        connect(edit, &QPushButton::clicked, &dialog, [&] {
            if (list->currentItem())
            {
                editMonitor(list->currentItem()->data(Qt::UserRole).toString());
                rebuild();
            }
        });
        connect(remove, &QPushButton::clicked, &dialog, [&] {
            if (!list->currentItem())
                return;
            const auto id = list->currentItem()->data(Qt::UserRole).toString();
            for (const auto &p : draft_.profiles)
                if (p.monitors.contains(id))
                {
                    QMessageBox::information(
                        &dialog, "Monitor in use",
                        "Remove this monitor from its profiles before deleting its saved mapping.");
                    return;
                }
            for (int i = 0; i < draft_.monitors.size(); ++i)
                if (draft_.monitors[i].id == id)
                {
                    draft_.monitors.removeAt(i);
                    break;
                }
            confirmed_.remove(id);
            dirty();
            rebuild();
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
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
        saved_ = draft_;
        config_ = saved_.preferences;
        dirty_ = false;
        save_->setText("Saved");
        message("Profiles saved. Each shortcut controls its named profile, whichever tab is open.");
        registerHotkeys(true);
        showResults();
        updateActions();
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
        auto apply = [&](DesktopWorkspace &w) {
            for (auto &p : w.profiles)
                if (p.id == profileId)
                {
                    if (actionId == "cycle")
                        p.cycleShortcut = text;
                    else if (actionId == "previous")
                        p.previousShortcut = text;
                    else
                        for (auto &a : p.actions)
                            if (a.id == actionId)
                                a.shortcut = text;
                }
        };
        apply(saved_);
        // External changes update saved state. Keep in-progress form edits as
        // drafts, including edits on a different profile's tab.
        if (!dirty_)
        {
            apply(draft_);
            if (profileId == activeId_)
            {
                QKeySequenceEdit *editor = nullptr;
                if (actionId == "cycle")
                    editor = cycleShortcut_;
                else if (actionId == "previous")
                    editor = previousShortcut_;
                else
                    for (int i = 0; i < active().actions.size(); ++i)
                        if (active().actions[i].id == actionId)
                        {
                            const int column = 2 + active().monitors.size();
                            editor = qobject_cast<QKeySequenceEdit *>(table_->cellWidget(i, column));
                            if (auto *item = table_->item(i, column))
                            {
                                const QSignalBlocker block(table_);
                                item->setText(key.toString(QKeySequence::NativeText));
                                item->setData(Qt::UserRole, text);
                            }
                        }
                if (editor)
                {
                    const QSignalBlocker block(editor);
                    editor->setKeySequence(key);
                }
            }
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
            if (ParseHotkey(text.toStdString(), spec) &&
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
    void sendInput(const QString &profileId, QString actionId, bool retry = false)
    {
        if (preview_)
        {
            message("Design preview · switching is disabled.");
            return;
        }
        if (busy_ || recordingShortcut_ || configurationBlocked_)
            return;
        if (dirty_)
        {
            message("Save your changes before switching.");
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
        const auto snapshot = saved_;
        const auto confirmed = confirmed_;
        lastSend_.restart();
        setBusy(true);
        message("Sending profile commands…");
        auto *watcher = new QFutureWatcher<ProfileSwitchResult>(this);
        connect(watcher, &QFutureWatcher<ProfileSwitchResult>::finished, this,
                [this, watcher, snapshot, retry] {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    discovery_ = result.discovery;
                    populateMembers();
                    session_.accept(snapshot, result, retry);
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
                    lastResults_[result.profileId] = display;
                    // Record the actual current attempt, not unsaved UI fields, and
                    // report every target without custom labels or identity hashes.
                    lastCommand_ = ProfileCommandDiagnostics(snapshot, result);
                    const auto *p = FindProfile(snapshot, result.profileId);
                    int sent = 0;
                    for (const auto &m : result.monitors)
                        if (m.result.sent())
                            ++sent;
                    setBusy(false);
                    showResults();
                    const auto text =
                        QString("%1: %2/%3 command(s) sent. %4")
                            .arg(p ? p->name : "Profile")
                            .arg(sent)
                            .arg(result.monitors.size())
                            .arg(result.complete()
                                     ? "Physical input changes are unverified."
                                     : "See per-monitor results; retry failed monitors when ready.");
                    message(text);
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
        DiagnosticSnapshot snapshot;
        snapshot.unsaved = dirty_;
        snapshot.lastCommand = lastCommand_;
        snapshot.profileCount = draft_.profiles.size();
        for (const auto &id : active().monitors)
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
                int index = -1;
                for (int i = 0; i < draft_.monitors.size(); ++i)
                    if (draft_.monitors[i].id == id)
                        index = i;
                snapshot.monitors.push_back({c, resolution.available, index});
            }
        for (const auto &action : active().actions)
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
        diagnosticReport_->setPlainText("Reading diagnostic information…");
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
    void updateActions()
    {
        if (!menu_)
            return;
        const bool usable = !busy_ && !dirty_ && !configurationBlocked_;
        bool hasActions = false;
        for (const auto &action : active().actions)
            if (action.enabled)
                hasActions = true;
        cycle_->setEnabled(!busy_ && hasActions);
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
        addProfile_->setEnabled(!busy_ && draft_.profiles.size() < 32);
        addMonitor_->setEnabled(!busy_ && active().monitors.size() < 16);
        for (auto *action : menu_->actions())
            if (action->menu())
                action->menu()->deleteLater();
        menu_->clear();
        connect(menu_->addAction("Open LG Input Switch"), &QAction::triggered, this,
                [this] { showWindow(); });
        menu_->addSeparator();
        for (const auto &p : saved_.profiles)
        {
            auto *submenu = menu_->addMenu(p.name);
            auto bind = [&](const QString &label, const QString &id, bool enabled) {
                auto *a = submenu->addAction(label);
                a->setEnabled(usable && enabled);
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
            retry->setEnabled(usable && !pending.failedMonitors().isEmpty());
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
        if (dirty_ && QMessageBox::question(this, "Unsaved changes", "Quit without saving your changes?") !=
                          QMessageBox::Yes)
            return;
        quitting_ = true;
        qApp->quit();
    }
    void theme()
    {
        const auto mode = appearance_->currentData().toString();
        const bool dark = mode == "dark" ||
                          (mode == "system" && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
        const QString bg = dark ? "#111827" : "#f4f6fa", card = dark ? "#1b2535" : "#ffffff",
                      text = dark ? "#edf2fa" : "#172238", muted = dark ? "#a9b7cc" : "#56657a",
                      border = dark ? "#354257" : "#dce3ee", field = dark ? "#222f43" : "#ffffff";
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(bg));
        palette.setColor(QPalette::WindowText, QColor(text));
        palette.setColor(QPalette::Base, QColor(field));
        palette.setColor(QPalette::AlternateBase, QColor(card));
        palette.setColor(QPalette::Text, QColor(text));
        palette.setColor(QPalette::Button, QColor(card));
        palette.setColor(QPalette::ButtonText, QColor(text));
        palette.setColor(QPalette::Highlight, QColor("#3268e8"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Link, QColor(dark ? "#91baff" : "#2458bd"));
        palette.setColor(QPalette::LinkVisited, QColor(dark ? "#c4a7ff" : "#7040a8"));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(muted));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
        qApp->setPalette(palette);
        if (auto *license = findChild<QLabel *>("license"))
            license->setText(
                QString("LG Input Switch · AGPL-3.0-or-later · "
                        "<a style=\"color:%1\" href=\"https://github.com/phillip9933/LGInputSwitch\">"
                        "Source and license</a>")
                    .arg(dark ? "#91baff" : "#2458bd"));
        qApp->setStyleSheet(
            QString(
                "QWidget{font-size:13px;color:%1;} QWidget#root{background:%2;}"
                "QFrame#card{background:%3;border:1px solid %4;border-radius:12px;}"
                "QLabel#title{font-size:22px;font-weight:700;} QLabel#muted,QLabel#status{color:%5;} "
                "QLabel#section{font-size:16px;font-weight:600;}"
                "QPushButton{padding:8px 14px;background:%3;border:1px solid %4;border-radius:7px;} "
                "QPushButton:hover{border-color:#3268e8;}"
                "QToolButton{padding:7px 10px;background:%3;border:1px solid %4;border-radius:6px;}"
                "QToolButton:hover,QPushButton:checked{border-color:#3268e8;}"
                "QToolButton[compactMenu=\"true\"]{border:0;background:transparent;}"
                "QToolButton[compactMenu=\"true\"]::menu-indicator{image:none;}"
                "QScrollArea{border:0;background:transparent;} QScrollArea>QWidget>QWidget{background:%2;}"
                "QPushButton:focus,QLineEdit:focus,QComboBox:focus{border:2px solid #3268e8;}"
                "QPushButton#primary{background:#3268e8;color:white;border:1px solid "
                "#3268e8;font-weight:600;}"
                "QPushButton:disabled{color:%5;} "
                "QLineEdit,QSpinBox,QComboBox,QKeySequenceEdit{background:%6;border:1px solid "
                "%4;border-radius:6px;padding:7px;}"
                "QTableWidget{background:%3;border:0;gridline-color:%4;selection-background-color:%6;}"
                "QTableWidget QComboBox,QTableWidget QPushButton,QTableWidget QKeySequenceEdit{margin:3px;}"
                "QHeaderView::section{background:%3;color:%5;border:0;border-bottom:1px solid "
                "%4;padding:10px;text-align:left;}"
                "QTabWidget::pane{border:0;} QTabBar::tab{padding:10px 16px;color:%5;border-bottom:2px "
                "solid transparent;}"
                "QTabBar::tab:selected{color:%1;border-bottom:2px solid #3268e8;} "
                "QTabBar#profileTabs::tab{padding:7px 12px;border:1px solid transparent;border-radius:6px;}"
                "QTabBar#profileTabs::tab:selected{background:%3;border:1px solid %4;color:%1;}"
                "QMenu{background:%3;border:1px solid %4;padding:6px;}"
                "QMenu::item{padding:8px 24px;} QMenu::item:selected{background:#3268e8;color:white;} "
                "QToolTip{background:%3;color:%1;border:1px solid %4;}")
                .arg(text, bg, card, border, muted, field));
        setWindowIcon(MonitorIcon());
        tray_->setIcon(MonitorIcon());
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
    explicit DesktopWindow(bool preview, const QString &component = KdeShortcuts::applicationComponent())
        : kdeShortcuts_(component), preview_(preview), menu_(nullptr)
    {
        QString warning;
        saved_ = preview ? MigrateDesktopConfig(DesktopConfig{}) : LoadDesktopWorkspace(warning);
        configurationBlocked_ = warning.startsWith("Saved profiles could not be read.");
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
        layout->setContentsMargins(18, 14, 18, 14);
        layout->setSpacing(10);
        auto *brand = new QHBoxLayout;
        auto *logo = new QLabel;
        logo->setPixmap(MonitorIcon().pixmap(28, 28));
        auto *title = new QLabel("LG Input Switch");
        title->setObjectName("title");
        title->setFixedHeight(32);
        brand->addWidget(logo);
        brand->addWidget(title, 1);
        layout->addLayout(brand);
        controls_ = new QWidget;
        auto *body = new QVBoxLayout(controls_);
        body->setContentsMargins(0, 0, 0, 0);
        tabs_ = new QTabWidget;
        tabs_->setObjectName("mainTabs");
        body->addWidget(tabs_);
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
        profileLayout->setContentsMargins(0, 10, 0, 0);
        profileLayout->setSpacing(12);
        profileLayout->setSizeConstraint(QLayout::SetMinimumSize);
        auto *profileRow = new QHBoxLayout;
        profileTabs_ = new QTabBar;
        profileTabs_->setObjectName("profileTabs");
        profileTabs_->setExpanding(false);
        profileTabs_->setDrawBase(false);
        profileTabs_->setElideMode(Qt::ElideRight);
        profileTabs_->setUsesScrollButtons(true);
        profileTabs_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        profileTabs_->setAccessibleName("Switching profiles");
        profileRow->addWidget(profileTabs_, 1);
        addProfile_ = new QPushButton("+ Profile");
        addProfile_->setObjectName("addProfile");
        profileRow->addWidget(addProfile_);
        auto *profileOptions = new QToolButton;
        profileOptions->setText("…");
        profileOptions->setAccessibleName("Profile options");
        auto *profileMenu = new QMenu(profileOptions);
        renameProfile_ = profileMenu->addAction("Rename profile…");
        removeProfile_ = profileMenu->addAction("Remove profile");
        profileOptions->setMenu(profileMenu);
        profileOptions->setPopupMode(QToolButton::InstantPopup);
        profileRow->addWidget(profileOptions);
        profileLayout->addLayout(profileRow);
        monitorCard_ = new QFrame;
        monitorCard_->setObjectName("card");
        auto *monitorLayout = new QVBoxLayout(monitorCard_);
        monitorLayout->setContentsMargins(14, 10, 14, 12);
        auto *head = new QHBoxLayout;
        monitorSummary_ = new QLabel("Monitors");
        monitorSummary_->setObjectName("section");
        head->addWidget(monitorSummary_, 1);
        monitorDetails_ = new QToolButton;
        monitorDetails_->setCheckable(true);
        monitorDetails_->setObjectName("monitorDetails");
        monitorDetails_->setText("Manage monitors");
        head->addWidget(monitorDetails_);
        monitorLayout->addLayout(head);
        monitorTools_ = new QWidget;
        auto *monitorToolsLayout = new QHBoxLayout(monitorTools_);
        monitorToolsLayout->setContentsMargins(0, 0, 0, 0);
        addMonitor_ = new QPushButton("+ Monitor");
        addMonitor_->setObjectName("addMonitor");
        refresh_ = new QPushButton("Refresh");
        refresh_->setObjectName("refreshMonitors");
        monitorToolsLayout->addWidget(addMonitor_);
        monitorToolsLayout->addStretch();
        monitorToolsLayout->addWidget(refresh_);
        members_ = new QTableWidget(0, 3);
        members_->setObjectName("profileMonitors");
        members_->verticalHeader()->hide();
        members_->horizontalHeader()->hide();
        members_->setShowGrid(false);
        members_->setFrameShape(QFrame::NoFrame);
        members_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        members_->setSelectionMode(QAbstractItemView::NoSelection);
        members_->setColumnWidth(0, 150);
        members_->setColumnWidth(2, 100);
        members_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        monitorLayout->addWidget(members_);
        monitorLayout->addWidget(monitorTools_);
        profileLayout->addWidget(monitorCard_);
        auto *inputs = new QFrame;
        inputs->setObjectName("card");
        auto *inputLayout = new QVBoxLayout(inputs);
        inputLayout->setContentsMargins(14, 12, 14, 12);
        auto *inputHeading = new QHBoxLayout;
        auto *inputTitle = new QLabel("Inputs & actions");
        inputTitle->setObjectName("section");
        inputHeading->addWidget(inputTitle, 1);
        editProfile_ = new QPushButton("Edit actions");
        editProfile_->setCheckable(true);
        editProfile_->setObjectName("editProfile");
        inputHeading->addWidget(editProfile_);
        inputLayout->addLayout(inputHeading);
        table_ = new QTableWidget;
        table_->setObjectName("inputs");
        table_->verticalHeader()->hide();
        table_->setShowGrid(false);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setMinimumHeight(180);
        inputLayout->addWidget(table_);
        editTools_ = new QWidget;
        auto *actions = new QHBoxLayout(editTools_);
        actions->setContentsMargins(0, 0, 0, 0);
        auto *addAction = new QPushButton("+ Action");
        addAction->setObjectName("addAction");
        auto *actionOptions = new QToolButton;
        actionOptions->setText("Action options");
        actionOptions->setPopupMode(QToolButton::InstantPopup);
        auto *actionMenu = new QMenu(actionOptions);
        actionOptions->setMenu(actionMenu);
        auto *up = actionMenu->addAction("Move up");
        auto *down = actionMenu->addAction("Move down");
        actionMenu->addSeparator();
        auto *removeAction = actionMenu->addAction("Remove action");
        actions->addWidget(addAction);
        actions->addWidget(actionOptions);
        actions->addStretch();
        inputLayout->addWidget(editTools_);
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
        cycle_->setObjectName("primary");
        switches->addWidget(retry_);
        switches->addStretch();
        switches->addWidget(previous_);
        switches->addWidget(cycle_);
        inputLayout->addLayout(switches);
        profileLayout->addWidget(inputs);
        profileLayout->addStretch();
        profileScroll_ = scrollPage(profiles, "profileScroll");
        tabs_->addTab(profileScroll_, "Profiles");
        auto *preferences = new QFrame;
        preferences->setObjectName("card");
        auto *form = new QFormLayout(preferences);
        form->setContentsMargins(20, 20, 20, 20);
        form->setSpacing(16);
        appearance_ = new QComboBox;
        appearance_->setObjectName("appearance");
        appearance_->addItem("Follow system", "system");
        appearance_->addItem("Light", "light");
        appearance_->addItem("Dark", "dark");
        appearance_->setCurrentIndex(qMax(0, appearance_->findData(config_.appearance)));
        form->addRow("Appearance", appearance_);
        notifications_ = new QCheckBox("Show command notifications");
        notifications_->setChecked(config_.notifications);
        form->addRow("Notifications", notifications_);
        startup_ = new QCheckBox("Launch when I sign in");
        startup_->setObjectName("startup");
        startup_->setChecked(config_.startup);
        form->addRow("Startup", startup_);
        globalShortcuts_ = new QCheckBox("Enable global shortcuts while the app is running");
        globalShortcuts_->setObjectName("globalShortcuts");
        globalShortcuts_->setChecked(config_.globalShortcuts);
#ifdef Q_OS_WIN
        globalShortcuts_->hide();
#else
        if (kdeAvailable_ || preview_)
            form->addRow("Global shortcuts", globalShortcuts_);
        else
            globalShortcuts_->hide();
#endif
        auto *shortcutHelp = new QLabel(
            "Shortcuts belong to named profiles and work whichever tab is open. Edit them on "
            "Profiles → Edit actions. On KDE, assignments are linked with Plasma’s shortcut manager.");
        shortcutHelp->setWordWrap(true);
        form->addRow(shortcutHelp);
#ifndef Q_OS_WIN
        if (!kdeAvailable_ && !preview_)
        {
            shortcutPanel_->hide();
            auto *help = new QLabel("Global shortcuts require KDE Plasma with KGlobalAccel. Other desktops "
                                    "can bind the existing single-monitor CLI in their shortcut settings.");
            help->setWordWrap(true);
            form->addRow(help);
        }
#endif
        auto *license = new QLabel;
        license->setObjectName("license");
        license->setOpenExternalLinks(true);
        license->setWordWrap(true);
        form->addRow(license);
        auto *aboutQt = new QPushButton("About Qt");
        connect(aboutQt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(this); });
        form->addRow(aboutQt);
        tabs_->addTab(scrollPage(preferences, "preferencesScroll"), "Preferences");
        auto *advanced = new QFrame;
        advanced->setObjectName("card");
        auto *advancedForm = new QFormLayout(advanced);
        advancedForm->setContentsMargins(20, 20, 20, 20);
        advancedForm->setSpacing(16);
        debounce_ = new QSpinBox;
        debounce_->setRange(0, 10000);
        debounce_->setSuffix(" ms");
        debounce_->setValue(config_.debounce);
        advancedForm->addRow("Shortcut debounce", debounce_);
        codes_ = new QCheckBox("Show codes beside input names");
        advancedForm->addRow("Input mapping", codes_);
        auto *manage = new QPushButton("Manage saved monitors…");
        connect(manage, &QPushButton::clicked, this, [this] { manageMonitors(); });
        advancedForm->addRow("Monitor library", manage);
        auto *explanation = new QLabel(
            "Use a monitor’s options menu to change its source address or input codes. All profiles "
            "share that monitor’s mapping. Duplicate or missing identities need explicit "
            "connection selection. Commands are sent in sequence; exact simultaneous switching "
            "and physical confirmation are not guaranteed.");
        explanation->setWordWrap(true);
        advancedForm->addRow(explanation);
        tabs_->addTab(scrollPage(advanced, "advancedScroll"), "Advanced");
        auto *diagnosticsTab = new QFrame;
        diagnosticsTab->setObjectName("card");
        auto *diagnosticsLayout = new QVBoxLayout(diagnosticsTab);
        diagnosticsLayout->setContentsMargins(16, 16, 16, 16);
        auto *diagnosticTarget = new QHBoxLayout;
        diagnosticTarget->addWidget(new QLabel("Report for"));
        diagnosticProfile_ = new QComboBox;
        diagnosticProfile_->setObjectName("diagnosticProfile");
        diagnosticProfile_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        diagnosticTarget->addWidget(diagnosticProfile_, 1);
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
        diagnosticsLayout->addWidget(diagnosticReport_, 1);
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
        diagnosticsLayout->setSizeConstraint(QLayout::SetMinimumSize);
        diagnosticReport_->setMinimumHeight(180);
        tabs_->addTab(scrollPage(diagnosticsTab, "diagnosticsScroll"), "Diagnostics");
        layout->addWidget(controls_, 1);
        status_ = new QLabel;
        status_->setObjectName("status");
        status_->setWordWrap(true);
        status_->setTextFormat(Qt::PlainText);
        status_->setFixedHeight(48);
        status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(status_);
        auto *footer = new QHBoxLayout;
        auto *close = new QPushButton("Hide to tray");
        footer->addWidget(close);
        footer->addStretch();
        save_ = new QPushButton("Save settings");
        save_->setAccessibleName("Save settings");
        save_->setObjectName("primary");
        save_->setEnabled(!configurationBlocked_);
        footer->addWidget(save_);
        layout->addLayout(footer);
        tray_ = new QSystemTrayIcon(this);
        menu_ = new QMenu(this);
        tray_->setContextMenu(menu_);
        tray_->setToolTip("LG Input Switch");
        connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                showWindow();
        });
        connect(close, &QPushButton::clicked, this, [this] {
            if (tray_->isVisible() && QSystemTrayIcon::isSystemTrayAvailable())
                hide();
            else
                message("This desktop has no system tray. Keep the window open or close it to quit.");
        });
        rebuildTabs();
        populateProfile();
        connect(profileTabs_, &QTabBar::currentChanged, this, [this](int index) {
            if (populating_ || index < 0)
                return;
            captureDraft();
            activeId_ = profileTabs_->tabData(index).toString();
            populateProfile();
        });
        connect(monitorDetails_, &QToolButton::toggled, this, [this] { populateMembers(); });
        connect(editProfile_, &QPushButton::toggled, this, [this] {
            captureDraft();
            populateProfile();
        });
        connect(diagnosticProfile_, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0 && diagnosticProfile_->currentData().toString() != activeId_)
                profileTabs_->setCurrentIndex(index);
        });
        connect(addProfile_, &QPushButton::clicked, this, [this] { addProfile(); });
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
            rebuildTabs();
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
            rebuildTabs();
            populateProfile();
            dirty();
        });
        connect(addMonitor_, &QPushButton::clicked, this, [this] { addMonitor(); });
        connect(refresh_, &QPushButton::clicked, this, [this] { refresh(); });
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
        connect(removeAction, &QAction::triggered, this, [this] {
            captureDraft();
            const int row = table_->currentRow();
            if (row < 0 || active().actions.size() <= 1)
                return;
            active().actions.removeAt(row);
            populateProfile();
            dirty();
        });
        auto move = [this](int delta) {
            const int row = table_->currentRow(), next = row + delta;
            if (row < 0 || next < 0 || next >= table_->rowCount())
                return;
            captureDraft();
            active().actions.swapItemsAt(row, next);
            populateProfile();
            table_->selectRow(next);
            dirty();
        };
        connect(up, &QAction::triggered, this, [move] { move(-1); });
        connect(down, &QAction::triggered, this, [move] { move(1); });
        connect(table_, &QTableWidget::itemChanged, this, [this] { dirty(); });
        connect(codes_, &QCheckBox::toggled, this, [this] {
            captureDraft();
            populateProfile();
        });
        connect(appearance_, &QComboBox::currentIndexChanged, this, [this] {
            theme();
            dirty();
        });
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { theme(); });
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
        QObject::disconnect(qApp, nullptr, this, nullptr);
        QObject::disconnect(qApp->styleHints(), nullptr, this, nullptr);
#ifdef Q_OS_WIN
        for (const auto &item : hotkeys_)
            UnregisterHotKey(nullptr, item.first);
#endif
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
            }
            menu_->popup(mapToGlobal(QPoint(100, 100)));
            qApp->processEvents();
            menu_->grab().save(directory + "/tray-" + mode + ".png");
            menu_->hide();
        }
        tabs_->setCurrentIndex(0);
        if (preview_)
        {
            activeId_ = "preview-group";
            rebuildTabs();
            populateProfile();
            for (const auto &mode : {QString("light"), QString("dark")})
            {
                appearance_->setCurrentIndex(appearance_->findData(mode));
                dirty_ = false;
                save_->setText("Save settings");
                updateActions();
                qApp->processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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
    QApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitch");
    app.setApplicationName("LGInputSwitchDesktop");
    app.setApplicationVersion("1.2.0");
    app.setDesktopFileName("lginputswitch");
    app.setQuitOnLastWindowClosed(false);
    app.setStyle("Fusion");
    QCommandLineParser parser;
    parser.setApplicationDescription("LG monitor input switching with a tray and global shortcuts.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"preview", "Show synthetic data without accessing settings or hardware."});
    parser.addOption({"background", "Start in the tray when a tray host is available."});
    parser.addOption({"screenshot-dir", "Render design previews (requires --preview).", "directory"});
    parser.process(app);
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
    if (preview || !parser.isSet("background") || !QSystemTrayIcon::isSystemTrayAvailable())
        window.show();
    if (preview && parser.isSet("screenshot-dir"))
        QTimer::singleShot(300, &window, [&] {
            window.capture(parser.value("screenshot-dir"));
            app.quit();
        });
    return app.exec();
}

#endif
