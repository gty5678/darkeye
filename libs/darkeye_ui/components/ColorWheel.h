#pragma once

#include <QWidget>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVector>
#include <QString>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QRectF>
#include <QKeyEvent>



// 前置声明，减少头文件依赖，加快编译速度
class QVBoxLayout;
class QLabel;
class QFocusEvent;

namespace darkeye {

class HexColorLineEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit HexColorLineEdit(QWidget *parent = nullptr);
    void setText(const QString &text);

signals:
    void validHexColor(const QString &hex);

protected:
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onTextChanged(const QString &text);

private:
    bool m_settingText = false;
};

class ColorLabel : public QPushButton
{
    Q_OBJECT

public:
    explicit ColorLabel(const QString &hexColor = "#FFFFFF", bool showText = true, QWidget *parent = nullptr);

    QString getColor() const;
    void setColor(const QString &color);

signals:
    void colorClicked(const QString &color);

private slots:
    void handleClicked();

private:
    void updateAppearance();
    static QColor textColorForBackground(const QColor &color);

    QString m_color;
    bool m_showText = true;
};

class OKLCHColorWheel : public QWidget
{
    Q_OBJECT

public:
    explicit OKLCHColorWheel(float l = 0.70f, float c = 0.12f, float h = 100.0f, QWidget *parent = nullptr);

    void setL(float l);
    void setC(float c);
    void setH(float h);

    float getL() const { return m_L; }
    float getC() const { return m_C; }
    float getH() const { return m_H; }
    QColor getCurrentColor() const;
    QString getHexColor() const;

    static bool srgbHexToOklch(const QString &hex, float &L, float &C, float &H);
    static QColor oklchToRgb(float l, float c, float h, bool autopair = true);

signals:
    void colorChanged(const QColor &color);
    void LChanged(float l);
    void CChanged(float c);
    void HChanged(float h);
    void mousePressed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateLayout();
    void generateOuterWheel();
    void generateInnerWheel();
    void generateSquare();
    void drawHuePointer(QPainter &painter);
    void drawInnerCircle(QPainter &painter);
    void updateHFromPos(const QPointF &pos);
    void updateLCFromPos(const QPointF &pos);

private:
    float m_L = 0.70f;
    float m_C = 0.12f;
    float m_H = 100.0f;

    bool m_isChoosingRing = false;
    bool m_isChoosingSquare = false;

    QPointF m_center;
    QRectF m_wheelRect;
    float m_outerRadius = 0;
    float m_midRadius = 0;
    float m_innerRadius = 0;
    int m_side = 0;

    QPixmap m_outerWheelPixmap;
    QPixmap m_innerWheelPixmap;
    QPixmap m_squarePixmap;
};

class ColorWheelSimple : public QWidget
{
    Q_OBJECT

public:
    explicit ColorWheelSimple(QWidget *parent = nullptr);

    void setL(float l);
    void setC(float c);
    void setH(float h);

    float getL() const;
    float getC() const;
    float getH() const;
    QColor getCurrentColor() const;
    QString getHexColor() const;

    void setHexColor(const QString &hex);
    void setInitialColor(const QString &hex);

signals:
    void colorChanged(const QColor &color);

private:
    void setupUi();
    void replaceHistory();
    void swapColor(ColorLabel *label);
    void onWheelChanged();

private:
    OKLCHColorWheel *m_wheel = nullptr;
    QWidget *m_bottomBar = nullptr;
    QHBoxLayout *m_bottomLayout = nullptr;
    QVector<ColorLabel *> m_historyLabels;
    QVector<QString> m_historyColors;
    ColorLabel *m_syncLabel = nullptr;
    HexColorLineEdit *m_hexLineEdit = nullptr;
};

} // namespace darkeye


