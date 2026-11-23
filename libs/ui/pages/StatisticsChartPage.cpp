#include "ui/pages/StatisticsChartPage.h"

#include "ui/pages/PageScrollArea.h"

#include "database/repositories/StatisticsRepository.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QDate>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace darkeye
{
namespace
{

constexpr int chartHeaderBottom = 56;
constexpr int pieLabelRowHeight = 22;

class StatisticsChart final : public QWidget
{
public:
    enum class Type { Message, Bars, Histogram, Pie, Bubbles, Line };
    explicit StatisticsChart(ThemeService &themes, QWidget *parent = nullptr)
        : QWidget(parent), m_themes(themes)
    {
        setMinimumSize(340, 336);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(&m_themes, &ThemeService::themeChanged, this, [this] { update(); });
    }

    void setHeading(const QString &title, const QString &description)
    {
        m_title = title;
        m_description = description;
        setAccessibleName(title);
        setAccessibleDescription(description);
    }

    void message(const QString &text)
    {
        m_message = text;
        showChart(Type::Message);
    }

    void bars(QVector<ChartValue> values, bool horizontal = false)
    {
        m_values = std::move(values);
        m_horizontal = horizontal;
        showChart(Type::Bars, horizontal ? chartHeaderBottom + 34 + 26 * m_values.size() : 336);
    }

    void histogram(QVector<WeightedChartValue> values, QString unit,
                   int bins, qreal bandwidth, qreal minimum = 0, qreal maximum = 0)
    {
        m_histogramMinimum = minimum;
        m_histogramMaximum = maximum;
        values.erase(std::remove_if(values.begin(), values.end(),
                                    [=](const auto &value) {
                                        return !std::isfinite(value.value)
                                               || (maximum > minimum
                                                   && (value.value < minimum || value.value > maximum));
                                    }), values.end());
        m_weighted = std::move(values);
        m_unit = std::move(unit);
        m_bins = bins;
        m_bandwidth = bandwidth;
        showChart(Type::Histogram);
    }

    void pie(QVector<ChartValue> values)
    {
        m_values = std::move(values);
        showChart(Type::Pie, chartHeaderBottom + 42 + pieLabelRowHeight * m_values.size());
    }

    void bubbles(QVector<WaistHipChartValue> values)
    {
        m_bubbles = std::move(values);
        showChart(Type::Bubbles);
    }

    void line(const QVector<QDate> &dates)
    {
        m_dates.clear();
        for (const auto &date : dates)
            ++m_dates[date];
        m_dateCount = dates.size();
        showChart(Type::Line);
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip && m_type == Type::Bars && !m_values.isEmpty()) {
            const auto &help = *static_cast<QHelpEvent *>(event);
            const QRectF area = barPlot();
            const qreal step = m_horizontal ? qMin<qreal>(36, area.height() / m_values.size())
                                            : area.width() / m_values.size();
            const qreal offset = m_horizontal ? help.pos().y() - area.top()
                                              : help.pos().x() - area.left();
            if (offset >= 0 && offset < step * m_values.size()
                && (m_horizontal || area.contains(help.pos()))) {
                const auto &value = m_values.at(static_cast<qsizetype>(offset / step));
                QToolTip::showText(help.globalPos(), QStringLiteral("%1 · %2").arg(
                                       value.label, QLocale().toString(qRound64(value.value))), this);
            } else {
                QToolTip::hideText();
                event->ignore();
            }
            return true;
        }
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const ThemeTokens tokens = m_themes.currentTokens();
        p.setPen(QColor(tokens.border));
        p.setBrush(QColor(tokens.background));
        p.drawPath(chamferPath(QRectF(rect()).adjusted(.5, .5, -.5, -.5)));

        p.setPen(QColor(tokens.text));
        QFont heading = font();
        heading.setPixelSize(21);
        heading.setWeight(QFont::DemiBold);
        p.setFont(heading);
        constexpr int inset = 20;
        const QFontMetrics titleMetrics = p.fontMetrics();
        const int baseline = (chartHeaderBottom + titleMetrics.ascent() - titleMetrics.descent()) / 2;
        p.drawText(inset, baseline, m_title);

        const int descriptionX = inset + titleMetrics.horizontalAdvance(m_title) + 14;
        const int descriptionWidth = width() - inset - descriptionX;
        if (descriptionWidth > 0) {
            p.setFont(font());
            p.setPen(secondaryTextColor());
            p.drawText(descriptionX, baseline,
                       p.fontMetrics().elidedText(m_description, Qt::ElideRight, descriptionWidth));
        }
        p.setPen(QColor(tokens.border));
        p.drawLine(inset, chartHeaderBottom, width() - inset, chartHeaderBottom);

        if (m_type == Type::Message || isEmpty()) {
            p.setPen(secondaryTextColor());
            p.drawText(rect().adjusted(20, chartHeaderBottom + 2, -20, -20), Qt::AlignCenter,
                       m_type == Type::Message ? m_message : QStringLiteral("当前范围暂无数据"));
            return;
        }
        switch (m_type) {
        case Type::Bars: paintBars(p); break;
        case Type::Histogram: paintHistogram(p); break;
        case Type::Pie: paintPie(p); break;
        case Type::Bubbles: paintBubbles(p); break;
        case Type::Line: paintLine(p); break;
        case Type::Message: break;
        }
    }

private:
    void showChart(Type type, qsizetype minimumHeight = 336)
    {
        m_type = type;
        setMinimumHeight(qMax<qsizetype>(336, minimumHeight));
        update();
    }

    bool isEmpty() const
    {
        switch (m_type) {
        case Type::Bars:
        case Type::Pie: return m_values.isEmpty();
        case Type::Histogram: return m_weighted.isEmpty();
        case Type::Bubbles: return m_bubbles.isEmpty();
        case Type::Line: return m_dates.isEmpty();
        case Type::Message: return true;
        }
        return true;
    }

    QColor tint(const QString &foreground, qreal strength) const
    {
        const QColor source(foreground);
        const QColor background(m_themes.currentTokens().background);
        return QColor::fromRgbF(
            background.redF() + (source.redF() - background.redF()) * strength,
            background.greenF() + (source.greenF() - background.greenF()) * strength,
            background.blueF() + (source.blueF() - background.blueF()) * strength);
    }

    QColor primaryTint(qreal strength) const
    {
        return tint(m_themes.currentTokens().primary, strength);
    }

    QColor secondaryTextColor() const
    {
        return tint(m_themes.currentTokens().text, .72);
    }

    ThemeService &m_themes;

    void paintBubbles(QPainter &p)
    {
        const QRect canvas = rect();
        const QRect plot = canvas.adjusted(66, chartHeaderBottom + 44, -86, -66);
        const QRect colorBar(canvas.right() - 64, plot.top(), 12, plot.height());
        constexpr qreal colorMin = 0.55F;
        constexpr qreal colorMax = 0.85F;
        const QList<QColor> colors{QColor("#313695"), QColor("#4575B4"),
                                   QColor("#74ADD1"), QColor("#ABD9E9"),
                                   QColor("#E0F3F8"), QColor("#FFFFBF"),
                                   QColor("#FEE090"), QColor("#FDAE61"),
                                   QColor("#F46D43"), QColor("#D73027"),
                                   QColor("#A50026")};
        const auto colorFor = [&colors](qreal value)
        {
            const qreal ratio = qBound(0.0F, (value - colorMin) / (colorMax - colorMin), 1.0F);
            const qreal location = ratio * 10;
            const int index = qBound(0, static_cast<int>(std::floor(location)), 9);
            const qreal local = location - index;
            const QColor left = colors.at(index);
            const QColor right = colors.at(index + 1);
            return QColor(qRound(left.red() + (right.red() - left.red()) * local),
                          qRound(left.green() + (right.green() - left.green()) * local),
                          qRound(left.blue() + (right.blue() - left.blue()) * local));
        };
        qreal minWaist = m_bubbles.first().waist;
        qreal maxWaist = minWaist;
        qreal minHip = m_bubbles.first().hip;
        qreal maxHip = minHip;
        qreal maxWeight = 1;
        for (const auto &point : m_bubbles) {
            minWaist = qMin(minWaist, point.waist); maxWaist = qMax(maxWaist, point.waist);
            minHip = qMin(minHip, point.hip); maxHip = qMax(maxHip, point.hip);
            maxWeight = qMax(maxWeight, point.weight);
        }
        constexpr qreal gridSpacing = 5.0F;
        const qreal minWaistGrid = std::floor(minWaist / gridSpacing) * gridSpacing;
        const qreal maxWaistGrid = qMax(
            minWaistGrid + gridSpacing,
            std::ceil(maxWaist / gridSpacing) * gridSpacing);
        const qreal minHipGrid = std::floor(minHip / gridSpacing) * gridSpacing;
        const qreal maxHipGrid = qMax(
            minHipGrid + gridSpacing,
            std::ceil(maxHip / gridSpacing) * gridSpacing);
        const qreal waistSpan = maxWaistGrid - minWaistGrid;
        const qreal hipSpan = maxHipGrid - minHipGrid;
        axes(p, plot, QStringLiteral("腰围 (cm)"), QStringLiteral("臀围 (cm)"));
        p.setPen(QPen(QColor(m_themes.currentTokens().border), 1));
        for (qreal waist = minWaistGrid; waist <= maxWaistGrid; waist += gridSpacing) {
            const qreal x = plot.left() + (waist - minWaistGrid) / waistSpan * plot.width();
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (qreal hip = minHipGrid; hip <= maxHipGrid; hip += gridSpacing) {
            const qreal y = plot.bottom() - (hip - minHipGrid) / hipSpan * plot.height();
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        p.setFont(QFont(p.font().family(), 8)); p.setPen(QColor(m_themes.currentTokens().text));
        for (qreal waist = minWaistGrid; waist <= maxWaistGrid; waist += gridSpacing) {
            const qreal x = plot.left() + (waist - minWaistGrid) / waistSpan * plot.width();
            p.drawText(QRectF(x - 28, plot.bottom() + 6, 56, 18), Qt::AlignCenter,
                       QString::number(waist, 'f', 0));
        }
        for (qreal hip = minHipGrid; hip <= maxHipGrid; hip += gridSpacing) {
            const qreal y = plot.bottom() - (hip - minHipGrid) / hipSpan * plot.height();
            p.drawText(QRectF(8, y - 9, plot.left() - 14, 18),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(hip, 'f', 0));
        }
        p.setPen(QColor(m_themes.currentTokens().background));
        for (const auto &point : m_bubbles) {
            const QPointF center(plot.left() + (point.waist - minWaistGrid) / waistSpan * plot.width(),
                                 plot.bottom() - (point.hip - minHipGrid) / hipSpan * plot.height());
            const qreal radius = 4 + 16 * point.weight / maxWeight;
            p.setBrush(colorFor(point.ratio));
            p.drawEllipse(center, radius, radius);
        }
        for (int y = 0; y < colorBar.height(); ++y) {
            const qreal value = colorMax - (colorMax - colorMin) * y / qMax(1, colorBar.height() - 1);
            p.fillRect(colorBar.left(), colorBar.top() + y, colorBar.width(), 1, colorFor(value));
        }
        p.setPen(QColor(m_themes.currentTokens().text)); p.setBrush(Qt::NoBrush); p.drawRect(colorBar);
        p.drawText(QRectF(colorBar.left() - 18, colorBar.top() - 24, 60, 18), Qt::AlignCenter, QStringLiteral("腰臀比"));
        for (int i = 0; i <= 5; ++i) {
            const qreal y = colorBar.top() + colorBar.height() * i / 5.0F;
            const qreal value = colorMax - (colorMax - colorMin) * i / 5.0F;
            p.drawLine(QPointF(colorBar.left() - 4, y), QPointF(colorBar.left(), y));
            p.drawText(QRectF(colorBar.right() + 5, y - 9, 48, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(value, 'f', 2));
        }
    }

    QRect plot() const { return rect().adjusted(88, chartHeaderBottom + 29, -48, -68); }

    void axes(QPainter &p, const QRect &r, const QString &xLabel,
              const QString &yLabel)
    {
        p.setPen(QPen(QColor(m_themes.currentTokens().border), 1));
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.bottomLeft(), r.topLeft());

        p.setPen(secondaryTextColor());
        p.setFont(QFont(p.font().family(), 9));
        p.drawText(QRect(r.left(), r.bottom() + 38, r.width(), 24),
                   Qt::AlignCenter, xLabel);
        p.save();
        p.translate(2, r.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-r.height() / 2, 0, r.height(), 24), Qt::AlignCenter,
                   yLabel);
        p.restore();
    }

    void drawYTicks(QPainter &p, const QRect &r, qreal maximum, bool counts = false)
    {
        const auto tokens = m_themes.currentTokens();
        const qreal scale = maximum > 0 ? maximum : 1;
        const int intervals = counts && scale < 4 ? static_cast<int>(scale) : 4;
        p.setFont(QFont(p.font().family(), 9));
        const QColor tickColor = secondaryTextColor();
        for (int tick = 0; tick <= intervals; ++tick) {
            const qreal value = scale * tick / intervals;
            const qreal y = r.bottom() - r.height() * tick / qreal(intervals);
            p.setPen(QColor(tokens.border));
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
            p.setPen(tickColor);
            p.drawText(QRectF(25, y - 9, r.left() - 33, 18),
                       Qt::AlignRight | Qt::AlignVCenter,
                       counts ? QLocale().toString(qRound64(value))
                              : QString::number(value, 'g', 3));
        }
    }

    qreal countScale(qreal maximum) const
    {
        return maximum >= 4 ? std::ceil(maximum / 4) * 4 : qMax<qreal>(1, maximum);
    }

    QRectF barPlot() const
    {
        if (!m_horizontal)
            return plot().adjusted(0, 12, 0, 0);
        const qreal labelWidth = qBound<qreal>(96, width() * .28, 228);
        const int top = chartHeaderBottom + 18;
        return QRectF(34 + labelWidth, top, width() - labelWidth - 108, height() - top - 16);
    }

    void paintBars(QPainter &p)
    {
        const auto tokens = m_themes.currentTokens();
        const auto maximum = std::max_element(m_values.cbegin(), m_values.cend(),
                                              [](const auto &a, const auto &b) {
                                                  return a.value < b.value;
                                              })->value;
        const QRectF r = barPlot();
        const auto count = m_values.size();
        const QColor labelColor = secondaryTextColor();
        const QColor highlight = primaryTint(.95);
        const QColor bar = primaryTint(.66);
        if (m_horizontal) {
            const qreal rowHeight = qMin<qreal>(36, r.height() / count);
            p.setFont(QFont(p.font().family(), 10));
            const QColor track = primaryTint(.13);
            for (qsizetype i = 0; i < count; ++i) {
                const auto &value = m_values.at(i);
                const qreal y = r.top() + i * rowHeight;
                const qreal barWidth = r.width() * value.value / qMax<qreal>(1, maximum);
                const qreal barY = y + (rowHeight - 8) / 2;
                p.setPen(Qt::NoPen);
                p.setBrush(track);
                p.drawRoundedRect(QRectF(r.left(), barY, r.width(), 8), 4, 4);
                p.setBrush(value.value == maximum ? highlight : bar);
                p.drawRoundedRect(QRectF(r.left(), barY, barWidth, 8), 4, 4);

                p.setPen(labelColor);
                p.drawText(QRectF(20, y, 27, rowHeight), Qt::AlignVCenter, QString::number(i + 1));
                p.setPen(QColor(tokens.text));
                const QRectF label(48, y, r.left() - 60, rowHeight);
                p.drawText(label, Qt::AlignVCenter,
                           p.fontMetrics().elidedText(value.label, Qt::ElideRight, int(label.width())));
                p.drawText(QRectF(r.right() + 8, y, 48, rowHeight),
                           Qt::AlignRight | Qt::AlignVCenter, QLocale().toString(qRound64(value.value)));
            }
            return;
        }

        const qreal scale = countScale(maximum);
        drawYTicks(p, r.toRect(), scale, true);
        axes(p, r.toRect(), QStringLiteral("年份"), QStringLiteral("数量"));
        const qreal step = r.width() / count;
        const qreal barWidth = qMin<qreal>(38, step * .72);
        p.setFont(QFont(p.font().family(), 9));
        const int labelWidth = p.fontMetrics().horizontalAdvance(QStringLiteral("0000")) + 12;
        const int labelStep = qMax(1, qCeil(labelWidth / step));

        const QLocale locale;
        const QFont yearFont = p.font();
        QFont countFont = yearFont;
        countFont.setPixelSize(11);
        const bool rotateCount = QFontMetrics(countFont).horizontalAdvance(
                                     locale.toString(qRound64(maximum))) + 4 > step;
        if (rotateCount)
            countFont.setPixelSize(qBound(8, qFloor(step), 11));
        const QFontMetrics countMetrics(countFont);
        const int countLabelStep = rotateCount
            ? qMax(1, qCeil((countMetrics.tightBoundingRect(QStringLiteral("0123456789")).height() + 1) / step))
            : 1;

        for (qsizetype i = 0; i < count; ++i) {
            const auto &value = m_values.at(i);
            const qreal center = r.left() + (i + .5) * step;
            const qreal barHeight = r.height() * value.value / scale;
            const qreal barTop = r.bottom() - barHeight;
            p.setPen(Qt::NoPen);
            p.setBrush(value.value == maximum ? highlight : bar);
            p.drawRoundedRect(QRectF(center - barWidth / 2, barTop, barWidth, barHeight), 2, 2);
            if (value.value > 0 && i % countLabelStep == 0) {
                p.save();
                p.setFont(countFont);
                p.setPen(QColor(tokens.text));
                const QString text = locale.toString(qRound64(value.value));
                const qreal textWidth = countMetrics.horizontalAdvance(text) + 2;
                const qreal textHeight = countMetrics.height();
                p.translate(center, barTop - 3);
                if (rotateCount) {
                    p.rotate(-90);
                    p.drawText(QRectF(0, -textHeight / 2, textWidth, textHeight),
                               Qt::AlignCenter, text);
                } else {
                    p.drawText(QRectF(-textWidth / 2, -textHeight, textWidth, textHeight),
                               Qt::AlignCenter, text);
                }
                p.restore();
            }
            if (i % labelStep == 0) {
                p.setFont(yearFont);
                p.setPen(labelColor);
                p.drawText(QRectF(center - labelWidth / 2.0, r.bottom() + 7, labelWidth, 18),
                           Qt::AlignCenter, value.label);
            }
        }
    }

    void paintHistogram(QPainter &p)
    {
        const bool integerBins = m_bins == 0;
        const bool fixedRange = m_histogramMaximum > m_histogramMinimum;
        qreal lo = m_weighted.first().value;
        qreal hi = lo;
        qreal totalWeight = 0;
        for (const auto &value : m_weighted) {
            const qreal sample = integerBins ? qRound(value.value) : value.value;
            lo = std::min(lo, sample);
            hi = std::max(hi, sample);
            totalWeight += value.weight;
        }
        int bins = m_bins;
        if (fixedRange) {
            lo = m_histogramMinimum;
            hi = m_histogramMaximum;
        } else if (integerBins) {
            // One bin per integer centimetre, centred on that height.
            lo = qRound(lo) - 0.5;
            hi = qRound(hi) + 0.5;
            bins = qMax(1, qRound(hi - lo));
        } else if (qFuzzyCompare(lo, hi)) {
            lo -= 0.5;
            hi += 0.5;
        }
        const qreal binWidth = (hi - lo) / bins;
        QVector<qreal> density(bins);
        for (const auto &value : m_weighted) {
            const qreal sample = integerBins ? qRound(value.value) : value.value;
            const int index = qBound(0, int((sample - lo) / binWidth), bins - 1);
            density[index] += value.weight;
        }
        if (totalWeight > 0) {
            for (auto &value : density)
                value /= totalWeight * binWidth;
        }

        QVector<qreal> kde(160);
        for (int i = 0; i < kde.size(); ++i) {
            const qreal x = lo + (hi - lo) * i / (kde.size() - 1);
            for (const auto &value : m_weighted) {
                const qreal sample = integerBins ? qRound(value.value) : value.value;
                const qreal z = (x - sample) / m_bandwidth;
                kde[i] += value.weight * std::exp(-0.5 * z * z);
            }
            kde[i] /= qMax<qreal>(1, totalWeight * m_bandwidth
                                       * std::sqrt(2.0 * std::numbers::pi_v<qreal>));
        }
        const qreal maximum = qMax(*std::max_element(density.cbegin(), density.cend()),
                                   *std::max_element(kde.cbegin(), kde.cend()));
        const qreal scale = maximum == 0 ? 1 : maximum;
        const QRect r = plot();
        drawYTicks(p, r, maximum);
        axes(p, r, m_unit, QStringLiteral("概率密度"));
        const qreal w = static_cast<qreal>(r.width()) / bins;
        const qreal gap = qMin<qreal>(2, w * 0.2);
        const QColor barColor = primaryTint(0.42);
        for (int i = 0; i < bins; ++i) {
            const qreal h = r.height() * density[i] / scale;
            p.fillRect(QRectF(r.left() + i * w + gap / 2, r.bottom() - h,
                             w - gap, h), barColor);
        }
        QPainterPath path;
        for (int i = 0; i < kde.size(); ++i) {
            const qreal x = r.left() + static_cast<qreal>(r.width()) * i / (kde.size() - 1);
            const qreal y = r.bottom() - r.height() * kde[i] / scale;
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        p.strokePath(path, QPen(primaryTint(0.95), 2));

        // Match Python weighted_percentile(): interpolate between adjacent
        // samples at the cumulative-weight threshold, using the original
        // values even when the height histogram uses integer-centred bins.
        auto sortedValues = m_weighted;
        std::stable_sort(sortedValues.begin(), sortedValues.end(),
                         [](const auto &a, const auto &b) { return a.value < b.value; });
        const auto percentile = [&](qreal fraction) {
            const qreal target = fraction * totalWeight;
            qreal cumulativeWeight = 0;
            for (qsizetype i = 0; i < sortedValues.size(); ++i) {
                const qreal previousWeight = cumulativeWeight;
                cumulativeWeight += sortedValues[i].weight;
                if (cumulativeWeight >= target) {
                    if (i == 0 || cumulativeWeight == previousWeight)
                        return sortedValues[i].value;
                    const qreal ratio = (target - previousWeight)
                                        / (cumulativeWeight - previousWeight);
                    return sortedValues[i - 1].value
                           + ratio * (sortedValues[i].value - sortedValues[i - 1].value);
                }
            }
            return sortedValues.last().value;
        };
        const qreal low = percentile(0.05);
        const qreal high = percentile(0.95);
        p.save();
        p.setFont(QFont(p.font().family(), 9));
        const auto drawPercentile = [&](qreal value, const QString &label) {
            const qreal x = r.left() + (value - lo) / (hi - lo) * r.width();
            p.setPen(QPen(QColor(m_themes.currentTokens().error), 1, Qt::DashLine));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.setPen(QColor(m_themes.currentTokens().error));
            p.drawText(QRectF(x - 48, r.top() + 4, 96, 18), Qt::AlignCenter, label);
        };
        if (qFuzzyCompare(low, high))
            drawPercentile(low, QStringLiteral("5% / 95%"));
        else {
            drawPercentile(low, QStringLiteral("5%"));
            drawPercentile(high, QStringLiteral("95%"));
        }
        p.restore();
        p.setPen(QColor(m_themes.currentTokens().text));
        p.setFont(QFont(p.font().family(), 8));
        const auto drawTick = [&](qreal x, const QString &label) {
            p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 5));
            p.drawText(QRectF(x - 28, r.bottom() + 8, 56, 18), Qt::AlignCenter, label);
        };
        if (integerBins || fixedRange) {
            const qreal pixelsPerUnit = r.width() / (hi - lo);
            const int labelWidth = p.fontMetrics().horizontalAdvance(QString::number(qFloor(hi))) + 10;
            const int tickStep = qMax(1, qCeil(labelWidth / pixelsPerUnit));
            for (int tick = qCeil(lo); tick <= qFloor(hi); tick += tickStep)
                drawTick(r.left() + (tick - lo) * pixelsPerUnit, QString::number(tick));
        } else {
            for (int tick = 0; tick <= 4; ++tick)
                drawTick(r.left() + r.width() * tick / 4.0,
                         QString::number(lo + (hi - lo) * tick / 4.0, 'g', 3));
        }
    }
    void paintPie(QPainter &p)
    {
        qreal total = 0;
        for (const auto &value : m_values)
            total += value.value;

        const auto tokens = m_themes.currentTokens();
        if (total <= 0)
            return;

        const QList<QColor> colors{
            QColor(tokens.primary), QColor("#5d91cb"), QColor("#56a58c"),
            QColor("#d89b51"), QColor("#ca7672"), QColor("#a08ac7"),
            QColor("#70a9b9"), QColor("#bc9564")};
        constexpr qreal leaderWidth = 22;
        constexpr qreal rowHeight = pieLabelRowHeight;
        const qreal labelWidth = qBound<qreal>(76, width() * .22, 144);
        const int top = chartHeaderBottom + 16;
        const QRectF pieArea(0, top, width(), height() - top - 18);
        const qreal diameter = std::min({width() - 2 * (labelWidth + leaderWidth + 14),
                                         pieArea.height(), qreal(320)});
        const qreal radius = diameter / 2;
        const QPointF center = pieArea.center();
        const QRectF circle(center.x() - radius, center.y() - radius, diameter, diameter);
        struct SliceLabel {
            QPointF direction;
            qsizetype index;
        };
        QList<SliceLabel> leftLabels;
        QList<SliceLabel> rightLabels;
        int start = 90 * 16;
        qreal cumulative = 0;
        for (qsizetype i = 0; i < m_values.size(); ++i) {
            const qreal value = m_values.at(i).value;
            const qreal angle = (90 - (cumulative + value / 2) * 360 / total)
                                * std::numbers::pi_v<qreal> / 180;
            const QPointF direction(std::cos(angle), -std::sin(angle));
            auto &labels = direction.x() < 0 ? leftLabels : rightLabels;
            labels.append({direction, i});

            cumulative += value;
            const int end = 90 * 16 - qRound(cumulative * 5760 / total);
            p.setBrush(colors.at(i % colors.size()));
            p.setPen(QPen(QColor(tokens.background), 1.5));
            p.drawPie(circle, start, end - start);
            start = end;
        }

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(tokens.background));
        const qreal innerRadius = diameter * .33;
        p.drawEllipse(circle.center(), innerRadius, innerRadius);
        QFont numberFont = font();
        numberFont.setPixelSize(diameter < 160 ? 17 : 24);
        numberFont.setBold(true);
        p.setFont(numberFont);
        p.setPen(QColor(tokens.text));
        p.drawText(QRectF(circle.center().x() - innerRadius, circle.center().y() - 23,
                          innerRadius * 2, 28),
                   Qt::AlignCenter, QLocale().toString(qRound64(total)));
        p.setFont(QFont(font().family(), 9));
        p.setPen(secondaryTextColor());
        p.drawText(QRectF(circle.center().x() - innerRadius, circle.center().y() + 7,
                          innerRadius * 2, 20), Qt::AlignCenter, QStringLiteral("合计"));

        QFont labelFont = font();
        labelFont.setPixelSize(16);
        p.setFont(labelFont);
        const auto drawLabels = [&](QList<SliceLabel> &labels, int side) {
            std::sort(labels.begin(), labels.end(), [](const auto &a, const auto &b) {
                return a.direction.y() < b.direction.y();
            });
            const qreal textEdge = center.x() + side * (radius + leaderWidth);
            const qreal topY = pieArea.top() + rowHeight / 2;
            const qreal bottomY = pieArea.bottom() - rowHeight / 2;
            qreal nextY = topY;
            for (qsizetype i = 0; i < labels.size(); ++i) {
                const auto &label = labels.at(i);
                const auto &value = m_values.at(label.index);
                const qreal latestY = bottomY - (labels.size() - 1 - i) * rowHeight;
                const QPointF anchor = center + label.direction * radius;
                const qreal y = qBound(nextY, anchor.y(), latestY);
                const QPolygonF leader{anchor, center + label.direction * (radius + 5),
                                       QPointF(textEdge - side * 10, y), QPointF(textEdge - side * 4, y)};
                p.setPen(QPen(colors.at(label.index % colors.size()), 1));
                p.drawPolyline(leader);

                const QString ratio = QStringLiteral("%1%").arg(value.value * 100 / total, 0, 'f', 1);
                const QString separator = QStringLiteral("  ");
                const int typeWidth = int(labelWidth) - p.fontMetrics().horizontalAdvance(separator + ratio);
                const QString type = p.fontMetrics().elidedText(value.label, Qt::ElideRight, typeWidth);
                const QRectF textArea(side > 0 ? textEdge : textEdge - labelWidth,
                                      y - rowHeight / 2, labelWidth, rowHeight);
                p.setPen(QColor(tokens.text));
                p.drawText(textArea, Qt::AlignVCenter | (side > 0 ? Qt::AlignLeft : Qt::AlignRight),
                           type + separator + ratio);
                nextY = y + rowHeight;
            }
        };
        drawLabels(leftLabels, -1);
        drawLabels(rightLabels, 1);
    }

    void paintLine(QPainter &p)
    {
        const auto tokens = m_themes.currentTokens();
        const QRect r = plot();
        const QDate first = m_dates.firstKey();
        const qint64 days = first.daysTo(m_dates.lastKey());
        const auto xAt = [&](qint64 offset) {
            return days > 0 ? r.left() + r.width() * qreal(offset) / days : r.center().x();
        };
        const qreal scale = countScale(m_dateCount);
        drawYTicks(p, r, scale, true);
        axes(p, r, QStringLiteral("入库日期"), QStringLiteral("累计作品数"));

        QPainterPath trend;
        trend.moveTo(r.left(), r.bottom());
        qsizetype runningTotal = 0;
        for (auto it = m_dates.cbegin(); it != m_dates.cend(); ++it) {
            const qreal x = xAt(first.daysTo(it.key()));
            trend.lineTo(x, trend.currentPosition().y());
            runningTotal += it.value();
            trend.lineTo(x, r.bottom() - r.height() * runningTotal / scale);
        }
        QPainterPath fill = trend;
        fill.lineTo(trend.currentPosition().x(), r.bottom());
        fill.closeSubpath();
        p.fillPath(fill, primaryTint(.16));
        const QPen trendPen(QColor(tokens.primary), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.strokePath(trend, trendPen);
        p.setPen(trendPen);
        p.setBrush(QColor(tokens.primary));
        p.drawEllipse(trend.currentPosition(), 3, 3);

        p.setFont(QFont(p.font().family(), 9));
        p.setPen(secondaryTextColor());
        const int labelWidth = p.fontMetrics().horizontalAdvance(QStringLiteral("0000-00-00")) + 18;
        const int intervals = qBound(1, r.width() / labelWidth, 7);
        const auto drawDate = [&](qint64 offset) {
            const qreal x = xAt(offset);
            p.drawText(QRectF(x - labelWidth / 2.0, r.bottom() + 7, labelWidth, 18),
                       Qt::AlignCenter, first.addDays(offset).toString(Qt::ISODate));
        };
        if (days == 0) {
            drawDate(0);
        } else {
            const int steps = qMin<qint64>(days, intervals);
            for (int i = 0; i <= steps; ++i)
                drawDate(qRound64(days * qreal(i) / steps));
        }
    }

    Type m_type = Type::Message;
    QString m_title;
    QString m_description;
    QString m_message;
    QString m_unit;
    QVector<ChartValue> m_values;
    QVector<WeightedChartValue> m_weighted;
    QVector<WaistHipChartValue> m_bubbles;
    QMap<QDate, int> m_dates;
    qsizetype m_dateCount = 0;
    int m_bins = 30;
    qreal m_bandwidth = 1;
    qreal m_histogramMinimum = 0;
    qreal m_histogramMaximum = 0;
    bool m_horizontal = false;
};
}

