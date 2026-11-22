#include "darkeye_ui/layouts/FlowLayout.h"

#include <QWidget>

namespace darkeye {

FlowLayout::FlowLayout(QWidget *parent, int margin, int spacing)
    : QLayout(parent)
{
    setContentsMargins(margin, margin, margin, margin);
    setSpacing(spacing);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_items.value(index, nullptr);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(),
                        margins.top() + margins.bottom());
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QMargins margins = contentsMargins();
    const QRect available = rect.adjusted(margins.left(), margins.top(),
                                          -margins.right(), -margins.bottom());
    int x = available.x();
    int y = available.y();
    int lineHeight = 0;

    for (QLayoutItem *item : m_items) {
        QWidget *widget = item->widget();
        if (widget != nullptr && widget->isHidden()) {
            continue;
        }

        const QSize hint = item->sizeHint();
        const int nextX = x + hint.width();
        if (nextX > available.right() + 1 && lineHeight > 0) {
            x = available.x();
            y += lineHeight + spacing();
            lineHeight = 0;
        }
        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), hint));
        }
        x += hint.width() + spacing();
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + margins.bottom();
}

} // namespace darkeye
