#include "ui/components/ClickableLabel.h"

#include "darkeye_ui/components/ToastNotification.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QSizePolicy>

namespace darkeye {

ClickableLabel::ClickableLabel(const QString &text, bool actressJump, QWidget *parent)
    : DesignLabel(text, parent), m_actressJump(actressJump)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setWordWrap(false);
}

void ClickableLabel::setActressJumpEnabled(bool enabled) noexcept
{
    m_actressJump = enabled;
}

bool ClickableLabel::actressJumpEnabled() const noexcept
{
    return m_actressJump;
}

QSize ClickableLabel::sizeHint() const
{
    const QFontMetrics metrics(font());
    return {metrics.horizontalAdvance(text()), metrics.height()};
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QApplication::clipboard()->setText(text());
        Toast::showSuccess(window(), QStringLiteral("复制成功"), nullptr, 2000);
        emit clicked();
        event->accept();
        return;
    } else if (m_actressJump && event->button() == Qt::RightButton) {
        emit actressJumpRequested(text());
        event->accept();
        return;
    }
    DesignLabel::mouseReleaseEvent(event);
}

} // namespace darkeye
