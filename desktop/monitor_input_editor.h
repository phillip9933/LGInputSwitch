#pragma once
#include "profiles.h"
#include <QWidget>

class QPushButton;

class MonitorInputEditor final : public QWidget
{
    std::function<bool()> changed_;
    QPushButton *saveInputs_;

public:
    bool hasChanges() const { return changed_(); }
    void saveChanges();
    MonitorInputEditor(QWidget *parent, SavedMonitor monitor, const DesktopWorkspace &workspace,
        std::function<MonitorResolution()> resolve, QThreadPool *worker, bool preview,
        std::function<bool(const SavedMonitor &, QString &)> save, std::function<void()> cancel);
};
