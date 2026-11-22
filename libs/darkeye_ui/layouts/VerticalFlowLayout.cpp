#include "darkeye_ui/layouts/VerticalFlowLayout.h"

#include <QWidget>

namespace darkeye {

VerticalFlowLayout::VerticalFlowLayout(QWidget *parent, int margin, int spacing)
    : QLayout(parent)
{
    setContentsMargins(margin, margin, margin, margin);
    setSpacing(spacing);
}

VerticalFlowLayout::~VerticalFlowLayout()
{
    while (QLayoutItem *item = takeAt(0)) delete item;
}

void VerticalFlowLayout::addItem(QLayoutItem *item) { m_items.append(item); }
int VerticalFlowLayout::count() const { return m_items.size(); }
QLayoutItem *VerticalFlowLayout::itemAt(int index) const
{
    return m_items.value(index, nullptr);
}
QLayoutItem *VerticalFlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}
Qt::Orientations VerticalFlowLayout::expandingDirections() const
{
    return Qt::Horizontal;
}

void VerticalFlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize VerticalFlowLayout::sizeHint() const { return minimumSize(); }

QSize VerticalFlowLayout::minimumSize() const
{
    const QMargins margins = contentsMargins();
    const QWidget *parent = parentWidget();
    const int height = parent != nullptr && parent->height() > 0
        ? parent->height() : 100;
    int maximumItemHeight = 0;
    for (const QLayoutItem *item : m_items) {
        maximumItemHeight = qMax(maximumItemHeight, item->sizeHint().height());
    }
    return {widthForHeight(height), maximumItemHeight + margins.top()
                                      + margins.bottom()};
}

int VerticalFlowLayout::widthForHeight(int height) const
{
    return doLayout(QRect(0, 0, 0, qMax(1, height)), true);
}

int VerticalFlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QMargins margins = contentsMargins();
    int x = rect.right() - margins.right();
    int y = rect.y() + margins.top();
    int columnWidth = 0;
    int lastColumnWidth = 0;

    for (QLayoutItem *item : m_items) {
        const QSize hint = item->sizeHint();
        int nextY = y + hint.height() + spacing();
        if (nextY - spacing() > rect.bottom() - margins.bottom()
            && columnWidth > 0) {
            y = rect.y() + margins.top();
            x -= columnWidth + spacing();
            nextY = y + hint.height() + spacing();
            columnWidth = 0;
            lastColumnWidth = hint.width();
        }
        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x - hint.width(), y), hint));
        }
        y = nextY;
        lastColumnWidth = qMax(lastColumnWidth, hint.width());
        columnWidth = qMax(columnWidth, hint.width());
    }
    return rect.right() - x + margins.left() + lastColumnWidth;
}

} // namespace darkeye
