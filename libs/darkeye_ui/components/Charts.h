#pragma once

#include <QDate>
#include <QGraphicsView>
#include <QHash>
#include <QStringList>
#include <QWidget>

namespace darkeye {

class ThemeService;

class CalendarHeatmap final : public QWidget
{
public:
    explicit CalendarHeatmap(int year = 2025,
                             const QHash<QDate, int> &data = {},
                             ThemeService *themes = nullptr,
                             QWidget *parent = nullptr);
    int year() const;
    QHash<QDate, int> data() const;
    void updateData(int year, const QHash<QDate, int> &data);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int columnForDate(const QDate &date) const;

    int m_year = 2025;
    QHash<QDate, int> m_data;
    ThemeService *m_themes = nullptr;
};

class RadarChartWidget final : public QGraphicsView
{
public:
    explicit RadarChartWidget(const QStringList &categories = {},
                              const QVector<qreal> &values = {},
                              const QStringList &displayValues = {},
                              int layers = 5, ThemeService *themes = nullptr,
                              QWidget *parent = nullptr);
    void updateChart(const QStringList &categories,
                     const QVector<qreal> &values,
                     const QStringList &displayValues = {});
    QStringList categories() const;
    QVector<qreal> values() const;

private:
    void rebuild();

    QStringList m_categories;
    QVector<qreal> m_values;
    QStringList m_displayValues;
    int m_layers = 5;
    ThemeService *m_themes = nullptr;
};

} // namespace darkeye
