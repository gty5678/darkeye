#include "darkeye_ui/components/Sidebar.h"

#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/theme/IconProvider.h"
#include "darkeye_ui/components/ChamferButton.h"
#include "darkeye_ui/components/LoadingFeedback.h"

#include <QColor>
#include <QDir>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace darkeye {

SidebarMenuButton::SidebarMenuButton(const QString &text, const QString &icon,
                                     int expandedWidth, int collapsedWidth,
                                     ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignSidebarMenuButton"));
    setFixedSize(expandedWidth, 44);
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::PointingHandCursor);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *iconButton = new IconButton(icon, themes, this);
    if (!IconProvider::contains(icon)) iconButton->setIconPath(icon);
    iconButton->setIconPixelSize(20);
    iconButton->setFixedSize(collapsedWidth, 44);
    iconButton->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("DesignSidebarMenuText"));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(iconButton);
    layout->addWidget(label);
    layout->addStretch();
    updateStyle();
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this,
                [this] { updateStyle(); });
    }
}

bool SidebarMenuButton::isSelected() const { return m_selected; }
void SidebarMenuButton::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    setProperty("selected", selected);
    updateStyle();
}

void SidebarMenuButton::updateStyle()
{
    const ThemeTokens tokens = ThemeService::tokens(
        m_themes == nullptr ? ThemeId::Light : m_themes->current(),
        m_themes == nullptr ? QString() : m_themes->customPrimary());
    const QColor background(tokens.background);
    const QColor primary(tokens.primary);
    const qreal ratio = background.lightness() < 128 ? 0.40 : 0.30;
    const QColor selectedBackground(
        static_cast<int>(primary.red() * ratio + background.red() * (1.0 - ratio)),
        static_cast<int>(primary.green() * ratio + background.green() * (1.0 - ratio)),
        static_cast<int>(primary.blue() * ratio + background.blue() * (1.0 - ratio)));
    const QString normalBackground = m_selected ? selectedBackground.name()
                                                  : QStringLiteral("transparent");
    const QString hoverBackground = m_selected ? selectedBackground.name()
                                                 : tokens.pageBackground;
    const QString border = m_selected ? tokens.primary
                                      : QStringLiteral("transparent");
    setStyleSheet(QStringLiteral(
        "QWidget#DesignSidebarMenuButton { background-color: %1; "
        "border-left: 3px solid %2; }"
        "QWidget#DesignSidebarMenuButton:hover { background-color: %3; }"
        "QLabel#DesignSidebarMenuText { color: %4; background-color: transparent; "
        "border: none; font-size: 14px; }"
        "QWidget#DesignSidebarMenuButton:hover QLabel#DesignSidebarMenuText { "
        "color: %4; }")
                      .arg(normalBackground, border, hoverBackground, tokens.text));
    update();
}
void SidebarMenuButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) emit clicked();
    QWidget::mousePressEvent(event);
}

Sidebar::Sidebar(const QList<SidebarMenuDefinition> &menus,
                 ThemeService *themes, QWidget *parent)
    : Sidebar(menus, {}, themes, parent)
{
}

Sidebar::Sidebar(const QList<SidebarMenuDefinition> &menus,
                 const QString &iconsBasePath, ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_iconsBasePath(iconsBasePath), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignSidebar"));
    setMinimumWidth(m_collapsedWidth);
    setMaximumWidth(m_collapsedWidth);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    auto *toggle = createButton(QStringLiteral("隐藏菜单"), QStringLiteral("menu"));
    connect(toggle, &SidebarMenuButton::clicked, this, &Sidebar::toggleMenu);
    m_layout->addWidget(toggle);
    auto *navigation = new QWidget(this);
    navigation->setFixedSize(m_collapsedWidth, 44);
    auto *navigationLayout = new QHBoxLayout(navigation);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(0);
    auto *back = new IconButton(QStringLiteral("arrow_left"), themes, navigation);
    auto *forward = new IconButton(QStringLiteral("arrow_right"), themes, navigation);
    back->setFixedSize(m_collapsedWidth / 2, 44);
    forward->setFixedSize(m_collapsedWidth / 2, 44);
    connect(back, &QPushButton::clicked, this, &Sidebar::backwardClicked);
    connect(forward, &QPushButton::clicked, this, &Sidebar::forwardClicked);
    navigationLayout->addWidget(back);
    navigationLayout->addWidget(forward);
    m_layout->addWidget(navigation);
    for (const auto &menu : menus) {
        SidebarMenuButton *button = createButton(menu.text, menu.icon);
        connect(button, &SidebarMenuButton::clicked, this,
                [this, id = menu.id] { handleMenuClick(id); });
        m_buttons.insert(menu.id, button);
        m_layout->addWidget(button);
    }
    m_layout->addStretch();
    auto *help = createButton(QStringLiteral("帮助"),
                              QStringLiteral("circle_question_mark"));
    auto *settings = createButton(QStringLiteral("设置"), QStringLiteral("settings"));
    connect(help, &SidebarMenuButton::clicked, this,
            [this] { emit itemClicked(QStringLiteral("help")); });
    connect(settings, &SidebarMenuButton::clicked, this, [this] {
        clearSelection();
        emit itemClicked(QStringLiteral("setting"));
    });
    m_layout->addWidget(help);
    m_layout->addWidget(settings);
    if (!menus.isEmpty()) select(menus.first().id);
    m_animation = new QPropertyAnimation(this, "minimumWidth", this);
    m_animation->setDuration(500);
    m_animation->setEasingCurve(QEasingCurve::InOutQuint);
    connect(m_animation, &QPropertyAnimation::valueChanged, this,
            [this](const QVariant &value) { setMaximumWidth(value.toInt()); });
}

