#pragma once

#include "darkeye_ui/theme/ThemeService.h"

#include <optional>
#include <QWidget>

class QTimer;

namespace darkeye {

class Skeleton final : public QWidget
{
    Q_OBJECT

public:
    explicit Skeleton(int height = 14, int radius = 6, bool animated = true,
                      int intervalMilliseconds = 35,
                      ThemeService *themes = nullptr, QWidget *parent = nullptr);
    void start();
    void stop();
    void setAnimated(bool animated);
    bool isAnimating() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_radius = 6;
    int m_offset = 0;
    ThemeService *m_themes = nullptr;
    QTimer *m_timer = nullptr;
};

class CalloutTooltip final : public QWidget
{
public:
    explicit CalloutTooltip(ThemeService *themes = nullptr,
                            QWidget *parent = nullptr);
    void setTokens(const ThemeTokens &tokens);
    void clearTokens();
    void showFor(QWidget *target, const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ThemeTokens tokens() const;

    QString m_text;
    ThemeService *m_themes = nullptr;
    std::optional<ThemeTokens> m_overrideTokens;
};

} // namespace darkeye


