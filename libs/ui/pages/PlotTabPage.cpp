#include "ui/pages/PlotTabPage.h"

#include "database/repositories/StatisticsRepository.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QButtonGroup>
#include <QDate>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace darkeye
{
namespace
{

class StatisticsChart final : public QWidget
{
public:
    enum class Type { Message, Bars, Histogram, Pie, Bubbles, Line };
    explicit StatisticsChart(ThemeService &themes, QWidget *parent = nullptr)
        : QWidget(parent), m_themes(themes)
    {
        setMinimumSize(560, 360);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(&m_themes, &ThemeService::themeChanged, this, [this] { update(); });
    }
    void message(const QString &text) { m_type = Type::Message; m_title = text; update(); }
    void bars(QString title, QVector<ChartValue> values, bool horizontal = false)
    { m_type=Type::Bars; m_title=std::move(title); m_values=std::move(values); m_horizontal=horizontal; update(); }
    void histogram(QString title, QVector<WeightedChartValue> values, QString unit,
                   int bins, qreal bandwidth, qreal minimum = 0, qreal maximum = 0)
    {
        m_type = Type::Histogram;
        m_title = std::move(title);
        m_histogramMinimum = minimum;
        m_histogramMaximum = maximum;
        if (maximum > minimum) {
            values.erase(std::remove_if(values.begin(), values.end(),
                                        [=](const auto &value) {
                                            return !std::isfinite(value.value)
                                                   || value.value < minimum
                                                   || value.value > maximum;
                                        }), values.end());
        }
        m_weighted = std::move(values);
        m_unit = std::move(unit);
        m_bins = bins;
        m_bandwidth = bandwidth;
        update();
    }
    void pie(QString title, QVector<ChartValue> values)
    { m_type=Type::Pie; m_title=std::move(title); m_values=std::move(values); update(); }
    void bubbles(QString title, QVector<WaistHipChartValue> values)
    { m_type=Type::Bubbles; m_title=std::move(title); m_bubbles=std::move(values); update(); }
    void line(QString title, QVector<QDate> dates)
    { m_type=Type::Line; m_title=std::move(title); m_dates=std::move(dates); update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), palette().base());
        p.setPen(palette().text().color()); QFont f=p.font(); f.setBold(true); p.setFont(f);
        p.drawText(QRect(12,10,width()-24,28),Qt::AlignCenter,m_title);
        if(m_type==Type::Message){p.drawText(rect(),Qt::AlignCenter,m_title);return;}
        if((m_type==Type::Histogram&&m_weighted.isEmpty()) || (m_type==Type::Line&&m_dates.isEmpty()) || (m_type!=Type::Histogram&&m_type!=Type::Line&&m_values.isEmpty()&&m_bubbles.isEmpty())){p.drawText(rect(),Qt::AlignCenter,QStringLiteral("暂无数据"));return;}
        if(m_type==Type::Bars) paintBars(p); else if(m_type==Type::Histogram) paintHistogram(p); else if(m_type==Type::Pie) paintPie(p); else if(m_type==Type::Bubbles) paintBubblesPython(p); else paintLine(p);
    }

private:
    QColor primaryTint(qreal strength) const
    {
        const ThemeTokens tokens = m_themes.currentTokens();
        const QColor primary(tokens.primary);
        const QColor background(tokens.background);
        return QColor::fromRgbF(
            background.redF() + (primary.redF() - background.redF()) * strength,
            background.greenF() + (primary.greenF() - background.greenF()) * strength,
            background.blueF() + (primary.blueF() - background.blueF()) * strength);
    }

    ThemeService &m_themes;

    void paintBubblesPython(QPainter &p)
    {
        const QRect canvas = rect();
        const QRect plot = canvas.adjusted(72, 66, -92, -64);
        const QRect colorBar(canvas.right() - 80, plot.top(), 24, plot.height());
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
        p.setPen(QPen(QColor("#dadce0"), 1));
        for (qreal waist = minWaistGrid; waist <= maxWaistGrid; waist += gridSpacing) {
            const qreal x = plot.left() + (waist - minWaistGrid) / waistSpan * plot.width();
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (qreal hip = minHipGrid; hip <= maxHipGrid; hip += gridSpacing) {
            const qreal y = plot.bottom() - (hip - minHipGrid) / hipSpan * plot.height();
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        p.setFont(QFont(p.font().family(), 8)); p.setPen(QColor("#5f6368"));
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
        for (const auto &point : m_bubbles) {
            const QPointF center(plot.left() + (point.waist - minWaistGrid) / waistSpan * plot.width(),
                                 plot.bottom() - (point.hip - minHipGrid) / hipSpan * plot.height());
            const qreal radius = 4 + 16 * point.weight / maxWeight;
            p.setBrush(colorFor(point.ratio)); p.setPen(Qt::white); p.drawEllipse(center, radius, radius);
        }
        for (int y = 0; y < colorBar.height(); ++y) {
            const qreal value = colorMax - (colorMax - colorMin) * y / qMax(1, colorBar.height() - 1);
            p.fillRect(colorBar.left(), colorBar.top() + y, colorBar.width(), 1, colorFor(value));
        }
        p.setPen(QColor("#5f6368")); p.setBrush(Qt::NoBrush); p.drawRect(colorBar);
        p.drawText(QRectF(colorBar.left() - 8, colorBar.top() - 22, 80, 18), Qt::AlignCenter, QStringLiteral("腰臀比"));
        for (int i = 0; i <= 5; ++i) {
            const qreal y = colorBar.top() + colorBar.height() * i / 5.0F;
            const qreal value = colorMax - (colorMax - colorMin) * i / 5.0F;
            p.drawLine(QPointF(colorBar.left() - 4, y), QPointF(colorBar.left(), y));
            p.drawText(QRectF(colorBar.right() + 5, y - 9, 48, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(value, 'f', 2));
        }
    }

    QRect plot() const { return rect().adjusted(90, 66, -34, -84); }

    void axes(QPainter &p, const QRect &r, const QString &xLabel,
              const QString &yLabel)
    {
        p.setPen(QPen(QColor("#444444"), 1));
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.bottomLeft(), r.topLeft());

        p.setFont(QFont(p.font().family(), 9));
        p.drawText(QRect(r.left(), r.bottom() + 38, r.width(), 24),
                   Qt::AlignCenter, xLabel);
        p.save();
        p.translate(18, r.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-r.height() / 2, 0, r.height(), 24), Qt::AlignCenter,
                   yLabel);
        p.restore();
    }

    void drawYTicks(QPainter &p, const QRect &r, qreal maximum)
    {
        const qreal scale = maximum > 0 ? maximum : 1;
        p.setFont(QFont(p.font().family(), 8));
        p.setPen(QColor("#9aa0a6"));
        for (int tick = 0; tick <= 4; ++tick) {
            const qreal value = scale * tick / 4;
            const qreal y = r.bottom() - r.height() * tick / 4;
            p.drawLine(r.left(), y, r.right(), y);
            p.drawText(QRectF(42, y - 9, r.left() - 50, 18),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(value, 'g', 3));
        }
    }

    void paintBars(QPainter&p)
    {
        QRect r=plot();const qreal maximum=std::max_element(m_values.cbegin(),m_values.cend(),[](const auto&a,const auto&b){return a.value<b.value;})->value;const qreal scale=maximum == 0 ? 1 : maximum;p.setFont(QFont(p.font().family(),8));const int n=m_values.size();
        if (!m_horizontal) {
            drawYTicks(p, r, maximum);
            axes(p, r, QStringLiteral("年份"), QStringLiteral("数量"));
        } else {
            axes(p, r, QString(), QString());
        }
        if(m_horizontal){const qreal h=r.height()/n;for(int i=0;i<n;++i){const auto&v=m_values.at(i);const qreal w=(r.width()-8)*v.value/scale;p.fillRect(QRectF(r.left(),r.top()+i*h+3,w,h-7),primaryTint(0.30));p.setPen(palette().text().color());p.drawText(QRectF(4,r.top()+i*h,r.left()-9,h).toRect(),Qt::AlignRight|Qt::AlignVCenter,v.label);p.drawText(QPointF(r.left()+w+4,r.top()+i*h+h/2+4),QString::number(v.value,'g',4));}return;}
        const qreal w=std::max(static_cast<qreal>(3.0),static_cast<qreal>((r.width()-8)/n-4));for(int i=0;i<n;++i){const auto&v=m_values.at(i);const qreal h=(r.height()-5)*v.value/scale;const qreal x=r.left()+4+i*(w+4);p.fillRect(QRectF(x,r.bottom()-h,w,h),primaryTint(0.30));p.setPen(palette().text().color());p.drawText(QRectF(x-12,r.bottom()-h-18,w+24,16),Qt::AlignCenter,QString::number(v.value,'g',4));p.save();p.translate(x+w/2,r.bottom()+10);p.rotate(-45);p.drawText(QRect(-38,0,76,16),Qt::AlignCenter,v.label);p.restore();}
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
        for (auto &value : density)
            value /= qMax<qreal>(1, totalWeight * binWidth);

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
        axes(p, r, m_unit, QStringLiteral("频率"));
        const qreal w = static_cast<qreal>(r.width()) / bins;
        const qreal gap = qMin<qreal>(2, w * 0.2);
        const QColor barColor = primaryTint(0.30);
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
        p.setPen(QPen(primaryTint(0.70), 2));
        p.drawPath(path);

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
            p.setPen(QPen(QColor("#D93025"), 1, Qt::DashLine));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.setPen(QColor("#D93025"));
            p.drawText(QRectF(x - 48, r.top() + 4, 96, 18), Qt::AlignCenter, label);
        };
        if (qFuzzyCompare(low, high))
            drawPercentile(low, QStringLiteral("5% / 95%"));
        else {
            drawPercentile(low, QStringLiteral("5%"));
            drawPercentile(high, QStringLiteral("95%"));
        }
        p.restore();
        p.setPen(QColor("#5f6368"));
        p.setFont(QFont(p.font().family(), 8));
        const auto drawTick = [&](qreal x, const QString &label) {
            p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 5));
            p.drawText(QRectF(x - 28, r.bottom() + 8, 56, 18), Qt::AlignCenter, label);
        };
        if (integerBins || (fixedRange && qFuzzyCompare(binWidth, qreal(1)))) {
            // Thin labels when needed, without changing the unit-width bins.
            const int labelWidth = p.fontMetrics().horizontalAdvance(
                                       QString::number(qRound(hi - 0.5))) + 10;
            const int tickStep = qMax(1, static_cast<int>(std::ceil(labelWidth / w)));
            for (int i = 0; i < bins; i += tickStep)
                drawTick(r.left() + (i + 0.5) * w, QString::number(qRound(lo + 0.5) + i));
        } else {
            for (int tick = 0; tick <= 4; ++tick)
                drawTick(r.left() + r.width() * tick / 4.0,
                         QString::number(lo + (hi - lo) * tick / 4.0, 'g', 3));
        }
    }
    void paintPie(QPainter&p)
    {
        qreal total = 0;
        for (const auto &value : m_values) {
            total += value.value;
        }
        if (total <= 0) {
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无数据"));
            return;
        }

        p.setFont(QFont(p.font().family(), 9));
        qreal legendTextWidth = 0;
        for (const auto &value : m_values) {
            const QString text = QStringLiteral("%1  %2%")
                                     .arg(value.label)
                                     .arg(value.value / total * 100, 0, 'f', 1);
            legendTextWidth = qMax<qreal>(legendTextWidth,
                                          p.fontMetrics().horizontalAdvance(text));
        }

        constexpr qreal margin = 24;
        constexpr qreal contentTop = 52;
        constexpr qreal legendGap = 24;
        const qreal legendWidth = qMin(qMax<qreal>(140, legendTextWidth + 22),
                                       qMax<qreal>(140, width() * 0.34F));
        const qreal legendX = width() - margin - legendWidth;
        const QRectF pieArea(margin, contentTop,
                             qMax<qreal>(40, legendX - legendGap - margin),
                             qMax<qreal>(40, height() - contentTop - margin));
        const qreal size = qMin(pieArea.width(), pieArea.height());
        const QRectF circle(pieArea.center().x() - size / 2,
                            pieArea.center().y() - size / 2, size, size);

        int start = 140 * 16;
        const QList<QColor> colors{
            QColor("#6baed6"), QColor("#9ecae1"), QColor("#74c69d"),
            QColor("#f4a261"), QColor("#e76f51"), QColor("#b07aa1"),
            QColor("#edc948")};
        for (int i = 0; i < m_values.size(); ++i) {
            const auto &value = m_values.at(i);
            const int span = qRound(-value.value / total * 5760);
            p.setBrush(colors.at(i % colors.size()));
            p.setPen(Qt::white);
            p.drawPie(circle, start, span);
            const qreal percent = value.value / total * 100;
            if (percent >= 3) {
                const qreal angle = (start + span / 2) / 16.0F
                                    * std::numbers::pi_v<qreal> / 180.0F;
                const QPointF center =
                    circle.center()
                    + QPointF(std::cos(angle) * circle.width() * .32F,
                              -std::sin(angle) * circle.width() * .32F);
                p.setPen(QColor("#202124"));
                p.drawText(QRectF(center.x() - 38, center.y() - 15, 76, 30),
                           Qt::AlignCenter,
                           QStringLiteral("%1\n%2%")
                               .arg(value.label)
                               .arg(percent, 0, 'f', 1));
            }
            start += span;
        }

        p.setPen(palette().text().color());
        for (int i = 0; i < m_values.size() && i < 12; ++i) {
            const qreal y = circle.top() + i * 24;
            p.fillRect(QRectF(legendX, y + 5, 14, 14),
                       colors.at(i % colors.size()));
            const auto &value = m_values.at(i);
            p.drawText(QRectF(legendX + 22, y, legendWidth - 22, 22),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1  %2%")
                           .arg(value.label)
                           .arg(value.value / total * 100, 0, 'f', 1));
        }
    }
    void paintLine(QPainter&p)
    {
        std::sort(m_dates.begin(),m_dates.end());QMap<QDate,int> increments;for(const auto&d:m_dates)++increments[d];const QDate first=increments.firstKey(),last=increments.lastKey();QVector<QPointF> points;int total=0;const QRect r=rect().adjusted(72,52,-38,-96);const int daySpan=qMax(1,first.daysTo(last));for(QDate d=first;d<=last;d=d.addDays(1)){total+=increments.value(d);points.append({static_cast<qreal>(r.left())+static_cast<qreal>(r.width())*first.daysTo(d)/daySpan,static_cast<qreal>(r.bottom())-static_cast<qreal>(r.height())*total/static_cast<qreal>(m_dates.size())});}axes(p,r,QStringLiteral("日期"),QStringLiteral("作品数量"));p.setPen(QPen(QColor("#9aa0a6"),1));p.setFont(QFont(p.font().family(),8));for(int i=0;i<=4;++i){const qreal y=r.bottom()-r.height()*i/4;p.drawLine(QPointF(r.left(),y),QPointF(r.right(),y));p.drawText(QRectF(8,y-9,r.left()-14,18),Qt::AlignRight|Qt::AlignVCenter,QString::number(m_dates.size()*i/4));}p.setPen(QPen(QColor("#2f6f9f"),2));p.drawPolyline(points);p.setPen(QColor("#5f6368"));for(int i=0;i<8;++i){const int offset=qRound(daySpan*i/7.0);const qreal x=r.left()+r.width()*offset/daySpan;const QString label=first.addDays(offset).toString(QStringLiteral("yyyy-MM-dd"));p.drawLine(QPointF(x,r.bottom()),QPointF(x,r.bottom()+5));p.drawText(QRectF(x-48,r.bottom()+8+((i%2)!=0?16:0),96,18),Qt::AlignCenter,label);}
    }
    qreal m_histogramMinimum = 0;
    qreal m_histogramMaximum = 0;
    Type m_type=Type::Message;QString m_title=QStringLiteral("请选择右侧统计项"),m_unit;QVector<ChartValue> m_values;QVector<WeightedChartValue> m_weighted;QVector<WaistHipChartValue> m_bubbles;QVector<QDate> m_dates;int m_bins=30;qreal m_bandwidth=1;bool m_horizontal=false;
};
}

