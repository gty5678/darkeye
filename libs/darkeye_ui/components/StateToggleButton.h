#pragma once

#include <QPushButton>

namespace darkeye {

class ThemeService;

class StateToggleButton final : public QPushButton
{
    Q_OBJECT

public:
    explicit StateToggleButton(const QString &state1Icon = QStringLiteral("x"),
                               const QString &state2Icon = QStringLiteral("check"),
                               int iconSize = 24, int outerSize = 24,
                               ThemeService *themes = nullptr,
                               QWidget *parent = nullptr);

    bool state() const;
    bool getState() const { return state(); }
    void setState(bool state);

signals:
    void stateChanged(bool state);

private:
    void toggleState();
    void refreshIcons();

    QString m_state1Icon;
    QString m_state2Icon;
    int m_iconSize = 24;
    bool m_state = false;
    ThemeService *m_themes = nullptr;
};

} // namespace darkeye
