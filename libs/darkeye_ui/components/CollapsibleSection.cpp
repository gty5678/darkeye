#include "darkeye_ui/components/CollapsibleSection.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QBoxLayout>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace darkeye {

TokenCollapsibleSection::TokenCollapsibleSection(const QString &title,
                                                 ThemeService *themes,
                                                 QWidget *parent)
    : QWidget(parent), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignCollapsibleSection"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_toggle = new QToolButton(this);
    m_toggle->setObjectName(QStringLiteral("DesignCollapsibleToggle"));
    setTitle(title);
    m_toggle->setCheckable(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("DesignCollapsibleContent"));
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(10, 10, 10, 10);
    m_content->hide();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_toggle);
    layout->addWidget(m_content);
    connect(m_toggle, &QToolButton::toggled, this,
            &TokenCollapsibleSection::toggleContent);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this,
                [this] { refreshIcon(); });
    }
    refreshIcon();
}

void TokenCollapsibleSection::setTitle(const QString &title) { m_toggle->setText(title); }
bool TokenCollapsibleSection::isExpanded() const { return m_expanded; }
QWidget *TokenCollapsibleSection::contentWidget() const { return m_content; }
QVBoxLayout *TokenCollapsibleSection::contentLayout() const { return m_contentLayout; }
void TokenCollapsibleSection::addWidget(QWidget *widget)
{
    m_contentLayout->addWidget(widget);
}
void TokenCollapsibleSection::addLayout(QBoxLayout *layout)
{
    m_contentLayout->addLayout(layout);
}
void TokenCollapsibleSection::expand() { m_toggle->setChecked(true); }
void TokenCollapsibleSection::collapse() { m_toggle->setChecked(false); }

void TokenCollapsibleSection::toggleContent(bool checked)
{
    m_expanded = checked;
    m_content->setVisible(checked);
    refreshIcon();
    emit toggled(checked);
}

void TokenCollapsibleSection::refreshIcon()
{
    const ThemeTokens tokens = ThemeService::tokens(
        m_themes == nullptr ? ThemeId::Light : m_themes->current(),
        m_themes == nullptr ? QString() : m_themes->customPrimary());
    m_toggle->setIcon(IconProvider::builtIn(
        m_expanded ? QStringLiteral("chevron_down")
                   : QStringLiteral("chevron_right"),
        QSize(16, 16), QColor(tokens.icon)));
}

} // namespace darkeye


