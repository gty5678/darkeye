#include "ui/components/PersonCard.h"

#include "darkeye_ui/components/OctImage.h"
#include "ui/components/ClickableLabel.h"

#include <QEvent>
#include <QFont>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace darkeye
{

PersonCard::PersonCard(qint64 personId, const QString &name, const QString &imagePath,
                       const QString &imageDirectory, QWidget *parent, bool deferImage)
    : QWidget(parent), m_personId(personId),
      m_avatar(new OctImage(imagePath, imageDirectory, 150, true, this, deferImage)),
      m_nameLabel(new ClickableLabel(name, false, this))
{
    setObjectName(QStringLiteral("PersonCard"));
    setFixedWidth(150);
    setAccessibleName(name);

    m_avatar->setObjectName(QStringLiteral("PersonCardAvatar"));
    m_nameLabel->setObjectName(QStringLiteral("PersonCardName"));
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setWordWrap(false);
    QFont font = m_nameLabel->font();
    font.setPointSize(12);
    font.setBold(true);
    m_nameLabel->setFont(font);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_avatar);
    layout->addWidget(m_nameLabel, 0, Qt::AlignCenter);

    m_avatar->installEventFilter(this);
}

qint64 PersonCard::personId() const noexcept
{
    return m_personId;
}

QString PersonCard::name() const
{
    return m_nameLabel->text();
}

OctImage *PersonCard::avatar() const
{
    return m_avatar;
}

void PersonCard::updateData(qint64 personId, const QString &name, const QString &imagePath)
{
    m_personId = personId;
    m_nameLabel->setText(name);
    setAccessibleName(name);
    m_avatar->updateImage(imagePath);
}

bool PersonCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_avatar
        && (event->type() == QEvent::ContextMenu
            || (event->type() == QEvent::MouseButtonPress
                && static_cast<QMouseEvent *>(event)->button() == Qt::RightButton)))
    {
        // The edit action is handled on release below.  Consume the matching
        // press and context-menu event so it cannot bubble into a widget behind
        // the avatar and open an unrelated menu.
        event->accept();
        return true;
    }
    if (watched == m_avatar && event->type() == QEvent::MouseButtonRelease)
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!m_avatar->containsAvatarPoint(mouseEvent->position()))
            return true;
        if (mouseEvent->button() == Qt::LeftButton)
        {
            emit activated(m_personId);
            return true;
        }
        if (mouseEvent->button() == Qt::RightButton)
        {
            // Windows sends QContextMenuEvent after the right-button release.
            // Match the Python implementation and navigate on the next event
            // turn, so that event is consumed by the avatar first.
            const qint64 personId = m_personId;
            QTimer::singleShot(0, this, [this, personId] { emit editRequested(personId); });
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace darkeye
