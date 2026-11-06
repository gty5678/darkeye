#include "ui/components/WorkCard.h"

#include "ui/components/AsyncImageLabel.h"
#include "ui/components/ClickableLabel.h"
#include "darkeye_ui/components/DesignLabel.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QColor cardColor(int tagId)
{
    switch (tagId) {
    case 1:
        return QColor(QStringLiteral("#80B0F8"));
    case 2:
        return QColor(QStringLiteral("#FFA475"));
    case 3:
        return QColor(QStringLiteral("#FFEB28"));
    default:
        return QColor(Qt::transparent);
    }
}

} // namespace

namespace darkeye {

WorkCard::WorkCard(const WorkSummary &work, const QString &coverDirectory,
                   bool largeCoverView, QWidget *parent, bool deferCoverLoad)
    : QWidget(parent), m_workId(work.id), m_backgroundColor(cardColor(work.highlightTagId)),
      m_largeCoverView(largeCoverView)
{
    setObjectName(QStringLiteral("WorkCard"));
    setProperty("workId", work.id);
    const int cardWidth = largeCoverView ? 250 : 220;
    const int imageWidth = largeCoverView ? 240 : 210;
    const int imageHeight = largeCoverView ? 162 : (work.standard ? 300 : 120);
    setFixedWidth(cardWidth);
    setFocusPolicy(largeCoverView ? Qt::StrongFocus : Qt::NoFocus);
    setAccessibleName(QStringLiteral("%1 %2").arg(work.serialNumber, work.chineseTitle));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, largeCoverView ? 6 : 10, 5,
                               largeCoverView ? 8 : 20);
    layout->setSpacing(largeCoverView ? 4 : 6);
    auto *serial = new ClickableLabel(work.serialNumber, false, this);
    serial->setObjectName(QStringLiteral("WorkCardSerialNumber"));
    serial->setStyleSheet(QStringLiteral(
        "font-size: 16px; font-family: 'Microsoft YaHei'; font-weight: bold;"));
    serial->ensurePolished();
    serial->setAlignment(Qt::AlignCenter);
    if (largeCoverView) {
        serial->setFixedSize(240, 22);
    } else {
        serial->setFixedWidth(210);
    }
    layout->addWidget(serial, 0, Qt::AlignHCenter);

    m_cover = new AsyncImageLabel(this);
    m_cover->setObjectName(QStringLiteral("WorkCardCover"));
    m_cover->setFixedSize(imageWidth, imageHeight);
    m_cover->setPlaceholderText(QStringLiteral("无封面"));
    m_cover->setDeferredLoading(deferCoverLoad);
    connect(m_cover, &AsyncImageLabel::imageLoaded, this,
            [this] { emit coverLoadFinished(); });
    connect(m_cover, &AsyncImageLabel::imageLoadFailed, this,
            [this] { emit coverLoadFinished(); });
    m_cover->setFitMode(largeCoverView ? ImageFitMode::Contain
                                       : (work.standard ? ImageFitMode::RightCover
                                                        : ImageFitMode::Contain));
    QString imagePath = work.imageUrl.trimmed();
    if (!imagePath.isEmpty() && !QFileInfo(imagePath).isAbsolute()) {
        imagePath = QDir(coverDirectory).filePath(imagePath);
    }
    m_cover->setSource(imagePath);
    m_cover->setCursor(Qt::PointingHandCursor);
    m_cover->installEventFilter(this);
    layout->addWidget(m_cover, 0, Qt::AlignCenter);

    QString displayedTitle = work.chineseTitle;
    if (largeCoverView) {
        displayedTitle = displayedTitle.left(40);
    }
    auto *title = new DesignLabel(displayedTitle, this);
    m_title = title;
    title->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-family: 'Microsoft YaHei'; font-weight: bold;"));
    title->ensurePolished();
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    title->setFixedWidth(largeCoverView ? 240 : 210);
    if (largeCoverView) {
        title->setFixedHeight(50);
    } else {
        title->setMaximumHeight(48);
    }
    if (!largeCoverView)
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(title);

