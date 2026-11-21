#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/components/ToastNotification.h"

#include "darkeye_ui/theme/ThemeService.h"

#include <QCloseEvent>
#include <QEvent>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

namespace darkeye {
namespace {
QList<QPointer<ToastNotification>> activeToasts;
}

ToastNotification *ToastNotification::showMessage(QWidget *anchor, const QString &message,
                                                  Level level, int durationMilliseconds,
                                                  ThemeService *themeService)
{
    auto *toast = new ToastNotification(anchor, message, level,
                                        durationMilliseconds, themeService);
    activeToasts.append(toast);
    toast->show();
    repositionGroup(anchor);
    if (durationMilliseconds > 0) toast->m_timer->start(durationMilliseconds);
    return toast;
}

ToastNotification *ToastNotification::showSuccess(
    QWidget *anchor, const QString &message, ThemeService *themeService,
    int durationMilliseconds)
{
    return showMessage(anchor, message, Level::Success, durationMilliseconds,
                       themeService);
}

ToastNotification *ToastNotification::showWarning(
    QWidget *anchor, const QString &message, ThemeService *themeService,
    int durationMilliseconds)
{
    return showMessage(anchor, message, Level::Warning, durationMilliseconds,
                       themeService);
}

ToastNotification *ToastNotification::showError(
    QWidget *anchor, const QString &message, ThemeService *themeService,
    int durationMilliseconds)
{
    return showMessage(anchor, message, Level::Error, durationMilliseconds,
                       themeService);
}

ToastNotification::ToastNotification(QWidget *anchor, const QString &message, Level level,
                                     int durationMilliseconds, ThemeService *themeService)
    : QWidget(nullptr), m_anchor(anchor), m_themeService(themeService), m_level(level)
{
    Q_UNUSED(durationMilliseconds)
    setObjectName(QStringLiteral("DesignToast"));
    setProperty("level", static_cast<int>(level));
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);
    m_label = new QLabel(message, this);
    m_label->setObjectName(QStringLiteral("DesignToastLabel"));
    m_label->setWordWrap(true);
    layout->addWidget(m_label);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::close);
    if (m_anchor) m_anchor->installEventFilter(this);
    if (m_themeService) {
        connect(m_themeService, &ThemeService::themeChanged, this,
                [this] { update(); });
    }
    adjustSize();
}

ToastNotification::Level ToastNotification::level() const noexcept
{
    return m_level;
}

void ToastNotification::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    ThemeTokens tokens = m_themeService
                             ? ThemeService::tokens(m_themeService->current(),
                                                    m_themeService->customPrimary())
                             : ThemeService::tokens(ThemeId::Light);
    QColor edge(tokens.info);
    if (m_level == Level::Success) edge = QColor(tokens.success);
    if (m_level == Level::Warning) edge = QColor(tokens.warning);
    if (m_level == Level::Error) edge = QColor(tokens.error);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path = chamferPath(rect().adjusted(1, 1, -1, -1), 8);
    painter.fillPath(path, QColor(tokens.background));
    painter.setPen(QPen(edge, 2));
    painter.drawPath(path);
}

bool ToastNotification::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_anchor && (event->type() == QEvent::Resize
                                || event->type() == QEvent::Move)) {
        repositionGroup(m_anchor);
    }
    return QWidget::eventFilter(watched, event);
}

void ToastNotification::closeEvent(QCloseEvent *event)
{
    if (m_anchor) m_anchor->removeEventFilter(this);
    activeToasts.removeAll(this);
    QWidget *anchor = m_anchor;
    QWidget::closeEvent(event);
    repositionGroup(anchor);
}

void ToastNotification::reposition()
{
    if (m_anchor) {
        const QPoint topLeft = m_anchor->mapToGlobal(m_anchor->rect().topLeft());
        move(topLeft.x() + m_anchor->width() - width() - 16,
             topLeft.y() + 16);
        return;
    }
    const QScreen *screen = QApplication::primaryScreen();
    const QRect available = screen != nullptr
        ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    move(available.right() - width() - 16, available.top() + 16);
}

void ToastNotification::repositionGroup(QWidget *anchor)
{
    int yOffset = 0;
    for (const QPointer<ToastNotification> &toast : std::as_const(activeToasts)) {
        if (!toast || toast->m_anchor != anchor) continue;
        toast->reposition();
        toast->move(toast->x(), toast->y() + yOffset);
        yOffset += toast->height() + 8;
    }
}

} // namespace darkeye


