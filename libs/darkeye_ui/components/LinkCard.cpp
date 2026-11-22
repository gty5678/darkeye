#include "darkeye_ui/components/LinkCard.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace darkeye {

TokenLinkCard::TokenLinkCard(const QString &title, const QString &description,
                             const QString &url, ThemeService *themes,
                             QWidget *parent)
    : QFrame(parent), m_url(url), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignLinkCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::PointingHandCursor);
    setToolTip(url);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(400);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(12);
    auto *textLayout = new QVBoxLayout;
    textLayout->setSpacing(4);
    auto *titleLabel = new DesignLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("DesignLinkCardTitle"));
    auto *descriptionLabel = new DesignLabel(description, this);
    descriptionLabel->setObjectName(QStringLiteral("DesignLinkCardDescription"));
    descriptionLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(descriptionLabel);
    layout->addLayout(textLayout, 1);
    m_icon = new DesignLabel({}, this);
    m_icon->setObjectName(QStringLiteral("DesignLinkCardIcon"));
    m_icon->setFixedSize(18, 18);
    layout->addWidget(m_icon, 0, Qt::AlignVCenter);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this,
                [this] { refreshIcon(); });
    }
    refreshIcon();
}

QUrl TokenLinkCard::url() const { return m_url; }
void TokenLinkCard::setUrl(const QUrl &url) { m_url = url; setToolTip(url.toString()); }
void TokenLinkCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) activate();
    QFrame::mousePressEvent(event);
}
void TokenLinkCard::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Space) activate();
    else QFrame::keyPressEvent(event);
}
void TokenLinkCard::activate()
{
    emit activated(m_url);
    QDesktopServices::openUrl(m_url);
}

void TokenLinkCard::refreshIcon()
{
    const ThemeTokens tokens = ThemeService::tokens(
        m_themes == nullptr ? ThemeId::Light : m_themes->current(),
        m_themes == nullptr ? QString() : m_themes->customPrimary());
    m_icon->setPixmap(IconProvider::builtIn(QStringLiteral("link"), QSize(18, 18),
                                            QColor(tokens.icon)).pixmap(18, 18));
}

} // namespace darkeye


