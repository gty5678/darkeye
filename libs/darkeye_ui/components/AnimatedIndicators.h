#pragma once

#include <QColor>
#include <QWidget>

class QPropertyAnimation;
class QTimer;

namespace darkeye {

class ThemeService;

class ToggleSwitch final : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal offset READ offset WRITE setOffset)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)

public:
    explicit ToggleSwitch(int width = 40, int height = 24,
                          ThemeService *themes = nullptr,
                          QWidget *parent = nullptr);

    bool isChecked() const;
    void setChecked(bool checked);
    qreal offset() const;
    void setOffset(qreal offset);
    QColor backgroundColor() const;
    void setBackgroundColor(const QColor &color);
    void refreshTokens();

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    qreal endOffset(bool checked) const;

    bool m_checked = false;
    qreal m_offset = 2.0;
    QColor m_background;
    QColor m_inactive;
    QColor m_active;
    QColor m_thumb;
    ThemeService *m_themes = nullptr;
    QPropertyAnimation *m_offsetAnimation = nullptr;
    QPropertyAnimation *m_colorAnimation = nullptr;
};

class CircularLoading final : public QWidget
{
    Q_OBJECT

public:
    explicit CircularLoading(int size = 32, int strokeWidth = 0,
                             ThemeService *themes = nullptr,
                             QWidget *parent = nullptr);
    void start();
    void stop();
    bool isAnimating() const;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void refreshTokens();

    int m_size = 32;
    int m_stroke = 4;
    qreal m_angle = 0.0;
    QColor m_arc;
    QColor m_track;
    ThemeService *m_themes = nullptr;
    QTimer *m_timer = nullptr;
};

} // namespace darkeye