    if (largeCoverView) {
        serial->installEventFilter(this);
        title->installEventFilter(this);
    }

    if (largeCoverView) {
        setFixedHeight(248);
    } else {
        const int titleHeight = displayedTitle.isEmpty() ? 0
            : QFontMetrics(title->font())
                  .boundingRect(QRect(0, 0, 210, 10000),
                                Qt::TextWordWrap | Qt::AlignCenter,
                                displayedTitle)
                  .height();
        title->setFixedHeight(titleHeight);
        const int bottomMargin = displayedTitle.isEmpty() ? 33 : 20;
        layout->setContentsMargins(5, 10, 5, bottomMargin);
        setFixedHeight(10 + serial->sizeHint().height() + 6 + imageHeight + 6
                       + titleHeight + bottomMargin);
    }
}

qint64 WorkCard::workId() const noexcept
{
    return m_workId;
}

void WorkCard::setCoverActive(bool active)
{
    m_cover->setDeferredLoading(!active);
}

bool WorkCard::eventFilter(QObject *watched, QEvent *event)
{
    if (m_largeCoverView && event->type() == QEvent::MouseButtonPress) {
        setFocus(Qt::MouseFocusReason);
    }
    if (watched == m_cover
        && (event->type() == QEvent::ContextMenu
            || (event->type() == QEvent::MouseButtonPress
                && static_cast<QMouseEvent *>(event)->button() == Qt::RightButton))) {
        // Right-click releases request editing.  The associated press/context
        // events must stay on the cover instead of reaching controls below it.
        event->accept();
        return true;
    }
    if (watched == m_cover && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            setFocus();
            emit activated(m_workId);
            return true;
        }
        if (mouseEvent->button() == Qt::RightButton) {
            // On Windows the context-menu event follows the release.  Defer
            // navigation exactly as the Python cover widget does, otherwise
            // that event lands on the newly displayed page.
            const qint64 workId = m_workId;
            QTimer::singleShot(0, this, [this, workId] { emit editRequested(workId); });
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WorkCard::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    if (!m_largeCoverView) return;
    for (QWidget *ancestor = parentWidget(); ancestor != nullptr;
         ancestor = ancestor->parentWidget()) {
        if (auto *scrollArea = qobject_cast<QScrollArea *>(ancestor)) {
            scrollArea->ensureWidgetVisible(this, 24, 24);
            break;
        }
    }
}

void WorkCard::keyPressEvent(QKeyEvent *event)
{
    if (m_largeCoverView
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        QWidget *container = parentWidget();
        QLayout *layout = container ? container->layout() : nullptr;
        QList<WorkCard *> cards;
        if (layout != nullptr) {
            for (int index = 0; index < layout->count(); ++index) {
                if (auto *card = qobject_cast<WorkCard *>(layout->itemAt(index)->widget()))
                    cards.append(card);
            }
        }
        const int current = cards.indexOf(this);
        const int next = current + (event->key() == Qt::Key_Left ? -1 : 1);
        if (current >= 0 && next >= 0 && next < cards.size())
            cards.at(next)->setFocus(Qt::TabFocusReason);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Space) {
        emit activated(m_workId);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void WorkCard::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    if (m_largeCoverView) {
        painter.fillRect(rect(), m_backgroundColor);
        painter.end();
        QWidget::paintEvent(event);
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal cut = width() * 0.15;
    QPainterPath path;
    path.moveTo(cut, 0);
    path.lineTo(width() - cut, 0);
    path.lineTo(width(), cut);
    path.lineTo(width(), height() - cut);
    path.lineTo(width() - cut, height());
    path.lineTo(cut, height());
    path.lineTo(0, height() - cut);
    path.lineTo(0, cut);
    path.closeSubpath();
    painter.fillPath(path, m_backgroundColor);
    painter.end();
    QWidget::paintEvent(event);
}

} // namespace darkeye
