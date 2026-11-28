#include "monitor_input_editor.h"
#include "../core/input_value.h"
#include "monitor_inputs.h"
#include "windows_monitors.h"
#include <QtConcurrent>
#include <QtWidgets>

MonitorInputEditor::MonitorInputEditor(QWidget *parent, SavedMonitor next, const DesktopWorkspace &workspace,
    std::function<MonitorResolution()> resolve, QThreadPool *worker, bool preview,
    std::function<bool(const SavedMonitor &, QString &)> save, std::function<void()> cancel)
    : QWidget(parent)
{
    auto *editor = this;
    const auto id = next.id;
    auto *layout = new QVBoxLayout(editor);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *advanced = new QWidget;
    advanced->hide();
    editor->setObjectName("inlineMonitorEditor");
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
    connect(method, &QComboBox::currentIndexChanged, editor,
            [method, source] { source->setEnabled(method->currentData() == "lg"); });
    auto *methodHint =
        new QLabel("Standard switching uses input control 0x60 and source 0x51. Input values still "
                   "depend on the monitor. After changing methods, review the codes or load presets; "
                   "custom LG codes are not translated automatically.");
    methodHint->setWordWrap(true);
    const auto connection = resolve();
    if (connection.available && !SupportsLgAlternate(connection.target))
    {
        auto *options = qobject_cast<QStandardItemModel *>(method->model());
        options->item(method->findData("lg"))->setEnabled(false);
    }
    if (connection.available && connection.target.hardware["nvidia_ddc"].toBool())
        methodHint->setText("LG alternate uses the NVIDIA driver for this display. Standard DDC/CI "
                            "is also available for monitors with standard input switching.");
    else if (connection.available && connection.target.hardware["adapter_kind"] == "intel")
        methodHint->setText("This Intel connection uses standard DDC/CI for input switching and brightness. "
                            "LG alternate switching is not yet implemented for Intel. "
                            "Use Standard only if your monitor supports standard input commands.");
    else if (connection.available && connection.target.transport == WindowsDdcTransport)
        methodHint->setText("This connection supports standard DDC/CI (0x60). LG alternate switching "
            "needs a custom source address that Windows DDC/CI cannot send through this connection. "
            "To test standard switching, select Standard and load its method presets. "
            "Some LG monitors only support alternate switching through a direct GPU connection.");
    form->addRow(methodHint);
    advanced->setLayout(form);
    auto *automatic = new QCheckBox("Read and set up this monitor’s inputs automatically");
    automatic->setObjectName("automaticMonitorInputs");
    automatic->setChecked(next.autoInputs);
    layout->addWidget(automatic);
    auto *inputs = new QTableWidget(0, 2);
    inputs->setObjectName("monitorInputs");
    inputs->setHorizontalHeaderLabels({"Input name", "Code"});
    inputs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    auto append = [inputs, automatic](const DesktopInput &input) {
        const int row = inputs->rowCount();
        inputs->insertRow(row);
        auto *label = new QTableWidgetItem(input.label);
        label->setData(Qt::UserRole, input.id);
        label->setCheckState(input.enabled ? Qt::Checked : Qt::Unchecked);
        label->setToolTip("Show this input in the monitor and tray switchers");
        inputs->setItem(row, 0, label);
        auto *code = new QTableWidgetItem(input.code);
        code->setFlags(code->flags().setFlag(Qt::ItemIsEditable, !automatic->isChecked()));
        inputs->setItem(row, 1, code);
    };
    for (const auto &input : next.inputs)
        append(input);
    inputs->verticalHeader()->hide();
    inputs->setShowGrid(false);
    inputs->setSelectionBehavior(QAbstractItemView::SelectRows);
    inputs->setSelectionMode(QAbstractItemView::SingleSelection);
    inputs->setMinimumHeight(150);
    inputs->setColumnHidden(1, true);
    layout->addWidget(inputs);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton("Add input");
    auto *remove = new QPushButton("Remove input");
    add->setObjectName("addMonitorInput");
    remove->setObjectName("removeMonitorInput");
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch();
    layout->addLayout(row);
    auto *error = new QLabel;
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    auto updateButtons = [=] {
        add->setEnabled(inputs->rowCount() < 64);
        remove->setEnabled(inputs->currentRow() >= 0 && inputs->rowCount() > 1);
        remove->setToolTip(inputs->rowCount() <= 1 ? "Keep at least one input."
                            : inputs->currentRow() < 0 ? "Select an input to remove." : QString{});
    };
    connect(inputs, &QTableWidget::currentCellChanged, editor, updateButtons);
    connect(inputs->model(), &QAbstractItemModel::rowsInserted, editor, updateButtons);
    connect(inputs->model(), &QAbstractItemModel::rowsRemoved, editor, updateButtons);
    updateButtons();
    connect(add, &QPushButton::clicked, editor, [=] {
        automatic->setChecked(false);
        append({"New input", {}, {}, true, NewDesktopId()});
        const int row = inputs->rowCount() - 1;
        inputs->setCurrentCell(row, 0);
        inputs->scrollToItem(inputs->item(row, 0));
        inputs->editItem(inputs->item(row, 0));
        error->setText("Enter the input name and its monitor code. Saving keeps this list in manual mode.");
    });
    connect(remove, &QPushButton::clicked, editor, [=] {
        const int row = inputs->currentRow();
        if (row < 0 || inputs->rowCount() <= 1) return;
        const auto *input = inputs->item(row, 0);
        const auto inputId = input->data(Qt::UserRole).toString();
        QStringList affected;
        for (const auto &profile : workspace.profiles)
            for (const auto &action : profile.actions)
                if (action.inputs.value(id) == inputId)
                    affected << profile.name + " / " + action.name;
        if (!affected.isEmpty() &&
            QMessageBox::question(editor, "Remove input?",
                QString("Removing %1 will disable these actions when you save:\n%2")
                    .arg(input->text(), affected.join("\n")),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        automatic->setChecked(false);
        inputs->removeRow(row);
        inputs->setCurrentCell(qMin(row, inputs->rowCount() - 1), 0);
        error->setText("Save inputs to keep the manual list, or cancel to restore it.");
    });
    auto *presets = new QPushButton("Load method presets");
    presets->setObjectName("loadMonitorPresets");
    auto *saveInputs = saveInputs_ = new QPushButton("Save inputs");
    saveInputs->setObjectName("primary");
    saveInputs->setAccessibleName("Save inputs");
    auto *cancelInputs = new QPushButton("Cancel");
    cancelInputs->setObjectName("cancelInputs");
    auto *advertised = new QPushButton("Read advertised inputs");
    advertised->setObjectName("readMonitorInputs");
    auto *discoveryButtons = new QHBoxLayout;
    discoveryButtons->addWidget(presets);
    discoveryButtons->addWidget(advertised);
    auto *more = new QToolButton;
    more->setText("Advanced");
    more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    more->setArrowType(Qt::RightArrow);
    more->setCheckable(true);
    layout->addWidget(more, 0, Qt::AlignLeft);
    layout->addWidget(advanced);
    form->addRow(discoveryButtons);
    connect(more, &QToolButton::toggled, editor, [=](bool open) {
        more->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        advanced->setVisible(open);
        inputs->setColumnHidden(1, !open && automatic->isChecked());
    });
    error->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout->addWidget(error);
    connect(presets, &QPushButton::clicked, editor, [=] {
        if (QMessageBox::question(editor, "Load input presets",
                                  "Replace this monitor’s input list with the selected method’s common "
                                  "inputs? Existing action mappings that cannot be matched by input name "
                                  "will need to be selected again before saving.") != QMessageBox::Yes)
            return;
        const auto resolved = resolve();
        auto defaults = DefaultMonitorInputs(method->currentData().toString(),
                                            resolved.available ? resolved.target.inputProfile : QString{});
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
    connect(advertised, &QPushButton::clicked, editor, [=] {
        if (preview)
        {
            error->setText("Design preview · monitor queries are disabled.");
            return;
        }
        const auto resolution = resolve();
        if (!resolution.available)
        {
            error->setText("Select this monitor’s connection before reading advertised inputs.");
            return;
        }
        auto query = next;
        query.method = method->currentData().toString();
        inputs->setEnabled(false);
        automatic->setEnabled(false);
        add->setEnabled(false);
        remove->setEnabled(false);
        presets->setEnabled(false);
        advertised->setEnabled(false);
        saveInputs->setEnabled(false);
        error->setText("Reading this monitor’s advertised inputs…");
        method->setEnabled(false);
        auto *watcher = new QFutureWatcher<MonitorInputRead>(editor);
        connect(watcher, &QFutureWatcher<MonitorInputRead>::finished, editor, [=] {
            const auto read = watcher->result();
            watcher->deleteLater();
            advertised->setEnabled(true);
            saveInputs->setEnabled(true);
            method->setEnabled(true);
            if (read.complete)
            {
                if (automatic->isChecked())
                {
                    auto detectedWorkspace = workspace;
                    for (auto &m : detectedWorkspace.monitors)
                        if (m.id == id)
                        {
                            m.autoInputs = true;
                            m.method = method->currentData().toString();
                        }
                    ApplyMonitorInputs(detectedWorkspace, id, read);
                    const auto *detected = FindMonitor(detectedWorkspace, id);
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
            inputs->setEnabled(true);
            automatic->setEnabled(true);
            presets->setEnabled(!automatic->isChecked());
            updateButtons();
            error->setText(read.status);
        });
        watcher->setFuture(QtConcurrent::run(
            worker, [query, target = resolution.target] { return ReadMonitorInputs(query, target); }));
    });
    auto editingState = [=] {
        inputs->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        for (int i = 0; i < inputs->rowCount(); ++i)
        {
            auto *code = inputs->item(i, 1);
            code->setFlags(code->flags().setFlag(Qt::ItemIsEditable, !automatic->isChecked()));
        }
        inputs->setColumnHidden(1, automatic->isChecked() && !more->isChecked());
        presets->setEnabled(!automatic->isChecked());
    };
    connect(automatic, &QCheckBox::toggled, editor, editingState);
    editingState();
    auto *footer = new QHBoxLayout;
    footer->addStretch();
    footer->addWidget(cancelInputs);
    footer->addWidget(saveInputs);
    layout->addLayout(footer);
    changed_ = [=] {
        if (name->text().trimmed() != next.name || source->text().trimmed() != next.source ||
            method->currentData() != next.method || automatic->isChecked() != next.autoInputs ||
            inputs->rowCount() != next.inputs.size()) return true;
        for (int i = 0; i < inputs->rowCount(); ++i)
        {
            const auto *label = inputs->item(i, 0);
            const auto &old = next.inputs[i];
            if (label->data(Qt::UserRole) != old.id || label->text().trimmed() != old.label ||
                (label->checkState() == Qt::Checked) != old.enabled ||
                inputs->item(i, 1)->text().trimmed() != old.code) return true;
        }
        return false;
    };
    connect(cancelInputs, &QPushButton::clicked, editor, cancel);
    connect(saveInputs, &QPushButton::clicked, editor, [=]() mutable {
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
        for (const auto &m : workspace.monitors)
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
                {label, code, {}, inputs->item(i, 0)->checkState() == Qt::Checked,
                 inputs->item(i, 0)->data(Qt::UserRole).toString()});
        }
        QString problem;
        if (!save(next, problem)) error->setText(problem);
    });
}

void MonitorInputEditor::saveChanges()
{
    saveInputs_->click();
}
