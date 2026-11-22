#pragma once

#include <QColor>
#include <QLabel>
#include <QTabBar>
#include <QWidget>

class QTimer;

namespace darkeye {

class ThemeService;

class VerticalTextLabel final : public QWidget
{
public:
    explicit VerticalTextLabel(const QString &text = {},
                               const QString &tone = QStringLiteral("normal"),
                               ThemeService *themes = nullptr,
                               QWidget *parent = nullptr);
    QString text() const;
    void setText(const QString &text);
    void setTextColor(const QColor &color);
    void setTone(const QString &tone);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor effectiveColor() const;
    QString m_text;
    QString m_tone;
    QColor m_overrideColor;
    ThemeService *m_themes = nullptr;
};

class TokenVLabel final : public QLabel
{
    Q_OBJECT

public:
    explicit TokenVLabel(const QString &text = {}, ThemeService *themes = nullptr,
                         QWidget *parent = nullptr);
    TokenVLabel(const QString &text, const QColor &background,
                const QColor &textColor = {}, int fixedWidth = 0,
                int fixedHeight = 0, const QColor &border = {},
                const QColor &hover = {}, ThemeService *themes = nullptr,
                QWidget *parent = nullptr);
    void setTextDynamic(const QString &text);
    void setColors(const QColor &background, const QColor &text,
                   const QColor &hover = {});
    void setBorderColor(const QColor &border);
    void setHoverColor(const QColor &hover);
    void flashInvert(int durationMilliseconds = 3000,
                     int intervalMilliseconds = 300);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void updateSize();

    ThemeService *m_themes = nullptr;
    QColor m_background;
    QColor m_textColor;
    QColor m_border;
    QColor m_hoverColor;
    bool m_hovered = false;
    bool m_inverted = false;
    bool m_explicitBackground = false;
    bool m_explicitTextColor = false;
    bool m_explicitBorder = false;
    bool m_explicitHoverColor = false;
    qint64 m_flashEnd = 0;
    QTimer *m_flashTimer = nullptr;
};

class TokenVerticalTabBar final : public QTabBar
{
public:
    explicit TokenVerticalTabBar(ThemeService *themes = nullptr,
                                 QWidget *parent = nullptr);
    QSize tabSizeHint(int index) const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ThemeService *m_themes = nullptr;
};

} // namespace darkeye
