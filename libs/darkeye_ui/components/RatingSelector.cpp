#include "darkeye_ui/components/RatingSelector.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

namespace darkeye {

namespace {

class RatingHeart final : public QLabel
{
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainterPath heart;
        heart.moveTo(12, 21);
        heart.cubicTo(10, 19, 2, 14, 2, 8.5);
        heart.cubicTo(2, 3, 8.5, 1.5, 12, 6.5);
        heart.cubicTo(15.5, 1.5, 22, 3, 22, 8.5);
        heart.cubicTo(22, 14, 14, 19, 12, 21);
        heart.closeSubpath();

        const qreal size = font().pixelSize() > 0 ? font().pixelSize() : 24;
        const QColor color = palette().color(foregroundRole());
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate((width() - size) / 2, (height() - size) / 2);
        painter.scale(size / 24, size / 24);
        painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (property("active").toBool()) {
            painter.setBrush(color);
        }
        painter.drawPath(heart);
    }
};

} // namespace

RatingSelector::RatingSelector(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DesignRating"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (int index = 1; index <= 5; ++index) {
        auto *heart = new RatingHeart(this);
        heart->setObjectName(QStringLiteral("RatingHeart"));
        heart->setProperty("ratingIndex", index);
        heart->setFixedSize(40, 40);
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
        auto *heart = m_hearts.at(index);
        const bool isActive = index < active;
        if (heart->property("active").toBool() == isActive) continue;
        heart->setProperty("active", isActive);
        heart->style()->unpolish(heart);
        heart->style()->polish(heart);
        heart->update();
    }
}

} // namespace darkeye
