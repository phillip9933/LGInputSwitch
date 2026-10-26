#include "../core/input_value.h"
#include "platform.h"
#include <QtConcurrent>
#include <QtWidgets>
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
        p.end();
        icon.addPixmap(image);
    }
    return icon;
}

class DesktopWindow final : public QWidget, public QAbstractNativeEventFilter
{
    DesktopConfig config_;
    bool preview_, busy_ = false, dirty_ = false, quitting_ = false;
    int position_ = -1;
    QThreadPool worker_;
    QSystemTrayIcon *tray_;
    QMenu *menu_;
    QComboBox *monitors_, *appearance_;
    QLabel *status_, *subtitle_;
    QTableWidget *table_;
    QLineEdit *source_;
    QSpinBox *debounce_;
    QCheckBox *notifications_, *startup_;
    QKeySequenceEdit *cycleShortcut_;
    QPushButton *refresh_, *save_, *cycle_;
    QWidget *controls_;
    QVector<DesktopMonitor> detected_;
    QElapsedTimer lastSend_;
#ifdef Q_OS_WIN
    std::map<int, QString> hotkeys_;
#endif
    void showWindow()
    {
        showNormal();
        raise();
        activateWindow();
    }
    void dirty()
    {
        if (!dirty_)
        {
            dirty_ = true;
            save_->setText("Save changes");
            rebuildMenu();
        }
    }
    void message(const QString &text)
    {
        status_->setText(text);
    }
    void setBusy(bool value)
    {
        busy_ = value;
        monitors_->setEnabled(!value);
        controls_->setEnabled(!value);
        refresh_->setEnabled(!value);
        save_->setEnabled(!value);
        cycle_->setEnabled(!value);
        rebuildMenu();
    }
    DesktopConfig collect() const
    {
        auto c = config_;
        c.inputs.clear();
        for (int i = 0; i < table_->rowCount(); ++i)
        {
            auto *shortcut = qobject_cast<QKeySequenceEdit *>(table_->cellWidget(i, 3));
            c.inputs.push_back({table_->item(i, 1)->text().trimmed(), table_->item(i, 2)->text().trimmed(),
                                shortcut
                                    ? shortcut->keySequence().toString(QKeySequence::PortableText).toUpper()
                                    : QString{},
                                table_->item(i, 0)->checkState() == Qt::Checked});
        }
        c.source = source_->text().trimmed();
        c.debounce = debounce_->value();
        c.appearance = appearance_->currentData().toString();
        c.notifications = notifications_->isChecked();
        c.startup = startup_->isChecked();
        c.cycleShortcut = cycleShortcut_->keySequence().toString(QKeySequence::PortableText).toUpper();
        int selected = monitors_->currentIndex();
        if (selected >= 0 && selected < detected_.size())
        {
            const auto &m = detected_[selected];
            c.adapter = m.adapter;
            c.display = m.display;
            c.bus = m.bus;
        }
        return c;
    }
    void populate()
    {
        QSignalBlocker block(table_);
        table_->setRowCount(config_.inputs.size());
        for (int i = 0; i < config_.inputs.size(); ++i)
        {
            const auto &row = config_.inputs[i];
            auto *enabled = new QTableWidgetItem;
            enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            enabled->setCheckState(row.enabled ? Qt::Checked : Qt::Unchecked);
            table_->setItem(i, 0, enabled);
            table_->setItem(i, 1, new QTableWidgetItem(row.label));
            table_->setItem(i, 2, new QTableWidgetItem(row.code));
            auto *shortcut =
                new QKeySequenceEdit(QKeySequence::fromString(row.shortcut, QKeySequence::PortableText));
            shortcut->setMaximumSequenceLength(1);
#ifdef Q_OS_WIN
            connect(shortcut, &QKeySequenceEdit::keySequenceChanged, this, [this] { dirty(); });
#else
            shortcut->setEnabled(false);
            shortcut->setToolTip("Configure global shortcuts in your desktop's keyboard settings.");
#endif
            table_->setCellWidget(i, 3, shortcut);
            auto *send = new QPushButton("Switch");
            send->setToolTip("Send this input command to the saved monitor");
            connect(send, &QPushButton::clicked, this, [this, i] { sendInput(i); });
            auto *cell = new QWidget;
            auto *cellLayout = new QHBoxLayout(cell);
            cellLayout->setContentsMargins(6, 5, 6, 5);
            cellLayout->addWidget(send);
            table_->setCellWidget(i, 4, cell);
            table_->setRowHeight(i, 46);
        }
    }
    bool validate(const DesktopConfig &c, QString &error)
    {
        unsigned int value;
        if (!ParseInputValue(c.source.toStdString(), 0xff, value))
        {
            error = "Enter a valid source address, such as 0x50.";
            return false;
        }
        QSet<QString> labels, shortcuts;
        int enabled = 0;
        auto validateShortcut = [&](const QString &text) {
            if (text.isEmpty())
                return true;
#ifdef Q_OS_WIN
            HotkeySpec spec{};
            if (!ParseHotkey(text.toStdString(), spec))
                return false;
#endif
            if (shortcuts.contains(text))
                return false;
            shortcuts.insert(text);
            return true;
        };
        if (!validateShortcut(c.cycleShortcut))
        {
            error = "Choose a supported, unique cycle shortcut.";
            return false;
        }
        for (const auto &row : c.inputs)
        {
            if (row.label.isEmpty() || labels.contains(row.label) ||
                !ParseInputValue(row.code.toStdString(), 0xffff, value))
            {
                error = "Each input needs a unique name and a valid code. Open Advanced to review codes.";
                return false;
            }
            labels.insert(row.label);
            if (row.enabled)
                ++enabled;
            if (!validateShortcut(row.shortcut))
            {
                error = "Shortcuts must be unique. Use Ctrl/Alt/Shift with a letter, number or F-key.";
                return false;
            }
        }
        if (!enabled)
        {
            error = "Enable at least one input.";
            return false;
        }
        return true;
    }
    void save()
    {
        if (preview_)
        {
            message("Design preview · nothing is saved and no monitor commands are sent.");
            return;
        }
        auto next = collect();
        QString error;
        if (!validate(next, error) || !SaveDesktopConfig(next, error))
        {
            message(error);
            return;
        }
        config_ = next;
        dirty_ = false;
        position_ = -1;
        save_->setText("Saved");
        message(monitors_->currentIndex() < 0
                    ? "Settings saved. Select an available monitor before switching."
                    : "Settings saved. Ready to switch.");
        registerHotkeys();
        rebuildMenu();
#ifdef Q_OS_WIN
        if (!SetStartWithWindows(config_.startup))
            message("Settings saved, but Windows startup could not be updated.");
#endif
    }
    void registerHotkeys()
    {
        if (preview_)
            return;
#ifdef Q_OS_WIN
        for (const auto &item : hotkeys_)
            UnregisterHotKey(nullptr, item.first);
        hotkeys_.clear();
        int id = 1;
        bool failed = false;
        auto bind = [&](const QString &text, const QString &label) {
            if (text.isEmpty())
                return;
            HotkeySpec spec{};
            if (ParseHotkey(text.toStdString(), spec) &&
                RegisterHotKey(nullptr, id, spec.fsModifiers | MOD_NOREPEAT, spec.vk))
                hotkeys_[id] = label;
            else
                failed = true;
            ++id;
        };
        bind(config_.cycleShortcut, {});
        for (const auto &row : config_.inputs)
            if (row.enabled)
                bind(row.shortcut, row.label);
        if (failed)
            message(
                "Some shortcuts are invalid or in use by another app. Choose different shortcuts and save.");
#endif
    }
    void sendInput(int index)
    {
        if (preview_)
        {
            message("Design preview · switching is disabled.");
            return;
        }
        if (busy_)
            return;
        if (dirty_)
        {
            message("Save your changes before switching.");
            showWindow();
            return;
        }
        if (monitors_->currentIndex() < 0)
        {
            message("Select an available monitor and save first.");
            showWindow();
            return;
        }
        if (lastSend_.isValid() && lastSend_.elapsed() < config_.debounce)
            return;
        if (index < 0)
        {
            int next = position_;
            for (int n = 0; n < config_.inputs.size(); ++n)
            {
                next = (next + 1) % config_.inputs.size();
                if (config_.inputs[next].enabled)
                {
                    index = next;
                    break;
                }
            }
        }
        if (index < 0 || index >= config_.inputs.size() || !config_.inputs[index].enabled)
            return;
        lastSend_.restart();
        setBusy(true);
        message("Sending input command…");
        const auto snapshot = config_;
        const auto row = config_.inputs[index];
        auto *watcher = new QFutureWatcher<lg::SwitchResult>(this);
        connect(watcher, &QFutureWatcher<lg::SwitchResult>::finished, this, [this, watcher, index, row] {
            auto result = watcher->result();
            watcher->deleteLater();
            setBusy(false);
            if (result.sent())
                position_ = index;
            const auto text =
                result.sent()
                    ? row.label + " command sent. Check the monitor; this input path has no confirmation."
                    : QString::fromStdString(result.detail);
            message(text);
            if (config_.notifications && tray_->isVisible())
                tray_->showMessage("LG Input Switch", text,
                                   result.sent() ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning);
        });
        watcher->setFuture(
            QtConcurrent::run(&worker_, [snapshot, row] { return SendDesktopInput(snapshot, row.code); }));
    }
    void refresh()
    {
        if (busy_)
            return;
        setBusy(true);
        message("Looking for connected monitors…");
        auto *watcher = new QFutureWatcher<Discovery>(this);
        connect(watcher, &QFutureWatcher<Discovery>::finished, this, [this, watcher] {
            const auto result = watcher->result();
            watcher->deleteLater();
            detected_ = result.monitors;
            QSignalBlocker blocker(monitors_);
            monitors_->clear();
            int selected = -1;
            for (int i = 0; i < detected_.size(); ++i)
            {
                const auto &m = detected_[i];
                monitors_->addItem(m.label);
                if (m.adapter == config_.adapter && m.display == config_.display && m.bus == config_.bus)
                    selected = i;
            }
            // Never silently select a different monitor after topology changes.
            monitors_->setCurrentIndex(selected);
            setBusy(false);
            message(result.message);
        });
        watcher->setFuture(QtConcurrent::run(&worker_, [] { return DiscoverDesktopMonitors(); }));
    }
    void rebuildMenu()
    {
        menu_->clear();
        auto *open = menu_->addAction("Open LG Input Switch");
        connect(open, &QAction::triggered, this, [this] { showWindow(); });
        menu_->addSeparator();
        auto *cycle = menu_->addAction("Cycle inputs");
        cycle->setEnabled(!busy_ && !dirty_ && monitors_->currentIndex() >= 0);
        connect(cycle, &QAction::triggered, this, [this] { sendInput(-1); });
        for (int i = 0; i < config_.inputs.size(); ++i)
            if (config_.inputs[i].enabled)
            {
                auto *action = menu_->addAction(config_.inputs[i].label);
                action->setEnabled(!busy_ && !dirty_ && monitors_->currentIndex() >= 0);
                connect(action, &QAction::triggered, this, [this, i] { sendInput(i); });
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
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(muted));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
        qApp->setPalette(palette);
        qApp->setStyleSheet(
            QString("QWidget{font-size:13px;color:%1;} QWidget#root{background:%2;}"
                    "QFrame#card{background:%3;border:1px solid %4;border-radius:12px;}"
                    "QLabel#title{font-size:27px;font-weight:700;} QLabel#muted,QLabel#status{color:%5;} "
                    "QLabel#section{font-size:16px;font-weight:600;}"
                    "QPushButton{padding:8px 14px;background:%3;border:1px solid %4;border-radius:7px;} "
                    "QPushButton:hover{border-color:#3268e8;}"
                    "QPushButton:focus,QLineEdit:focus,QComboBox:focus{border:2px solid #3268e8;}"
                    "QPushButton#primary{background:#3268e8;color:white;border:1px solid "
                    "#3268e8;font-weight:600;}"
                    "QPushButton:disabled{color:%5;} "
                    "QLineEdit,QSpinBox,QComboBox,QKeySequenceEdit{background:%6;border:1px solid "
                    "%4;border-radius:6px;padding:7px;}"
                    "QTableWidget{background:%3;border:0;gridline-color:%4;selection-background-color:%6;}"
                    "QHeaderView::section{background:%3;color:%5;border:0;border-bottom:1px solid "
                    "%4;padding:10px;text-align:left;}"
                    "QTabWidget::pane{border:0;} QTabBar::tab{padding:10px 16px;color:%5;border-bottom:2px "
                    "solid transparent;}"
                    "QTabBar::tab:selected{color:%1;border-bottom:2px solid #3268e8;} "
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
                int index = -1;
                if (!found->second.isEmpty())
                    for (int i = 0; i < config_.inputs.size(); ++i)
                        if (config_.inputs[i].label == found->second)
                            index = i;
                sendInput(index);
                return true;
            }
        }
#else
        Q_UNUSED(raw);
#endif
        return false;
    }

