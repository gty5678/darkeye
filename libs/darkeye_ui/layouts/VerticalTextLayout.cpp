#include "darkeye_ui/layouts/VerticalTextLayout.h"

#include <QtMath>

namespace darkeye {

VerticalTextLayout::VerticalTextLayout(const QFontMetrics &metrics,
                                       qreal lineSpacing,
                                       qreal columnSpacing)
    : m_metrics(metrics), m_lineSpacing(lineSpacing),
      m_columnSpacing(columnSpacing),
      m_characterHeight(metrics.height() + lineSpacing),
      m_characterWidth(metrics.maxWidth())
{
}

QString VerticalTextLayout::replaceEllipsis(QString text)
{
    const QList<QPair<QString, QString>> replacements{
        {QStringLiteral("……"), QStringLiteral("︙")},
        {QStringLiteral("…"), QStringLiteral("︙")},
        {QStringLiteral("⋯"), QStringLiteral("︙")},
        {QStringLiteral("，"), QStringLiteral("︐")},
        {QStringLiteral("、"), QStringLiteral("︑")},
        {QStringLiteral("。"), QStringLiteral("︒")},
        {QStringLiteral("："), QStringLiteral("︓")},
        {QStringLiteral("；"), QStringLiteral("︔")},
        {QStringLiteral("！"), QStringLiteral("︕")},
        {QStringLiteral("？"), QStringLiteral("︖")},
        {QStringLiteral("（"), QStringLiteral("︵")},
        {QStringLiteral("）"), QStringLiteral("︶")},
        {QStringLiteral("【"), QStringLiteral("︻")},
        {QStringLiteral("】"), QStringLiteral("︼")},
        {QStringLiteral("《"), QStringLiteral("︽")},
        {QStringLiteral("》"), QStringLiteral("︾")},
        {QStringLiteral("〈"), QStringLiteral("︿")},
        {QStringLiteral("〉"), QStringLiteral("﹀")},
        {QStringLiteral("「"), QStringLiteral("﹁")},
        {QStringLiteral("」"), QStringLiteral("﹂")},
        {QStringLiteral("『"), QStringLiteral("﹃")},
        {QStringLiteral("』"), QStringLiteral("﹄")},
    };
    for (const auto &[source, vertical] : replacements) {
        text.replace(source, vertical);
    }
    return text;
}

QVector<VerticalTextLayout::TextRun>
VerticalTextLayout::splitTextBlocks(const QString &text)
{
    QVector<TextRun> blocks;
    QString buffer;
    bool bufferIsEnglish = false;
    bool hasBuffer = false;
    for (const QChar character : text) {
        const bool english = character.unicode() < 128;
        if (hasBuffer && english != bufferIsEnglish) {
            blocks.append({buffer, bufferIsEnglish});
            buffer.clear();
        }
        buffer.append(character);
        bufferIsEnglish = english;
        hasBuffer = true;
    }
    if (hasBuffer) blocks.append({buffer, bufferIsEnglish});
    return blocks;
}

QVector<VerticalTextBlock> VerticalTextLayout::calculateLayout(
    const QString &source, qreal width, qreal height) const
{
    const QString text = replaceEllipsis(source);
    QVector<VerticalTextBlock> result;
    qreal x = width - m_characterWidth;
    qreal y = 0.0;
    bool exhausted = false;

    const auto nextColumn = [&] {
        y = 0.0;
        x -= m_characterWidth + m_columnSpacing;
        return x + m_characterWidth > 0.0;
    };

    for (const TextRun &run : splitTextBlocks(text)) {
        if (exhausted) break;
        if (run.english) {
            const qreal blockHeight = m_metrics.horizontalAdvance(run.text);
            if (height > 0.0 && y + blockHeight + m_lineSpacing > height
                && y > 0.0 && !nextColumn()) {
                break;
            }
            result.append({run.text, true,
                           QRectF(x, y, m_characterWidth, blockHeight),
                           90.0, qreal(m_metrics.ascent())});
            y += blockHeight + m_lineSpacing;
            continue;
        }
        for (const QChar character : run.text) {
            if (height > 0.0 && y + m_characterHeight > height && y > 0.0
                && !nextColumn()) {
                exhausted = true;
                break;
            }
            result.append({QString(character), false,
                           QRectF(x, y, m_characterWidth, m_characterHeight)});
            y += m_characterHeight;
        }
    }
    return result;
}

QSize VerticalTextLayout::calculateSize(const QString &source, int height) const
{
    const QString text = replaceEllipsis(source);
    if (text.isEmpty()) return {50, 50};
    qreal maximumHeight = 0.0;
    qreal currentHeight = 0.0;
    int columns = 1;
    const auto appendHeight = [&](qreal blockHeight) {
        if (height > 0 && currentHeight + blockHeight > height
            && currentHeight > 0.0) {
            ++columns;
            currentHeight = blockHeight;
        } else {
            currentHeight += blockHeight;
        }
        maximumHeight = qMax(maximumHeight, currentHeight);
    };
    for (const TextRun &run : splitTextBlocks(text)) {
        if (run.english) {
            appendHeight(m_metrics.horizontalAdvance(run.text) + m_lineSpacing);
        } else {
            for (qsizetype index = 0; index < run.text.size(); ++index) {
                appendHeight(m_characterHeight);
            }
        }
    }
    const qreal totalWidth = columns * m_characterWidth
        + qMax(0, columns - 1) * m_columnSpacing;
    return {qCeil(totalWidth), qCeil(height > 0
        ? qMin(qreal(height), maximumHeight) : maximumHeight)};
}

} // namespace darkeye
