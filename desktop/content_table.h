#pragma once
#include <QHeaderView>
#include <QScrollBar>
#include <QTableWidget>

// The workspace page owns vertical scrolling. Keep every row in reach there,
// including when a narrow viewport needs a horizontal scrollbar.
class ContentTable final : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

    void fitRows()
    {
        const int header = horizontalHeader()->isHidden() ? 0 : horizontalHeader()->height();
        const int bar = horizontalScrollBar()->maximum() > 0 ? horizontalScrollBar()->sizeHint().height() : 0;
        const int height = verticalHeader()->length() + header + bar + 2 * frameWidth();
        if (height > 0 && (minimumHeight() != height || maximumHeight() != height))
            setFixedHeight(height);
    }

protected:
    void updateGeometries() override
    {
        QTableWidget::updateGeometries();
        fitRows();
    }
};
