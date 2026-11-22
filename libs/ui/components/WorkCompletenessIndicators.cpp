#include "ui/components/WorkCompletenessIndicators.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyle>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

namespace darkeye
{
namespace
{

constexpr int indicatorCount = 15;
constexpr int cellSize = 10;
constexpr int cellGap = 1;

const QColor unknownColor(QStringLiteral("#888888"));
const QColor availableColor(QStringLiteral("#27ae60"));
const QColor missingColor(QStringLiteral("#c0392b"));

} // namespace

const QStringList &workCompletenessKeys()
{
    static const QStringList keys = {
        QStringLiteral("cover"),       QStringLiteral("actress"),
        QStringLiteral("actor"),       QStringLiteral("director"),
        QStringLiteral("release_date"), QStringLiteral("runtime"),
        QStringLiteral("tag"),         QStringLiteral("cn_title"),
        QStringLiteral("jp_title"),    QStringLiteral("cn_story"),
        QStringLiteral("jp_story"),    QStringLiteral("maker"),
        QStringLiteral("label"),       QStringLiteral("series"),
        QStringLiteral("fanart"),
    };
    return keys;
}

const QStringList &workCompletenessLabels()
{
    static const QStringList labels = {
        QStringLiteral("封面"),     QStringLiteral("女优"),     QStringLiteral("男优"),
        QStringLiteral("导演"),     QStringLiteral("发售日"),   QStringLiteral("时长"),
        QStringLiteral("标签"),     QStringLiteral("中文标题"), QStringLiteral("日文标题"),
        QStringLiteral("中文简介"), QStringLiteral("日文简介"), QStringLiteral("片商"),
        QStringLiteral("厂牌"),     QStringLiteral("系列"),     QStringLiteral("剧照"),
    };
    return labels;
}

WorkCompletenessLedStrip::WorkCompletenessLedStrip(
    const std::optional<QMap<QString, bool>> &completeness, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("WorkCompletenessLedStrip"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(cellGap);

    for (const QString &key : workCompletenessKeys())
    {
        auto *cell = new QWidget(this);
        cell->setObjectName(QStringLiteral("WorkCompletenessCell_%1").arg(key));
        cell->setFixedSize(14, 14);
        cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cell->setCursor(Qt::PointingHandCursor);

        auto *outer = new QVBoxLayout(cell);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        outer->addStretch();
        auto *inner = new QHBoxLayout;
        inner->setContentsMargins(0, 0, 0, 0);
        inner->setSpacing(0);
        inner->addStretch();
        auto *indicator = new QFrame(cell);
        indicator->setObjectName(QStringLiteral("WorkCompletenessIndicator_%1").arg(key));
        indicator->setFixedSize(cellSize, cellSize);
        indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
        inner->addWidget(indicator);
        inner->addStretch();
        outer->addLayout(inner);
        outer->addStretch();
        layout->addWidget(cell);
        m_cells.append(cell);
        m_indicators.append(indicator);
    }
    setCompleteness(completeness);
}

void WorkCompletenessLedStrip::setCompleteness(
    const std::optional<QMap<QString, bool>> &completeness)
{
    const QStringList &keys = workCompletenessKeys();
    const QStringList &labels = workCompletenessLabels();
    for (int index = 0; index < keys.size(); ++index)
    {
        QString color = unknownColor.name();
        QString state = QStringLiteral("未检测");
        if (completeness.has_value())
        {
            const bool available = completeness->value(keys.at(index), false);
            color = (available ? availableColor : missingColor).name();
            state = available ? QStringLiteral("已有") : QStringLiteral("暂无");
        }
        m_indicators.at(index)->setStyleSheet(
            QStringLiteral("QFrame { background-color: %1; border-radius: 0; }").arg(color));
        m_cells.at(index)->setToolTip(
            QStringLiteral("%1\n（库内完整度）%2").arg(labels.at(index), state));
        m_cells.at(index)->setProperty("completenessState", state);
    }
}

WorkCompletenessBitsDelegate::WorkCompletenessBitsDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void WorkCompletenessBitsDelegate::paint(QPainter *painter,
                                         const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    const QString bits = normalizeBits(index.data(Qt::DisplayRole));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (option.state.testFlag(QStyle::State_Selected))
    {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    const int width = indicatorCount * cellSize + (indicatorCount - 1) * cellGap;
    const int startX = option.rect.x() + qMax(0, (option.rect.width() - width) / 2);
    const int startY = option.rect.y() + qMax(0, (option.rect.height() - cellSize) / 2);
    for (int bitIndex = 0; bitIndex < bits.size(); ++bitIndex)
    {
        QColor color = unknownColor;
        if (bits.at(bitIndex) == u'1')
        {
            color = availableColor;
        }
        else if (bits.at(bitIndex) == u'0')
        {
            color = missingColor;
        }
        painter->fillRect(
            QRect(startX + bitIndex * (cellSize + cellGap), startY, cellSize, cellSize), color);
    }
    painter->restore();
}

QSize WorkCompletenessBitsDelegate::sizeHint(const QStyleOptionViewItem &option,
                                             const QModelIndex &) const
{
    const int width = indicatorCount * cellSize + (indicatorCount - 1) * cellGap;
    return {width + 6, qMax(option.fontMetrics.height() + 2, cellSize + 4)};
}

bool WorkCompletenessBitsDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                             const QStyleOptionViewItem &option,
                                             const QModelIndex &index)
{
    if (event != nullptr && event->type() == QEvent::ToolTip)
    {
        QToolTip::showText(event->globalPos(),
                           tooltipForBits(normalizeBits(index.data(Qt::DisplayRole))), view);
        return true;
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

QString WorkCompletenessBitsDelegate::normalizeBits(const QVariant &value)
{
    const QString bits = value.toString().trimmed();
    if (bits.size() == indicatorCount
        && std::all_of(bits.cbegin(), bits.cend(), [](QChar bit) { return bit == u'0' || bit == u'1'; }))
    {
        return bits;
    }
    return QString(indicatorCount, u'?');
}

QString WorkCompletenessBitsDelegate::tooltipForBits(const QString &bits)
{
    const QString normalized = normalizeBits(bits);
    if (normalized.contains(u'?'))
    {
        return QStringLiteral("库内完整度\n未检测");
    }

    QStringList missing;
    int available = 0;
    for (int index = 0; index < normalized.size(); ++index)
    {
        if (normalized.at(index) == u'1')
        {
            ++available;
        }
        else
        {
            missing.append(workCompletenessLabels().at(index));
        }
    }
    if (missing.isEmpty())
    {
        return QStringLiteral("库内完整度 %1/%2\n信息完整").arg(available).arg(indicatorCount);
    }
    return QStringLiteral("库内完整度 %1/%2\n缺：%3")
        .arg(available)
        .arg(indicatorCount)
        .arg(missing.join(u'、'));
}

} // namespace darkeye
