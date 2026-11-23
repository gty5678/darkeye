#include "darkeye_ui/layouts/WaterfallLayout.h"

#include <QVector>
#include <algorithm>

namespace darkeye {

WaterfallLayout::WaterfallLayout(QWidget *parent, int columnWidth, int margin,
                                 int spacing, int verticalSpacing)
    : QLayout(parent), m_columnWidth(qMax(1, columnWidth)), m_verticalSpacing(verticalSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
    setSpacing(spacing);
}

WaterfallLayout::~WaterfallLayout()
{
    while (QLayoutItem *item = takeAt(0)) {
        delete item;
    }
}

void WaterfallLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int WaterfallLayout::count() const
{
    return m_items.size();
}

QLayoutItem *WaterfallLayout::itemAt(int index) const
{
    return m_items.value(index, nullptr);
}

QLayoutItem *WaterfallLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations WaterfallLayout::expandingDirections() const
{
    return Qt::Horizontal;
}

bool WaterfallLayout::hasHeightForWidth() const
{
    return true;
}

int WaterfallLayout::heightForWidth(int width) const
{
    return layoutHeight(QRect(0, 0, width, 0), true);
}

void WaterfallLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    layoutHeight(rect, false);
}

QSize WaterfallLayout::sizeHint() const
{
    const QMargins margins = contentsMargins();
    const int width = m_columnWidth * 4 + spacing() * 3 + margins.left()
                      + margins.right();
    return QSize(width, heightForWidth(width));
}

QSize WaterfallLayout::minimumSize() const
{
    const QMargins margins = contentsMargins();
    return QSize(m_columnWidth + margins.left() + margins.right(),
                 heightForWidth(m_columnWidth + margins.left() + margins.right()));
}

int WaterfallLayout::columnWidth() const
{
    return m_columnWidth;
}

void WaterfallLayout::setColumnWidth(int width)
{
    if (width <= 0 || width == m_columnWidth) {
        return;
    }
    m_columnWidth = width;
    invalidate();
}

void WaterfallLayout::setUniformRows(bool enabled)
{
    if (m_uniformRows == enabled) return;
    m_uniformRows = enabled;
    invalidate();
}

int WaterfallLayout::layoutHeight(const QRect &rect, bool testOnly) const
{
    const QMargins margins = contentsMargins();
    const int verticalSpacing = m_verticalSpacing >= 0 ? m_verticalSpacing : spacing();
    const int availableWidth = qMax(1, rect.width() - margins.left() - margins.right());
    const int columns =
        qMax(1, (availableWidth + spacing()) / (m_columnWidth + spacing()));
    const int layoutWidth = columns * m_columnWidth + (columns - 1) * spacing();
    const int offsetX = rect.x() + margins.left() + (availableWidth - layoutWidth) / 2;
    if (m_uniformRows) {
        int y = rect.y() + margins.top();
        for (qsizetype rowStart = 0; rowStart < m_items.size(); rowStart += columns) {
            const qsizetype rowEnd = qMin(rowStart + columns, m_items.size());
            int rowHeight = 0;
            for (qsizetype index = rowStart; index < rowEnd; ++index) {
                auto *item = m_items.at(index);
                const int height = item->sizeHint().height();
                rowHeight = qMax(rowHeight, height);
                if (!testOnly) {
                    const int x = offsetX + int(index - rowStart) * (m_columnWidth + spacing());
                    item->setGeometry(QRect(x, y, m_columnWidth, height));
                }
            }
            y += rowHeight + verticalSpacing;
        }
        return y - rect.y() + margins.bottom() - (m_items.isEmpty() ? 0 : verticalSpacing);
    }
    QVector<int> heights(columns, rect.y() + margins.top());

    for (QLayoutItem *item : m_items) {
        int column = 0;
        for (int index = 1; index < heights.size(); ++index) {
            if (heights.at(index) < heights.at(column)) {
                column = index;
            }
        }
        const int itemHeight = item->sizeHint().height();
        if (!testOnly) {
            item->setGeometry(QRect(offsetX + column * (m_columnWidth + spacing()),
                                    heights.at(column), m_columnWidth, itemHeight));
        }
        heights[column] += itemHeight + verticalSpacing;
    }

    const int tallest = m_items.isEmpty()
                            ? rect.y() + margins.top()
                            : *std::max_element(heights.cbegin(), heights.cend()) - verticalSpacing;
    return tallest - rect.y() + margins.bottom();
}

} // namespace darkeye