StatisticsChartPage::StatisticsChartPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                                         ThemeService &themeService, QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_themeService(themeService)
{
}

void StatisticsChartPage::lazyLoad()
{
    enum class Metric {
        ReleaseYears, WorkAge, AddedDates, DebutYears, DebutAge, Height, WaistHip,
        Cup, Directors, Makers, RecordedActresses
    };
    struct ChartOption {
        Metric metric;
        QString title;
        QString description;
        bool scoped = true;
    };
    const QList<QList<ChartOption>> categories{
        {
            {Metric::ReleaseYears, QStringLiteral("作品发行趋势"), QStringLiteral("按发行年份统计作品")},
            {Metric::WorkAge, QStringLiteral("拍摄年龄分布"), QStringLiteral("按作品统计出演女优的平均拍摄年龄，每柱 4 个月")},
            {Metric::AddedDates, QStringLiteral("作品入库趋势"), QStringLiteral("公共数据库 · 按入库日期累计作品数量"), false},
        },
        {
            {Metric::DebutYears, QStringLiteral("出道年份"), QStringLiteral("按出道年份统计女优")},
            {Metric::DebutAge, QStringLiteral("出道年龄分布"), QStringLiteral("公共数据库 · 按出道日期减去半年估算年龄"), false},
            {Metric::Height, QStringLiteral("身高分布"), QStringLiteral("按身高统计女优")},
            {Metric::WaistHip, QStringLiteral("腰臀比分布"), QStringLiteral("颜色表示腰臀比，气泡大小表示数量")},
            {Metric::Cup, QStringLiteral("罩杯分布"), QStringLiteral("按资料中记录的罩杯统计占比")},
        },
        {
            {Metric::Directors, QStringLiteral("导演排名"), QStringLiteral("按作品统计导演")},
            {Metric::Makers, QStringLiteral("片商排名"), QStringLiteral("按作品统计片商")},
            {Metric::RecordedActresses, QStringLiteral("女优记录排名"), QStringLiteral("按关联作品的个人记录次数取前 20 名"), false},
        },
    };

    auto *content = addPageScrollArea(this);
    content->setObjectName(QStringLiteral("StatisticsContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *fields = new QHBoxLayout;
    fields->setSpacing(12);
    auto addField = [content, fields](const QString &title) {
        auto *field = new QWidget(content);
        QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        policy.setRetainSizeWhenHidden(true);
        field->setSizePolicy(policy);
        auto *column = new QVBoxLayout(field);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(5);
        auto *label = new DesignLabel(title, field);
        auto *selector = new DesignComboBox(field);
        selector->setAccessibleName(title);
        label->setBuddy(selector);
        column->addWidget(label);
        column->addWidget(selector);
        fields->addWidget(field, 1);
        return selector;
    };
    auto *category = addField(QStringLiteral("分类"));
    category->addItems({QStringLiteral("作品"), QStringLiteral("女优"), QStringLiteral("排名")});
    auto *metric = addField(QStringLiteral("统计项"));
    auto *scope = addField(QStringLiteral("统计范围"));
    scope->addItem(QStringLiteral("公共数据库"), -1);
    scope->addItem(QStringLiteral("收藏库内"), 0);
    scope->addItem(QStringLiteral("撸过"), 1);
    scope->addItem(QStringLiteral("撸过加权"), 2);
    layout->addLayout(fields);

    auto *canvas = new StatisticsChart(m_themeService, content);
    m_chart = canvas;
    layout->addWidget(canvas, 1);

    const auto refresh = [this, categories, category, metric, scope, canvas] {
        const ChartOption &option = categories.at(category->currentIndex()).at(metric->currentIndex());
        scope->parentWidget()->setVisible(option.scoped);

        const int filter = scope->currentData().toInt();
        QString description = option.description;
        if (option.scoped) {
            description = QStringLiteral("%1 · %2").arg(scope->currentText(), description);
        }
        canvas->setHeading(option.title, description);

        StatisticsRepository repository(m_publicDatabase, m_privateDatabase);
        QString error;
        switch (option.metric) {
        case Metric::ReleaseYears:
            canvas->bars(repository.workReleaseYears(filter, &error));
            break;
        case Metric::WorkAge:
            canvas->histogram(repository.workActressAges(filter, &error),
                              QStringLiteral("平均拍摄年龄（岁）"), 72, 1, 16.5, 40.5);
            break;
        case Metric::AddedDates:
            canvas->line(repository.workAddedDates(&error));
            break;
        case Metric::DebutYears:
            canvas->bars(repository.actressDebutYears(filter, &error));
            break;
        case Metric::DebutAge:
            canvas->histogram(repository.actressDebutAges(&error),
                              QStringLiteral("出道年龄（岁）"), 40, 1);
            break;
        case Metric::Height:
            canvas->histogram(repository.actressHeights(filter, &error),
                              QStringLiteral("身高（cm）"), 0, 2);
            break;
        case Metric::WaistHip:
            canvas->bubbles(repository.waistHipDistribution(filter, &error));
            break;
        case Metric::Cup:
            canvas->pie(repository.cupDistribution(filter, &error));
            break;
        case Metric::Directors:
            canvas->bars(repository.topDirectors(filter, &error), true);
            break;
        case Metric::Makers:
            canvas->bars(repository.topMakers(filter, &error), true);
            break;
        case Metric::RecordedActresses:
            canvas->bars(repository.mostRecordedActresses(&error), true);
            break;
        }
        if (!error.isEmpty())
            canvas->message(QStringLiteral("图表数据加载失败"));
    };
    const auto selectCategory = [categories, metric, refresh](int index) {
        {
            const QSignalBlocker blocker(metric);
            metric->clear();
            for (const auto &option : categories.at(index))
                metric->addItem(option.title);
        }
        refresh();
    };
    connect(category, &QComboBox::currentIndexChanged, canvas, selectCategory);
    connect(metric, &QComboBox::currentIndexChanged, canvas, refresh);
    connect(scope, &QComboBox::currentIndexChanged, canvas, refresh);
    selectCategory(category->currentIndex());
}

QWidget *StatisticsChartPage::captureContent()
{
    initialize();
    return m_chart;
}

} // namespace darkeye