  public:
    explicit DesktopWindow(bool preview) : preview_(preview)
    {
        QString warning;
        if (!preview)
            config_ = LoadDesktopConfig(warning);
        worker_.setMaxThreadCount(1);
        setObjectName("root");
        setWindowTitle("LG Input Switch");
        resize(900, 740);
        setMinimumSize(800, 740);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 24, 28, 20);
        layout->setSpacing(12);
        auto *title = new QLabel("LG Input Switch");
        title->setObjectName("title");
        title->setFixedHeight(40);
        auto *brand = new QHBoxLayout;
        auto *logo = new QLabel;
        logo->setPixmap(MonitorIcon().pixmap(38, 38));
        brand->addWidget(logo);
        brand->addWidget(title, 1);
        layout->addLayout(brand);
        subtitle_ = new QLabel("Your display. One shortcut away.");
        subtitle_->setObjectName("muted");
        subtitle_->setFixedHeight(24);
        layout->addWidget(subtitle_);
        auto *monitorCard = new QFrame;
        monitorCard->setObjectName("card");
        monitorCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *monitorLayout = new QVBoxLayout(monitorCard);
        monitorLayout->setContentsMargins(18, 16, 18, 16);
        auto *heading = new QLabel("Monitor");
        heading->setObjectName("section");
        monitorLayout->addWidget(heading);
        auto *targetRow = new QHBoxLayout;
        monitors_ = new QComboBox;
        monitors_->setObjectName("monitors");
        monitors_->setMinimumWidth(400);
        monitors_->setPlaceholderText("Select a monitor…");
        refresh_ = new QPushButton("Refresh");
        targetRow->addWidget(monitors_, 1);
        targetRow->addWidget(refresh_);
        monitorLayout->addLayout(targetRow);
        layout->addWidget(monitorCard);
        controls_ = new QWidget;
        auto *body = new QVBoxLayout(controls_);
        body->setContentsMargins(0, 0, 0, 0);
        auto *tabs = new QTabWidget;
        body->addWidget(tabs);
        auto *inputs = new QFrame;
        inputs->setObjectName("card");
        auto *inputLayout = new QVBoxLayout(inputs);
        inputLayout->setContentsMargins(16, 16, 16, 16);
        auto *inputHeading = new QLabel("Inputs & shortcuts");
        inputHeading->setObjectName("section");
        inputLayout->addWidget(inputHeading);
        auto *hint = new QLabel("Enable the inputs you use. Reorder them to choose the cycle sequence.");
        hint->setObjectName("muted");
        inputLayout->addWidget(hint);
        table_ = new QTableWidget(0, 5);
        table_->setObjectName("inputs");
        table_->setHorizontalHeaderLabels({"Use", "Input", "Code", "Shortcut", ""});
        table_->verticalHeader()->hide();
        table_->setShowGrid(false);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setColumnWidth(0, 44);
        table_->setColumnWidth(2, 84);
        table_->setColumnWidth(3, 170);
        table_->setColumnWidth(4, 90);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table_->setMinimumHeight(260);
        inputLayout->addWidget(table_);
        auto *order = new QHBoxLayout;
        auto *up = new QPushButton("Move up");
        auto *down = new QPushButton("Move down");
        order->addWidget(up);
        order->addWidget(down);
        order->addStretch();
        cycle_ = new QPushButton("Cycle inputs");
        cycle_->setObjectName("primary");
        order->addWidget(cycle_);
        inputLayout->addLayout(order);
        tabs->addTab(inputs, "Inputs");
        auto *preferences = new QFrame;
        preferences->setObjectName("card");
        auto *form = new QFormLayout(preferences);
        form->setContentsMargins(20, 24, 20, 24);
        form->setSpacing(18);
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
        startup_ = new QCheckBox("Launch when I sign in to Windows", preferences);
        startup_->setChecked(config_.startup);
        cycleShortcut_ =
            new QKeySequenceEdit(QKeySequence::fromString(config_.cycleShortcut, QKeySequence::PortableText));
        cycleShortcut_->setParent(preferences);
        cycleShortcut_->setMaximumSequenceLength(1);
#ifdef Q_OS_WIN
        form->addRow("Startup", startup_);
        form->addRow("Cycle shortcut", cycleShortcut_);
#else
        startup_->hide();
        cycleShortcut_->hide();
        auto *shortcutHelp =
            new QLabel("Global shortcuts are managed by your Linux desktop. Bind the lginputswitch CLI in "
                       "keyboard settings. Tray actions work without a global shortcut.");
        shortcutHelp->setWordWrap(true);
        form->addRow("Linux shortcuts", shortcutHelp);
        table_->hideColumn(3);
#endif
        tabs->addTab(preferences, "Preferences");
        auto *advanced = new QFrame;
        advanced->setObjectName("card");
        auto *advancedForm = new QFormLayout(advanced);
        advancedForm->setContentsMargins(20, 24, 20, 24);
        advancedForm->setSpacing(18);
        source_ = new QLineEdit(config_.source);
        source_->setMaximumWidth(160);
        advancedForm->addRow("I²C source address", source_);
        debounce_ = new QSpinBox;
        debounce_->setRange(0, 10000);
        debounce_->setSuffix(" ms");
        debounce_->setValue(config_.debounce);
        advancedForm->addRow("Shortcut debounce", debounce_);
        auto *codes = new QCheckBox("Show editable input codes on the Inputs tab");
        advancedForm->addRow("Input mapping", codes);
        table_->hideColumn(2);
        auto *explanation = new QLabel("LG input codes vary by monitor. Keep known-working values. A command "
                                       "can be sent successfully without the monitor confirming a switch.");
        explanation->setWordWrap(true);
        advancedForm->addRow(explanation);
        tabs->addTab(advanced, "Advanced");
        layout->addWidget(controls_, 1);
        status_ = new QLabel;
        status_->setObjectName("status");
        status_->setWordWrap(true);
        status_->setFixedHeight(48);
        layout->addWidget(status_);
        auto *footer = new QHBoxLayout;
        auto *close = new QPushButton("Hide to tray");
        footer->addWidget(close);
        footer->addStretch();
        save_ = new QPushButton("Save settings");
        save_->setAccessibleName("Save settings");
        save_->setObjectName("primary");
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
                message("This desktop has no system tray. Keep this window open, or use Quit from the window "
                        "menu.");
        });
        connect(save_, &QPushButton::clicked, this, [this] { save(); });
        connect(refresh_, &QPushButton::clicked, this, [this] { refresh(); });
        connect(cycle_, &QPushButton::clicked, this, [this] { sendInput(-1); });
        connect(codes, &QCheckBox::toggled, this, [this](bool show) { table_->setColumnHidden(2, !show); });
        auto move = [this](int delta) {
            int row = table_->currentRow();
            int next = row + delta;
            if (row < 0 || next < 0 || next >= table_->rowCount())
                return;
            config_.inputs = collect().inputs;
            config_.inputs.swapItemsAt(row, next);
            populate();
            table_->selectRow(next);
            dirty();
        };
        connect(up, &QPushButton::clicked, this, [move] { move(-1); });
        connect(down, &QPushButton::clicked, this, [move] { move(1); });
        populate();
        connect(table_, &QTableWidget::itemChanged, this, [this] { dirty(); });
        connect(monitors_, &QComboBox::currentIndexChanged, this, [this] {
            dirty();
            rebuildMenu();
        });
        connect(appearance_, &QComboBox::currentIndexChanged, this, [this] {
            theme();
            dirty();
        });
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { theme(); });
        connect(source_, &QLineEdit::textChanged, this, [this] { dirty(); });
        connect(debounce_, &QSpinBox::valueChanged, this, [this] { dirty(); });
        connect(notifications_, &QCheckBox::toggled, this, [this] { dirty(); });
        connect(startup_, &QCheckBox::toggled, this, [this] { dirty(); });
        connect(cycleShortcut_, &QKeySequenceEdit::keySequenceChanged, this, [this] { dirty(); });
