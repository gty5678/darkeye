#pragma once

#include <QWidget>

class QBoxLayout;
class QToolButton;
class QVBoxLayout;

namespace darkeye {

class ThemeService;

class TokenCollapsibleSection final : public QWidget
{
    Q_OBJECT

public:
    explicit TokenCollapsibleSection(const QString &title = QStringLiteral("标题"),
                                     ThemeService *themes = nullptr,
                                     QWidget *parent = nullptr);
    void setTitle(const QString &title);
    bool isExpanded() const;
    QWidget *contentWidget() const;
    QVBoxLayout *contentLayout() const;
    void addWidget(QWidget *widget);
    void addLayout(QBoxLayout *layout);
    void expand();
    void collapse();

signals:
    void toggled(bool expanded);

private:
    void toggleContent(bool checked);
    void refreshIcon();

    bool m_expanded = false;
    ThemeService *m_themes = nullptr;
    QToolButton *m_toggle = nullptr;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
};

} // namespace darkeye
