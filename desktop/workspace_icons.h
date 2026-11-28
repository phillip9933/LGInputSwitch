#pragma once
#include <QtWidgets>

// Keep the section pencils and delete buttons legible on platforms without
// a desktop icon theme. Qt asks this engine to paint at the target DPI.
class WorkspaceEditIcon final : public QIconEngine
{
    bool trash_;
public:
    explicit WorkspaceEditIcon(bool trash) : trash_(trash) {}
    QIconEngine *clone() const override { return new WorkspaceEditIcon(trash_); }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pix(size);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        paint(&painter, pix.rect(), mode, state);
        return pix;
    }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->translate(rect.topLeft());
        p->scale(rect.width() / 20., rect.height() / 20.);
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(qApp->palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                             QPalette::ButtonText), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        if (trash_) {
            p->drawLine(4, 5, 16, 5);
            p->drawLine(8, 3, 12, 3);
            p->drawRoundedRect(QRectF(6, 6, 8, 11), 1, 1);
            p->drawLine(9, 8, 9, 14);
            p->drawLine(11, 8, 11, 14);
        } else {
            p->drawPolygon(QPolygonF{{3, 17}, {4, 13}, {14, 3}, {17, 6}, {7, 16}});
            p->drawLine(12, 5, 15, 8);
        }
        p->restore();
    }
};

inline void configureIconButton(QAbstractButton *button, const QString &label, bool trash = false)
{
    button->setIcon(QIcon(new WorkspaceEditIcon(trash)));
    button->setAccessibleName(label);
    button->setToolTip(label);
    button->setProperty("iconOnly", true);
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(32, 30);
}
