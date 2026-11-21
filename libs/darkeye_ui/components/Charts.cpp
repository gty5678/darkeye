#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/components/Charts.h"

#include "darkeye_ui/theme/ThemeService.h"

#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QPainter>

#include <cmath>
#include <numbers>

namespace darkeye {
namespace {

ThemeTokens tokensFor(ThemeService *themes)
{
    return ThemeService::tokens(themes == nullptr ? ThemeId::Light
                                                   : themes->current(),
                                themes == nullptr ? QString()
                                                   : themes->customPrimary());
}

QPointF radarPoint(int index, int count, qreal radius)
{
    constexpr qreal center = 100.0;
    const qreal angle = -2.0 * std::numbers::pi * index / count
                        + std::numbers::pi / 2.0;
    return {center + radius * std::cos(angle), center - radius * std::sin(angle)};
}

qreal radarAngle(int index, int count)
{
    return -2.0 * std::numbers::pi * index / count + std::numbers::pi / 2.0;
}

} // namespace

CalendarHeatmap::CalendarHeatmap(int year, const QHash<QDate, int> &data,
                                 ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_year(year), m_data(data), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignCalendarHeatmap"));
    setFixedSize(750, 155);
    setAttribute(Qt::WA_TranslucentBackground);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this, [this] { update(); });
    }
}

int CalendarHeatmap::year() const { return m_year; }
QHash<QDate, int> CalendarHeatmap::data() const { return m_data; }
void CalendarHeatmap::updateData(int year, const QHash<QDate, int> &data)
{
    m_year = year;
    m_data = data;
    update();
}

QSize CalendarHeatmap::sizeHint() const
{
    const int columns = columnForDate(QDate(m_year, 12, 31)) + 1;
    return {columns * 10 + qMax(0, columns - 1) * 3, 7 * 10 + 6 * 3};
}

int CalendarHeatmap::columnForDate(const QDate &date) const
{
    const QDate first(m_year, 1, 1);
    return (first.dayOfWeek() - 1 + first.daysTo(date)) / 7;
}

void CalendarHeatmap::paintEvent(QPaintEvent *)
{
    const ThemeTokens tokens = tokensFor(m_themes);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(tokens.background));
    painter.setPen(QPen(QColor(tokens.border), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chamferPath(rect().adjusted(10, 10, -10, -10), 8));
    painter.setPen(QColor(tokens.text));
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QStringList weekDays{QStringLiteral("一"), QStringLiteral("二"),
                               QStringLiteral("三"), QStringLiteral("四"),
                               QStringLiteral("五"), QStringLiteral("六"),
                               QStringLiteral("日")};
    for (int row = 0; row < 7; ++row) {
        painter.drawText(15, 40 + row * 13 + 8, weekDays.at(row));
    }
    const QStringList months{QStringLiteral("一月"), QStringLiteral("二月"),
                             QStringLiteral("三月"), QStringLiteral("四月"),
                             QStringLiteral("五月"), QStringLiteral("六月"),
                             QStringLiteral("七月"), QStringLiteral("八月"),
                             QStringLiteral("九月"), QStringLiteral("十月"),
                             QStringLiteral("十一月"), QStringLiteral("十二月")};
    for (int month = 1; month <= 12; ++month) {
        const int firstColumn = columnForDate(QDate(m_year, month, 1));
        const int lastColumn = columnForDate(QDate(m_year, month,
                                                   QDate(m_year, month, 1).daysInMonth()));
        const qreal center = 40 + ((firstColumn + lastColumn + 1) / 2.0) * 13;
        const QString label = months.at(month - 1);
        painter.drawText(qRound(center - painter.fontMetrics().horizontalAdvance(label) / 2.0),
                         30, label);
    }
    for (QDate date(m_year, 1, 1); date <= QDate(m_year, 12, 31);
         date = date.addDays(1)) {
        const int column = columnForDate(date);
        const int row = date.dayOfWeek() - 1;
        const int value = qBound(0, m_data.value(date, 0), 4);
        static const QColor colors[] = {QColor(230, 230, 230), QColor(100, 255, 100),
                                        QColor(150, 200, 50), QColor(255, 200, 0),
                                        QColor(255, 100, 100)};
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors[value]);
        painter.drawRoundedRect(QRectF(40 + column * 13, 40 + row * 13, 10, 10),
                                2, 2);
    }
}

RadarChartWidget::RadarChartWidget(const QStringList &categories,
                                   const QVector<qreal> &values,
                                   const QStringList &displayValues, int layers,
                                   ThemeService *themes, QWidget *parent)
    : QGraphicsView(parent), m_categories(categories), m_values(values),
      m_displayValues(displayValues.isEmpty() ? QStringList() : displayValues),
      m_layers(qMax(1, layers)), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignRadarChart"));
    setFrameStyle(QFrame::NoFrame);
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this, [this] { rebuild(); });
    }
    rebuild();
}

void RadarChartWidget::updateChart(const QStringList &categories,
                                   const QVector<qreal> &values,
                                   const QStringList &displayValues)
{
    m_categories = categories;
    m_values = values;
    m_displayValues = displayValues;
    rebuild();
}
QStringList RadarChartWidget::categories() const { return m_categories; }
QVector<qreal> RadarChartWidget::values() const { return m_values; }

void RadarChartWidget::rebuild()
{
    scene()->clear();
    const int count = qMin(m_categories.size(), m_values.size());
    if (count < 3) return;
    const ThemeTokens tokens = tokensFor(m_themes);
    const QPen grid(QColor(tokens.border), 1);
    for (int layer = 1; layer <= m_layers; ++layer) {
        QPolygonF polygon;
        for (int index = 0; index < count; ++index)
            polygon << radarPoint(index, count, 80.0 * layer / m_layers);
        scene()->addPolygon(polygon, grid, Qt::NoBrush);
    }
    for (int index = 0; index < count; ++index) {
        scene()->addLine(QLineF(QPointF(100, 100), radarPoint(index, count, 80)), grid);
        auto *label = scene()->addText(m_categories.at(index));
        label->setDefaultTextColor(QColor(tokens.text));
        const QPointF point = radarPoint(index, count, 100);
        label->setPos(point.x() - label->boundingRect().width() / 2,
                      point.y() - label->boundingRect().height() / 2);
    }
    QPolygonF dataPolygon;
    for (int index = 0; index < count; ++index) {
        const QPointF point = radarPoint(index, count,
                                         80 * qBound(0.0, m_values.at(index), 1.0));
        dataPolygon << point;
        const QString shown = index < m_displayValues.size()
                                  ? m_displayValues.at(index)
                                  : QString::number(m_values.at(index));
        auto *label = scene()->addText(shown);
        label->setDefaultTextColor(QColor(tokens.primary));
        const qreal angle = radarAngle(index, count);
        label->setPos(
            point.x() + 10.0 * std::cos(angle)
                - label->boundingRect().width() / 2.0,
            point.y() - 10.0 * std::sin(angle)
                - label->boundingRect().height() / 2.0);
    }
    QColor fill(tokens.primary);
    fill.setAlpha(100);
    scene()->addPolygon(dataPolygon, QPen(QColor(tokens.primary), 2), fill);
    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-5, -5, 5, 5));
}

} // namespace darkeye


