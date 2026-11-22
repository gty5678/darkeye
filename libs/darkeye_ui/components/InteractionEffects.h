#pragma once

#include "darkeye_ui/components/IconButton.h"

#include <QSlider>

class QPropertyAnimation;

namespace darkeye {

class ThemeService;

class RotateButton final : public IconButton
{
    Q_OBJECT
    Q_PROPERTY(qreal angle READ angle WRITE setAngle)

public:
    explicit RotateButton(const QString &iconName = QStringLiteral("settings"),
                          ThemeService *themes = nullptr,
                          QWidget *parent = nullptr);
    RotateButton(const QString &iconName, const QString &iconPath, int iconSize,
                 int outerSize, bool hoverable = true,
                 ThemeService *themes = nullptr, QWidget *parent = nullptr);
    qreal angle() const;
    void setAngle(qreal angle);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    qreal m_angle = 0.0;
    QPropertyAnimation *m_animation = nullptr;
};

class ShakeButton final : public IconButton
{
    Q_OBJECT
    Q_PROPERTY(qreal iconOffset READ iconOffset WRITE setIconOffset)

public:
    explicit ShakeButton(const QString &iconName = QStringLiteral("settings"),
                         ThemeService *themes = nullptr,
                         QWidget *parent = nullptr);
    ShakeButton(const QString &iconName, const QString &iconPath, int iconSize,
                int outerSize, bool hoverable = true,
                ThemeService *themes = nullptr, QWidget *parent = nullptr);
    qreal iconOffset() const;
    void setIconOffset(qreal offset);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    qreal m_offset = 0.0;
    QPropertyAnimation *m_animation = nullptr;
};

class ClickableSlider final : public QSlider
{
public:
    explicit ClickableSlider(Qt::Orientation orientation = Qt::Horizontal,
                             ThemeService *themes = nullptr,
                             QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
};

} // namespace darkeye
