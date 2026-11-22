#pragma once

#include <QFontMetrics>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QVector>

namespace darkeye {

struct VerticalTextBlock
{
    QString text;
    bool english = false;
    QRectF rect;
    qreal rotation = 0.0;
    qreal ascent = 0.0;
};

class VerticalTextLayout final
{
public:
    struct TextRun {
        QString text;
        bool english = false;
    };

    explicit VerticalTextLayout(const QFontMetrics &metrics,
                                qreal lineSpacing = 0.0,
                                qreal columnSpacing = 0.0);

    static QString replaceEllipsis(QString text);
    static QVector<TextRun> splitTextBlocks(const QString &text);

    QVector<VerticalTextBlock> calculateLayout(const QString &text,
                                               qreal width,
                                               qreal height) const;
    QSize calculateSize(const QString &text, int height = 0) const;

private:
    QFontMetrics m_metrics;
    qreal m_lineSpacing = 0.0;
    qreal m_columnSpacing = 0.0;
    qreal m_characterHeight = 0.0;
    qreal m_characterWidth = 0.0;
};

} // namespace darkeye
