#include "darkeye_ui/components/Avatar.h"

#include "darkeye_ui/theme/ThemeService.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

namespace darkeye {

Avatar::Avatar(const QString &text, const QString &imagePath, int size,
               ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_text(text), m_size(qMax(16, size)),
      m_pixmap(imagePath), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignAvatar"));
    setFixedSize(m_size, m_size);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this, [this] { update(); });
    }
}

void Avatar::setText(const QString &text) { m_text = text; update(); }
void Avatar::setImagePath(const QString &path) { m_pixmap.load(path); update(); }

QString Avatar::initials() const
{
    const QStringList parts = m_text.simplified().split(QLatin1Char(' '),
                                                        Qt::SkipEmptyParts);
    if (parts.isEmpty()) return QStringLiteral("?");
    if (parts.size() == 1) return parts.first().left(2).toUpper();
    return (parts.first().left(1) + parts.last().left(1)).toUpper();
}

void Avatar::paintEvent(QPaintEvent *)
{
    const ThemeTokens tokens = ThemeService::tokens(
        m_themes == nullptr ? ThemeId::Light : m_themes->current(),
        m_themes == nullptr ? QString() : m_themes->customPrimary());
    QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath clip;
    clip.addEllipse(area);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipPath(clip);
    QColor background(tokens.primary);
    int hue = background.hslHue();
    int shift = -18;
    for (const QChar character : m_text) shift += character.unicode();
    background.setHsl(hue < 0 ? 200 : (hue + shift % 36 + 360) % 360,
                      qMax(40, background.hslSaturation()),
                      background.lightness(), background.alpha());
    painter.fillPath(clip, background);
    if (!m_pixmap.isNull()) {
        const QPixmap scaled = m_pixmap.scaled(area.size().toSize(),
                                               Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation);
        painter.drawPixmap(area.toRect(), scaled,
                           QRect((scaled.width() - area.width()) / 2,
                                 (scaled.height() - area.height()) / 2,
                                 area.width(), area.height()));
    } else {
        painter.setPen(QColor(tokens.textInverse));
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSize(qMax(8, qRound(m_size * 0.35)));
        painter.setFont(font);
        painter.drawText(area, Qt::AlignCenter, initials());
    }
    painter.setClipping(false);
    painter.setPen(QColor(tokens.pageBackground));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(area);
}

AvatarGroup::AvatarGroup(const QStringList &avatars, int avatarSize, int overlap,
                         int maxVisible, ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_avatarSize(qMax(16, avatarSize)),
      m_maxVisible(qMax(1, maxVisible)), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignAvatarGroup"));
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(-qMax(0, overlap));
    setAvatars(avatars);
}

void AvatarGroup::setAvatars(const QStringList &avatars)
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QStringList shown = avatars.mid(0, m_maxVisible);
    const int hidden = qMax(0, avatars.size() - m_maxVisible);
    if (hidden > 0 && !shown.isEmpty()) shown.last() = QStringLiteral("+%1").arg(hidden);
    for (const QString &text : shown) {
        m_layout->addWidget(new Avatar(text, {}, m_avatarSize, m_themes, this));
    }
    m_layout->addStretch();
}

} // namespace darkeye


