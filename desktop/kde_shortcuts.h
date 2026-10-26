#pragma once
#include <QAction>
#include <QKeySequence>
#include <QVector>
#include <functional>
#ifdef LG_HAVE_KGLOBALACCEL
#include <KGlobalAccel>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif

// KDE owns the global key grab on both Wayland and X11. No simulated input or
// private KWin APIs. Other desktops retain the CLI/desktop-settings fallback.
class KdeShortcuts
{
    QVector<QAction *> actions_;

  public:
    static bool available()
    {
#ifdef LG_HAVE_KGLOBALACCEL
        return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("KDE", Qt::CaseInsensitive) &&
               QDBusConnection::sessionBus().interface() &&
               QDBusConnection::sessionBus().interface()->isServiceRegistered("org.kde.kglobalaccel");
#else
        return false;
#endif
    }
    bool bind(const QString &id, const QString &label, const QKeySequence &key,
              const std::function<void()> &triggered)
    {
#ifdef LG_HAVE_KGLOBALACCEL
        if (key.isEmpty())
            return true;
        // Never take an existing shortcut from another application or a user's
        // manually configured command.
        if (!KGlobalAccel::isGlobalShortcutAvailable(key, "lginputswitch-desktop"))
            return false;
        auto *action = new QAction(label);
        action->setObjectName(id);
        action->setProperty("componentName", "lginputswitch-desktop");
        action->setProperty("componentDisplayName", "LG Input Switch");
        QObject::connect(action, &QAction::triggered, action, triggered);
        if (!KGlobalAccel::self()->setShortcut(action, {key}, KGlobalAccel::NoAutoloading) ||
            !KGlobalAccel::self()->shortcut(action).contains(key))
        {
            KGlobalAccel::self()->removeAllShortcuts(action);
            delete action;
            return false;
        }
        actions_.push_back(action);
        return true;
#else
        Q_UNUSED(id);
        Q_UNUSED(label);
        Q_UNUSED(key);
        Q_UNUSED(triggered);
        return false;
#endif
    }
    void clear()
    {
#ifdef LG_HAVE_KGLOBALACCEL
        for (auto *action : actions_)
        {
            KGlobalAccel::self()->removeAllShortcuts(action);
            delete action;
        }
#endif
        actions_.clear();
    }
    ~KdeShortcuts()
    {
        clear();
    }
};
