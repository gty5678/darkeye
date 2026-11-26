#include "resource_painting.h"

#include "../resources/resource_manager.h"
#include "../theme_palette.h"

#include <QFontDatabase>
#include <QPainter>

namespace mdedit {
namespace {

void paintText(QPainter &painter, const QString &text, bool truncated, const QRectF &content,
               const std::optional<QPalette> &theme)
{
    painter.save();
    painter.setClipRect(content, Qt::IntersectClip);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(painter.font().pointSizeF());
    painter.setFont(font);
    painter.setPen(themeColor(theme, QPalette::Text, QColor("#26232d")));
    const QFontMetricsF metrics(painter.font());
    const auto lines = text.split('\n');
    const int available = qMax(0, int((content.height() - 24) / metrics.lineSpacing()));
    const int count = qMin(available, int(lines.size()));
    bool clipped = truncated || count < lines.size();
    for (int i = 0; i < count; ++i) {
        QString line = lines[i];
        line.replace('\t', "    ");
        clipped |= metrics.horizontalAdvance(line) > content.width();
        painter.drawText(QPointF(content.left(), content.top() + metrics.ascent() + i * metrics.lineSpacing()),
                         metrics.elidedText(line, Qt::ElideRight, content.width()));
    }
    painter.setPen(themeColor(theme, QPalette::Text, QColor("#746980")));
    if (text.isEmpty()) {
        painter.drawText(content, Qt::AlignCenter, "空文本文件");
    } else if (clipped) {
        painter.drawText(content.adjusted(0, 0, 0, -2), Qt::AlignBottom | Qt::AlignLeft,
                         "仅显示部分内容，请打开原文件查看全文");
    }
    painter.restore();
}

void paintTable(QPainter &painter, const QList<QStringList> &rows, const QRectF &content,
                const std::optional<QPalette> &theme)
{
    const int rowCount = qMin(8, int(rows.size()));
    int columnCount = 1;
    for (const auto &row : rows)
        columnCount = qMax(columnCount, qMin(8, int(row.size())));

    const qreal columnWidth = content.width() / columnCount;
    const QColor borderColor = themeColor(theme, QPalette::Mid, QColor("#ded7e6"));
    const QColor textColor = themeColor(theme, QPalette::Text, QColor("#26232d"));
    for (int row = 0; row < rowCount; ++row) {
        for (int column = 0; column < columnCount; ++column) {
            const QRectF cell(content.left() + column * content.width() / columnCount,
                              content.top() + row * 24, columnWidth, 24);
            painter.setPen(borderColor);
            painter.drawRect(cell);
            painter.setPen(textColor);
            painter.save();
            painter.setClipRect(cell, Qt::IntersectClip);
            painter.drawText(cell.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::TextSingleLine,
                             rows[row].value(column));
            painter.restore();
        }
    }
}

} // namespace

void paintResource(QPainter &painter, const ResourceSnapshot &data, const QRectF &content,
                   const std::optional<QPalette> &theme)
{
    if (!data.error.isEmpty()) {
        painter.drawText(content, Qt::TextWordWrap, data.error);
    } else if (!data.image.isNull()) {
        QSizeF size = data.image.size();
        size.scale(content.size(), Qt::KeepAspectRatio);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(content.center() - QPointF(size.width() / 2, size.height() / 2), size),
                          data.image);
    } else if (!data.rows.isEmpty()) {
        paintTable(painter, data.rows, content, theme);
    } else if (data.text.has_value()) {
        paintText(painter, *data.text, data.textTruncated, content, theme);
    } else {
        painter.drawText(content, Qt::AlignCenter, "资源没有可显示的内容");
    }
}

} // namespace mdedit