PlotTabPage::PlotTabPage(QSqlDatabase publicDatabase,QSqlDatabase privateDatabase,ThemeService &themeService,QWidget*parent):LazyWidget(parent),m_publicDatabase(std::move(publicDatabase)),m_privateDatabase(std::move(privateDatabase)),m_themeService(themeService){}

void PlotTabPage::lazyLoad()
{
    auto *splitter=new QSplitter(this);splitter->setChildrenCollapsible(false);splitter->setHandleWidth(1);splitter->setObjectName(QStringLiteral("StatisticsSplitter"));splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: #cccccc; width: 1px; height: 1px; border: none; margin: 0; } QSplitter::handle:hover { background: #888888; }"));auto *canvas=new StatisticsChart(m_themeService,splitter);auto *toolbar=new QWidget(splitter);splitter->addWidget(canvas);splitter->addWidget(toolbar);splitter->setStretchFactor(0,1);splitter->setStretchFactor(1,9);splitter->setSizes({1002,375});auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->addWidget(splitter);
    m_chart = canvas;
    canvas->setToolTip(QStringLiteral("按 C 保存当前图表"));
    auto *tools=new QVBoxLayout(toolbar);auto *scopeBox=new TokenGroupBox(QStringLiteral("选择统计范围"),toolbar);auto *scopeLayout=new QVBoxLayout(scopeBox);auto *group=new QButtonGroup(scopeBox);const QList<QPair<QString,int>> scopes{{QStringLiteral("公共数据库"),-1},{QStringLiteral("收藏库内"),0},{QStringLiteral("撸过"),1},{QStringLiteral("撸过加权"),2}};for(const auto &[label,scope]:scopes){auto *radio=new TokenRadioButton(label,scopeBox);group->addButton(radio,scope);scopeLayout->addWidget(radio);if(scope==1)radio->setChecked(true);}tools->addWidget(scopeBox);auto *stats=new TokenGroupBox(QStringLiteral("多样化统计"),toolbar);auto *buttons=new QVBoxLayout(stats);tools->addWidget(stats);const auto scope=[group]{return group->checkedId();};const auto add=[buttons,canvas,stats](const QString &text,auto callback){auto *button=new DesignButton(text,stats);buttons->addWidget(button);QObject::connect(button,&QPushButton::clicked,canvas,callback);};
    add(QStringLiteral("作品拍摄时女优年龄分布直方图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->histogram(QStringLiteral("作品中女优平均拍摄年龄分布"),r.workActressAges(scope()),QStringLiteral("平均拍摄年龄（岁）"),24,1,16.5,40.5);});
    add(QStringLiteral("作品发行年份分布直方图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->bars(QStringLiteral("每年数量统计"),r.workReleaseYears(scope()));});
    add(QStringLiteral("女优出道年份分布直方图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->bars(QStringLiteral("女优出道年份统计"),r.actressDebutYears(scope()));});
    add(QStringLiteral("女优身高分布直方图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->histogram(QStringLiteral("女优身高分布"),r.actressHeights(scope()),QStringLiteral("身高（cm）"),0,2);});
    add(QStringLiteral("女优身材腰臀比气泡图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->bubbles(QStringLiteral("女优腰臀比（颜色=腰臀比，大小=人数）"),r.waistHipDistribution(scope()));});
    add(QStringLiteral("女优罩杯分布饼图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->pie(QStringLiteral("女优罩杯分布（日本罩杯比国内大两个）"),r.cupDistribution(scope()));});
    add(QStringLiteral("导演统计柱状图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->bars(QStringLiteral("导演作品数量排名"),r.topDirectors(scope()),true);});
    add(QStringLiteral("制作商统计柱状图"),[this,canvas,scope]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->bars(QStringLiteral("片商数量排名"),r.topMakers(scope()),true);});
    auto *disabled=new DesignButton(QStringLiteral("Tag词云生成"),toolbar);disabled->setEnabled(false);tools->addWidget(disabled);
    add(QStringLiteral("最喜欢的女优柱状图"),[this,canvas]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->bars(QStringLiteral("女优按撸管次数排名"),r.mostRecordedActresses(),true);});
    add(QStringLiteral("按添加时间统计作品数"),[this,canvas]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->line(QStringLiteral("添加到数据库中作品数量随时间分布"),r.workAddedDates());});
    add(QStringLiteral("女优出道年龄分布直方图"),[this,canvas]{StatisticsRepository r(m_publicDatabase,m_privateDatabase);canvas->histogram(QStringLiteral("女优平均出道年龄分布（以出道日期减半年计算）"),r.actressDebutAges(),QStringLiteral("出道年龄"),40,1);});
    auto *yearReport=new DesignButton(QStringLiteral("撸管年回忆录"),toolbar);yearReport->setEnabled(false);tools->addWidget(yearReport);tools->addStretch();
}

QWidget *PlotTabPage::captureContent()
{
    initialize();
    return m_chart;
}

void PlotTabPage::refresh(){initialize();}
} // namespace darkeye