#ifdef Q_OS_WIN
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *current) {
            bool capture = false;
            for (auto *widget = current; widget; widget = widget->parentWidget())
                if (qobject_cast<QKeySequenceEdit *>(widget))
                {
                    capture = true;
                    break;
                }
            if (capture)
            {
                for (const auto &item : hotkeys_)
                    UnregisterHotKey(nullptr, item.first);
                hotkeys_.clear();
            }
            else if (!preview_)
                registerHotkeys();
        });
#endif
        qApp->installNativeEventFilter(this);
        theme();
        rebuildMenu();
        if (!preview_)
        {
            tray_->show();
            registerHotkeys();
            refresh();
        }
        else
        {
            detected_.push_back({"LG UltraGear · Design preview", 0, 1, true});
            monitors_->addItem(detected_[0].label);
            monitors_->setCurrentIndex(0);
            dirty_ = false;
            save_->setText("Save settings");
            message("Design preview · no settings are saved and no hardware commands are sent.");
        }
        if (!warning.isEmpty())
            QTimer::singleShot(100, this, [this, warning] { message(warning); });
    }
    ~DesktopWindow() override
    {
#ifdef Q_OS_WIN
        for (const auto &item : hotkeys_)
            UnregisterHotKey(nullptr, item.first);
#endif
        worker_.waitForDone();
    }
    void capture(const QString &directory)
    {
        QDir().mkpath(directory);
        auto *tabs = findChild<QTabWidget *>();
        for (const auto &mode : {QString("light"), QString("dark")})
        {
            appearance_->setCurrentIndex(appearance_->findData(mode));
            dirty_ = false;
            save_->setText("Save settings");
            rebuildMenu();
            for (int tab = 0; tab < tabs->count(); ++tab)
            {
                tabs->setCurrentIndex(tab);
                qApp->processEvents();
                const auto suffix = tab == 0 ? QString{} : "-" + tabs->tabText(tab).toLower();
                grab().save(directory + "/desktop-" + mode + suffix + ".png");
            }
            menu_->popup(mapToGlobal(QPoint(100, 100)));
            qApp->processEvents();
            menu_->grab().save(directory + "/tray-" + mode + ".png");
            menu_->hide();
        }
        tabs->setCurrentIndex(0);
    }
};

#ifndef LG_DESKTOP_TEST
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitch");
    app.setApplicationName("LGInputSwitchDesktop");
    app.setQuitOnLastWindowClosed(false);
    app.setStyle("Fusion");
    const QStringList args = app.arguments();
    const bool preview = args.contains("--preview");
    std::unique_ptr<QLockFile> lock;
    if (!preview)
    {
        auto path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(path);
        lock = std::make_unique<QLockFile>(path + "/desktop.lock");
        if (!lock->tryLock())
        {
            QMessageBox::information(nullptr, "LG Input Switch",
                                     "The desktop app is already running. Open it from the tray.");
            return 0;
        }
    }
    DesktopWindow window(preview);
    window.show();
    const int screenshot = args.indexOf("--screenshot-dir");
    if (preview && screenshot >= 0 && screenshot + 1 < args.size())
        QTimer::singleShot(300, &window, [&] {
            window.capture(args[screenshot + 1]);
            app.quit();
        });
    return app.exec();
}

#endif
