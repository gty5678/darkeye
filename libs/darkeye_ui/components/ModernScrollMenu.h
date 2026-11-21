#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class QButtonGroup;
class QFrame;
class QHBoxLayout;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace darkeye {

class ModernScrollMenu final : public QWidget
{
    Q_OBJECT

public:
    using Section = QPair<QString, QWidget *>;

    explicit ModernScrollMenu(const QList<Section> &sections = {},
                              QWidget *parent = nullptr);
    int sectionCount() const;
    int currentSection() const;
    void addSection(const QString &title, QWidget *content);
    void scrollToSection(int index);

private:
    void addNavigationButton(const QString &title, int index);
    void updateNavigation(int scrollValue);

    QHBoxLayout *m_navigationLayout = nullptr;
    QButtonGroup *m_buttonGroup = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QList<QWidget *> m_sections;
    QList<QPushButton *> m_buttons;
    QList<QFrame *> m_separators;
    QPropertyAnimation *m_animation = nullptr;
    bool m_animating = false;
};

} // namespace darkeye
