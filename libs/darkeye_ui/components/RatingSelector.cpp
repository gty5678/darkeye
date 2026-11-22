#include "darkeye_ui/components/RatingSelector.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>

namespace darkeye {

RatingSelector::RatingSelector(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DesignRating"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (int index = 1; index <= 5; ++index) {
        auto *heart = new QLabel(QStringLiteral("🤍"), this);
        heart->setObjectName(QStringLiteral("RatingHeart"));
        heart->setProperty("ratingIndex", index);
        heart->setAlignment(Qt::AlignCenter);
        heart->setFixedSize(40, 40);
        QFont font(QStringLiteral("Segoe UI Emoji"));
        font.setPixelSize(24);
        heart->setFont(font);
        heart->setCursor(Qt::PointingHandCursor);
        heart->installEventFilter(this);
        layout->addWidget(heart);
        m_hearts.append(heart);
    }
}

int RatingSelector::rating() const { return m_rating; }

void RatingSelector::setRating(int rating, bool notify)
{
    const int normalized = qBound(0, rating, 5);
    if (m_rating == normalized) return;
    m_rating = normalized;
    refresh();
    if (notify) emit ratingChanged(m_rating);
}

bool RatingSelector::eventFilter(QObject *watched, QEvent *event)
{
    const int index = watched->property("ratingIndex").toInt();
    switch (event->type()) {
    case QEvent::Enter:
        m_hoverRating = index;
        refresh();
        break;
    case QEvent::Leave:
        m_hoverRating = -1;
        refresh();
        break;
    case QEvent::MouseButtonPress:
        if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            setRating(index, true);
            return true;
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void RatingSelector::refresh()
{
    const int active = m_hoverRating >= 0 ? m_hoverRating : m_rating;
    for (int index = 0; index < m_hearts.size(); ++index) {
        m_hearts.at(index)->setText(index < active ? QStringLiteral("❤️")
                                                  : QStringLiteral("🤍"));
        m_hearts.at(index)->setProperty("active", index < active);
        m_hearts.at(index)->style()->unpolish(m_hearts.at(index));
        m_hearts.at(index)->style()->polish(m_hearts.at(index));
    }
}

} // namespace darkeye
