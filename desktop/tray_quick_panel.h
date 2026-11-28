#pragma once

#include <QtWidgets>
#include <algorithm>
#ifdef LG_HAVE_PLASMA_TRAY_PLACEMENT
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/plasmashell.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/surface.h>
#endif

class TrayQuickPanel : public QDialog
{
#ifdef LG_HAVE_PLASMA_TRAY_PLACEMENT
    KWayland::Client::PlasmaShell *shell_ = nullptr;
#endif

public:
    explicit TrayQuickPanel(QWidget *owner) : QDialog(owner, Qt::Popup)
    {
#ifdef LG_HAVE_PLASMA_TRAY_PLACEMENT
        using namespace KWayland::Client;
        if (!QGuiApplication::platformName().startsWith("wayland"))
            return;
        auto *connection = ConnectionThread::fromApplication(this);
        if (!connection)
            return;
        auto *registry = new Registry(this);
        registry->create(connection);
        registry->setup();
        connection->roundtrip();
        const auto global = registry->interface(Registry::Interface::PlasmaShell);
        if (global.name && global.version >= 7)
        {
            shell_ = registry->createPlasmaShell(global.name, global.version, registry);
            // The compositor places a toplevel. An xdg popup would instead
            // need a parent surface owned by the panel hosting the tray icon.
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        }
#endif
    }

    static QPoint menuPosition(const QRect &area, const QRect &icon, const QSize &size)
    {
        const auto center = icon.center();
        const int left = qAbs(icon.left() - area.left());
        const int right = qAbs(area.right() - icon.right());
        const int top = qAbs(icon.top() - area.top());
        const int bottom = qAbs(area.bottom() - icon.bottom());
        const int edge = std::min({left, right, top, bottom});
        QPoint pos;
        if (edge == bottom || edge == top)
            pos = {center.x() - size.width() / 2,
                   edge == bottom ? icon.top() - size.height() : icon.bottom() + 1};
        else
            pos = {edge == left ? icon.right() + 1 : icon.left() - size.width(),
                   center.y() - size.height() / 2};
        return {qBound(area.left(), pos.x(), area.right() + 1 - size.width()),
                qBound(area.top(), pos.y(), area.bottom() + 1 - size.height())};
    }

    void openAtTray(const QPoint &fallbackPosition)
    {
#ifdef LG_HAVE_PLASMA_TRAY_PLACEMENT
        using namespace KWayland::Client;
        if (shell_)
        {
            // openUnderCursor applies before the first buffer of a surface.
            // Create a fresh native window for each click, retaining the widgets.
            destroy();
            winId();
            if (auto *surface = Surface::fromWindow(windowHandle()))
            {
                auto *popup = shell_->createSurface(surface, surface);
                popup->setRole(PlasmaShellSurface::Role::AppletPopup);
                popup->setSkipTaskbar(true);
                popup->setSkipSwitcher(true);
                popup->openUnderCursor();
                show();
                windowHandle()->requestActivate();
                return;
            }
        }
#endif
        move(fallbackPosition);
        show();
    }

protected:
    bool event(QEvent *event) override
    {
#ifdef LG_HAVE_PLASMA_TRAY_PLACEMENT
        if (shell_ && event->type() == QEvent::WindowDeactivate && isVisible())
            hide();
#endif
        return QDialog::event(event);
    }
};
