#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QTimer;
class QVBoxLayout;

namespace darkeye {

class IconButton;
class ThemeService;

struct SidebarMenuDefinition
{
    QString id;
    QString text;
    QString icon;
};

class SidebarMenuButton final : public QWidget
{
    Q_OBJECT

public:
    explicit SidebarMenuButton(const QString &text, const QString &icon,
                               int expandedWidth, int collapsedWidth,
                               ThemeService *themes = nullptr,
                               QWidget *parent = nullptr);
    bool isSelected() const;
    void setSelected(bool selected);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void updateStyle();

    bool m_selected = false;
    ThemeService *m_themes = nullptr;
};

class Sidebar final : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(const QList<SidebarMenuDefinition> &menus = {},
                     ThemeService *themes = nullptr,
                     QWidget *parent = nullptr);
    Sidebar(const QList<SidebarMenuDefinition> &menus, const QString &iconsBasePath,
            ThemeService *themes = nullptr, QWidget *parent = nullptr);
    QString selectedId() const;
    bool isExpanded() const;
    void clearSelection();
    void select(const QString &menuId);
    void toggleMenu();

signals:
    void itemClicked(const QString &menuId);
    void selectionChanged(const QString &menuId);
    void backwardClicked();
    void forwardClicked();

private:
    SidebarMenuButton *createButton(const QString &text, const QString &icon);
    void handleMenuClick(const QString &menuId);

    int m_expandedWidth = 180;
    int m_collapsedWidth = 60;
    bool m_expanded = false;
    QString m_selectedId;
    QString m_iconsBasePath;
    ThemeService *m_themes = nullptr;
    QHash<QString, SidebarMenuButton *> m_buttons;
    QVBoxLayout *m_layout = nullptr;
    QPropertyAnimation *m_animation = nullptr;
};

class CalloutTooltip;
class ChamferButton;

class Sidebar2 final : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar2(const QList<SidebarMenuDefinition> &menus = {},
                      ThemeService *themes = nullptr,
                      QWidget *parent = nullptr);
    QString selectedId() const;
    void clearSelection();
    void select(const QString &menuId);
    void toggleMenu();

signals:
    void itemClicked(const QString &menuId);
    void selectionChanged(const QString &menuId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void handleClick(const QString &menuId);

    ThemeService *m_themes = nullptr;
    QString m_selectedId;
    QHash<QString, ChamferButton *> m_buttons;
    QHash<QObject *, QString> m_tooltipTexts;
    CalloutTooltip *m_tooltip = nullptr;
    QTimer *m_tooltipTimer = nullptr;
    ChamferButton *m_hoveredButton = nullptr;
};

} // namespace darkeye
