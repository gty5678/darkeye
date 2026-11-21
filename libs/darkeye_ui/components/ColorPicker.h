#pragma once

#include <QColor>
#include <QLabel>

namespace darkeye {

class ColorWheelSimple;

class ColorPicker final : public QLabel
{
    Q_OBJECT

public:
    enum class Shape { Rectangle, Octagon, Circle = Octagon };

    explicit ColorPicker(const QColor &color = QColor(QStringLiteral("#cccccc")),
                         bool showText = true, Shape shape = Shape::Rectangle,
                         QWidget *parent = nullptr);
    QString color() const;
    QString getColor() const { return color(); }
    void setColor(const QString &color);
    void setShowText(bool show);
    void setShape(Shape shape);

signals:
    void colorChanged(const QString &color);
    void colorConfirmed(const QString &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateDisplay();

    QColor m_color;
    bool m_showText = true;
    Shape m_shape = Shape::Rectangle;
    ColorWheelSimple *m_colorWheel = nullptr;
};

} // namespace darkeye
