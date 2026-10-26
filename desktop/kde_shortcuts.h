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
    QString component_;

  public:
    explicit KdeShortcuts(QString component = "lginputswitch.desktop") : component_(std::move(component))
    {
    }
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
              const std::function<void()> &triggered,
              const std::function<void(const QKeySequence &)> &changed = {}, bool applyEdits = true,
              bool active = true)
    {
#ifdef LG_HAVE_KGLOBALACCEL

        // Never take an existing shortcut from another application or a user's
        // manually configured command.
        if (applyEdits && !key.isEmpty() && !KGlobalAccel::isGlobalShortcutAvailable(key, component_))
            return false;
        auto *action = new QAction(label);
        action->setObjectName(id);
        action->setProperty("isConfigurationAction", !active);
        action->setProperty("componentName", component_);
        action->setProperty("componentDisplayName", "LG Input Switch");
        QObject::connect(action, &QAction::triggered, action, triggered);
        if (!KGlobalAccel::self()->setShortcut(
                action, key.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{key},
                applyEdits ? KGlobalAccel::NoAutoloading : KGlobalAccel::Autoloading))
        {
            delete action;
            return false;
        }
        actions_.push_back(action);
        if (changed)
        {
            QObject::connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutChanged, action,
                             [action, changed](QAction *updated, const QKeySequence &sequence) {
                                 if (updated == action)
                                     changed(sequence);
                             });
            const auto actual = KGlobalAccel::self()->shortcut(action);
            changed(actual.isEmpty() ? QKeySequence{} : actual.first());
        }
        return true;
#else
        Q_UNUSED(id);
        Q_UNUSED(label);
        Q_UNUSED(key);
        Q_UNUSED(triggered);
        Q_UNUSED(changed);
        Q_UNUSED(applyEdits);
        Q_UNUSED(active);
        return false;
#endif
    }
    void clear(bool forget = false)
    {
#ifdef LG_HAVE_KGLOBALACCEL
        for (auto *action : actions_)
        {
            if (forget)
                KGlobalAccel::self()->removeAllShortcuts(action);
            delete action;
        }
#else
        Q_UNUSED(forget);
#endif
        actions_.clear();
    }
    static void pruneInactive()
    {
#ifdef LG_HAVE_KGLOBALACCEL
        KGlobalAccel::cleanComponent("lginputswitch.desktop");
#endif
    }
    ~KdeShortcuts()
    {
        clear();
    }
};