QString Sidebar::selectedId() const { return m_selectedId; }
bool Sidebar::isExpanded() const { return m_expanded; }

void Sidebar::clearSelection()
{
    if (m_selectedId.isEmpty()) return;
    if (m_buttons.contains(m_selectedId)) m_buttons.value(m_selectedId)->setSelected(false);
    m_selectedId.clear();
    emit selectionChanged({});
}

void Sidebar::select(const QString &menuId)
{
    if (!m_buttons.contains(menuId) || m_selectedId == menuId) return;
    if (m_buttons.contains(m_selectedId)) m_buttons.value(m_selectedId)->setSelected(false);
    m_selectedId = menuId;
    m_buttons.value(menuId)->setSelected(true);
    emit selectionChanged(menuId);
}

void Sidebar::toggleMenu()
{
    m_animation->stop();
    m_animation->setStartValue(width());
    m_animation->setEndValue(m_expanded ? m_collapsedWidth : m_expandedWidth);
    m_animation->start();
    m_expanded = !m_expanded;
}

SidebarMenuButton *Sidebar::createButton(const QString &text, const QString &icon)
{
    auto *button = new SidebarMenuButton(text, icon, m_expandedWidth, m_collapsedWidth,
                                         m_themes, this);
    if (!IconProvider::contains(icon) && !m_iconsBasePath.isEmpty()) {
        const auto iconButtons = button->findChildren<IconButton *>();
        if (!iconButtons.isEmpty()) {
            iconButtons.first()->setIconPath(QDir(m_iconsBasePath).filePath(icon));
        }
    }
    return button;
}

void Sidebar::handleMenuClick(const QString &menuId)
{
    if (m_selectedId == menuId) {
        clearSelection();
    } else {
        select(menuId);
    }
    emit itemClicked(menuId);
}

Sidebar2::Sidebar2(const QList<SidebarMenuDefinition> &menus,
                   ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_themes(themes)
{
    setObjectName(QStringLiteral("Sidebar2"));
    setFixedWidth(72);
    setAttribute(Qt::WA_StyledBackground);
    setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addStretch();
    m_tooltip = new CalloutTooltip(themes);
    m_tooltipTimer = new QTimer(this);
    m_tooltipTimer->setSingleShot(true);
    connect(m_tooltipTimer, &QTimer::timeout, this, [this] {
        if (m_hoveredButton != nullptr)
            m_tooltip->showFor(m_hoveredButton, m_tooltipTexts.value(m_hoveredButton));
    });
    for (const SidebarMenuDefinition &menu : menus) {
        auto *button = new ChamferButton(menu.text, menu.icon, 22, 40, 0.5,
                                         themes, this);
        button->setToolTip({});
        button->installEventFilter(this);
        connect(button, &QAbstractButton::clicked, this,
                [this, id = menu.id] { handleClick(id); });
        m_buttons.insert(menu.id, button);
        m_tooltipTexts.insert(button, menu.text);
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }
    layout->addStretch();
    if (!menus.isEmpty() && m_buttons.contains(menus.first().id)) {
        m_selectedId = menus.first().id;
        m_buttons.value(m_selectedId)->setSelected(true);
    }
    if (m_themes != nullptr) connect(m_themes, &ThemeService::themeChanged,
                                    this, [this] { update(); });
}

QString Sidebar2::selectedId() const { return m_selectedId; }
void Sidebar2::clearSelection()
{
    if (m_selectedId.isEmpty()) return;
    m_buttons.value(m_selectedId)->setSelected(false);
    m_selectedId.clear();
    emit selectionChanged({});
}
void Sidebar2::select(const QString &menuId)
{
    if (!m_buttons.contains(menuId) || m_selectedId == menuId) return;
    if (m_buttons.contains(m_selectedId)) m_buttons.value(m_selectedId)->setSelected(false);
    m_selectedId = menuId;
    m_buttons.value(menuId)->setSelected(true);
    emit selectionChanged(menuId);
}
void Sidebar2::toggleMenu() {}

bool Sidebar2::eventFilter(QObject *watched, QEvent *event)
{
    if (m_tooltipTexts.contains(watched)) {
        if (event->type() == QEvent::Enter) {
            m_hoveredButton = static_cast<ChamferButton *>(watched);
            m_tooltipTimer->start(300);
        } else if (event->type() == QEvent::Leave) {
            m_tooltipTimer->stop();
            m_hoveredButton = nullptr;
            m_tooltip->hide();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Sidebar2::paintEvent(QPaintEvent *)
{
    const ThemeTokens tokens = ThemeService::tokens(
        m_themes == nullptr ? ThemeId::Light : m_themes->current(),
        m_themes == nullptr ? QString() : m_themes->customPrimary());
    const QRectF area = rect().adjusted(5, 20, -5, -20);
    constexpr qreal cut = 12;
    QPainterPath path;
    path.moveTo(area.left() + cut, area.top());
    path.lineTo(area.right() - cut, area.top());
    path.lineTo(area.right(), area.top() + cut);
    path.lineTo(area.right(), area.bottom() - cut);
    path.lineTo(area.right() - cut, area.bottom());
    path.lineTo(area.left() + cut, area.bottom());
    path.lineTo(area.left(), area.bottom() - cut);
    path.lineTo(area.left(), area.top() + cut);
    path.closeSubpath();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(tokens.background));
    painter.drawPath(path);
}

void Sidebar2::handleClick(const QString &menuId)
{
    if (m_selectedId == menuId) {
        clearSelection();
    } else {
        select(menuId);
    }
    emit itemClicked(menuId);
}

} // namespace darkeye
