#include "darkeye_ui/components/ModernScrollMenu.h"

#include "darkeye_ui/components/DesignLabel.h"

#include <QButtonGroup>
#include <QEasingCurve>
#include <QFrame>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace darkeye {

ModernScrollMenu::ModernScrollMenu(const QList<Section> &sections, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DesignModernScrollMenu"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *navigation = new QWidget(this);
    navigation->setObjectName(QStringLiteral("DesignScrollMenuNavigation"));
    m_navigationLayout = new QHBoxLayout(navigation);
    m_navigationLayout->setContentsMargins(20, 10, 20, 0);
    m_navigationLayout->addStretch();
    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);
    root->addWidget(navigation);
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("DesignScrollMenuArea"));
    m_scroll->viewport()->setObjectName(QStringLiteral("DesignScrollMenuViewport"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scrollContent = new QWidget(m_scroll);
    m_scrollContent->setObjectName(QStringLiteral("DesignScrollMenuContent"));
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(40, 20, 40, 20);
    m_scroll->setWidget(m_scrollContent);
    root->addWidget(m_scroll, 1);
    m_animation = new QPropertyAnimation(m_scroll->verticalScrollBar(), "value", this);
    m_animation->setDuration(500);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this,
            [this] { m_animating = false; });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ModernScrollMenu::updateNavigation);
    for (const Section &section : sections) addSection(section.first, section.second);
    m_contentLayout->addStretch();
}

int ModernScrollMenu::sectionCount() const { return m_sections.size(); }
int ModernScrollMenu::currentSection() const
{
    for (int index = 0; index < m_buttons.size(); ++index)
        if (m_buttons.at(index)->isChecked()) return index;
    return -1;
}

void ModernScrollMenu::addNavigationButton(const QString &title, int index)
{
    auto *button = new QPushButton(title, m_navigationLayout->parentWidget());
    button->setObjectName(QStringLiteral("DesignScrollMenuButton"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setChecked(index == 0);
    m_buttonGroup->addButton(button);
    m_navigationLayout->insertWidget(index, button);
    connect(button, &QPushButton::clicked, this,
            [this, index] { scrollToSection(index); });
    m_buttons.append(button);
}

void ModernScrollMenu::addSection(const QString &title, QWidget *content)
{
    if (content == nullptr) return;
    if (m_contentLayout->count() > 0
        && m_contentLayout->itemAt(m_contentLayout->count() - 1)
               ->spacerItem() != nullptr) {
        delete m_contentLayout->takeAt(m_contentLayout->count() - 1);
    }
    addNavigationButton(title, m_sections.size());
    auto *wrapper = new QWidget(m_scrollContent);
    wrapper->setObjectName(QStringLiteral("DesignScrollMenuSection"));
    auto *outer = new QHBoxLayout(wrapper);
    outer->setContentsMargins(0, 10, 0, 10);
    auto *row = new QWidget(wrapper);
    row->setFixedWidth(1000);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(100);
    auto *label = new DesignLabel(title, row);
    label->setObjectName(QStringLiteral("DesignScrollMenuTitle"));
    label->setFixedWidth(100);
    content->setParent(row);
    content->setProperty("designScrollMenuContent", true);
    content->setFixedWidth(800);
    rowLayout->addWidget(label);
    rowLayout->addWidget(content);
    outer->addStretch(); outer->addWidget(row); outer->addStretch();
    if (!m_sections.isEmpty()) {
        auto *separator = new QFrame(m_scrollContent);
        separator->setObjectName(QStringLiteral("DesignScrollMenuSeparator"));
        separator->setFrameShape(QFrame::NoFrame);
        separator->setFixedHeight(1);
        m_contentLayout->addWidget(separator);
    }
    m_contentLayout->addWidget(wrapper);
    m_sections.append(wrapper);
    m_contentLayout->addStretch();
}

void ModernScrollMenu::scrollToSection(int index)
{
    if (index < 0 || index >= m_sections.size()) return;
    m_buttons.at(index)->setChecked(true);
    m_animating = true;
    m_animation->stop();
    m_animation->setStartValue(m_scroll->verticalScrollBar()->value());
    m_animation->setEndValue(m_sections.at(index)->pos().y());
    m_animation->start();
}

void ModernScrollMenu::updateNavigation(int value)
{
    if (m_animating) return;
    int current = -1;
    for (int index = 0; index < m_sections.size(); ++index) {
        if (m_sections.at(index)->pos().y() <= value + 50) current = index;
        else break;
    }
    if (current >= 0) m_buttons.at(current)->setChecked(true);
}

} // namespace darkeye
