#include "ui/layouts/myads/PaneWidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QHash>
#include <QHoverEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QProxyStyle>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QHBoxLayout>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <optional>

namespace darkeye::myads {
namespace {

constexpr int TabBevel = 8;
constexpr int PreferredTabWidth = 168;
constexpr int MinimumTabWidth = 120;

void drawBeveledTab(QPainter *painter, const QRectF &bounds, const QColor &color,
                   qreal detached = 0, const QColor &border = {})
{
    const qreal top = bounds.top();
    const qreal bottom = bounds.bottom();
    const qreal bevel = qMin<qreal>(TabBevel, bounds.width() / 4);
    const qreal shoulderInset = bevel * (1 - detached);
    const qreal left = bounds.left() + shoulderInset;
    const qreal right = bounds.right() - shoulderInset;
    const qreal footInset = bevel * (2 * detached - 1);
    const QPolygonF outline{
        {left + footInset, bottom}, {left, bottom - bevel},
        {left, top + bevel}, {left + bevel, top},
        {right - bevel, top}, {right, top + bevel},
        {right, bottom - bevel}, {right - footInset, bottom},
    };
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(outline);
    if (border.isValid()) {
        painter->setPen(QPen(border, 1));
        painter->drawPolyline(outline);
    }
    painter->restore();
}

QIcon closeIcon(const QColor &color)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(6, 6), QPointF(18, 18));
    painter.drawLine(QPointF(18, 6), QPointF(6, 18));
    return QIcon(pixmap);
}

class DockTabStyle final : public QProxyStyle
{
public:
    DockTabStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

    void setColors(const DockTheme &theme)
    {
        m_strip = theme.tabBarBackground;
        m_selected = theme.tabSelected;
        m_border = theme.border;
    }

    void drawStrip(QPainter *painter, const QRect &area) const
    {
        painter->fillRect(area, m_strip);
        painter->fillRect(area.left(), area.bottom(), area.width(), 1, m_border);
    }

    void setDragPosition(QStyleOptionTab::TabPosition position)
    {
        m_dragPosition = position;
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        // The drag pixmap needs room for the lower bevel on both sides.
        if (metric == PM_TabBarTabOverlap) return TabBevel;
        if (metric == PM_TabBarTabHSpace) return 20;
        if (metric == PM_TabBarTabShiftVertical) return 0;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget) const override
    {
        const QRect bounds = QProxyStyle::subElementRect(element, option, widget);
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (element == SE_TabBarTabText && tab && !tab->text.isEmpty()) {
            const auto position = tab->position == QStyleOptionTab::Moving ? m_dragPosition : tab->position;
            if (position == QStyleOptionTab::Beginning || position == QStyleOptionTab::OnlyOneTab) {
                // The outer shoulder sits one bevel inside the tab's layout rect.
                QRect title = visualRect(tab->direction, tab->rect, bounds);
                title.setLeft(title.left() + qBound(0, title.width(), TabBevel));
                return visualRect(tab->direction, tab->rect, title);
            }
        }
        if (element != SE_TabBarTabRightButton || !tab || tab->rightButtonSize.isEmpty())
            return bounds;

        const QRect title = QProxyStyle::subElementRect(SE_TabBarTabText, tab, widget);
        // Use the same inset for every tab so the button keeps its position
        // when a reorder moves the tab between the middle and end of the bar.
        const int trailingInset = TabBevel / 2;
        QRect logicalButton = visualRect(tab->direction, tab->rect, bounds);
        const QRect logicalTitle = visualRect(tab->direction, tab->rect, title);
        const int titleEnd = logicalTitle.right() + 1;
        const int edge = tab->rect.right() + 1 - trailingInset;
        logicalButton.moveLeft((titleEnd + edge - logicalButton.width()) / 2);
        return visualRect(tab->direction, tab->rect, logicalButton);
    }

    QRectF selectedTabBounds(const QStyleOptionTab &tab) const
    {
        const QRectF bounds(tab.rect);
        const qreal bevel = qMin<qreal>(TabBevel, bounds.width() / 4);
        const bool rtl = tab.direction == Qt::RightToLeft;
        const auto position = tab.position == QStyleOptionTab::Moving ? m_dragPosition : tab.position;
        const bool single = position == QStyleOptionTab::OnlyOneTab;
        const bool first = position == QStyleOptionTab::Beginning;
        const bool last = position == QStyleOptionTab::End;
        // Internal shoulders meet at the tab boundary, with the feet overlapping below.
        const qreal leftOverlap = single || (rtl ? last : first) ? 0 : bevel;
        const qreal rightOverlap = single || (rtl ? first : last) ? 0 : bevel;
        return bounds.adjusted(-leftOverlap, 0, rightOverlap, 0);
    }

    QColor selectedColor() const { return m_selected; }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (element != CE_TabBarTabShape || !tab) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }

        if (tab->state.testFlag(State_Selected)) {
            const QRectF bounds = selectedTabBounds(*tab).adjusted(0, 0.5, 0, 0);
            drawBeveledTab(painter, bounds, m_selected, 0, m_border);
        } else if (tab->position != QStyleOptionTab::Beginning
                   && tab->position != QStyleOptionTab::OnlyOneTab
                   && tab->selectedPosition != QStyleOptionTab::PreviousIsSelected) {
            const int x = tab->direction == Qt::RightToLeft ? tab->rect.right() : tab->rect.left();
            const int inset = tab->rect.height() / 3;
            painter->fillRect(QRect(x, tab->rect.top() + inset, 1,
                                    tab->rect.height() - 2 * inset), m_border);
        }
    }

private:
    QColor m_strip;
    QColor m_selected;
    QColor m_border;
    QStyleOptionTab::TabPosition m_dragPosition = QStyleOptionTab::OnlyOneTab;
};

class DetachedTabPreview final : public QWidget
{
public:
    DetachedTabPreview(QPixmap label, const QRectF &startBounds, const QRect &tabBounds,
                       const QColor &color, const QPoint &hotSpot)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                               Qt::WindowDoesNotAcceptFocus),
          m_label(std::move(label)), m_startBounds(startBounds), m_tabBounds(tabBounds),
          m_color(color), m_hotSpot(hotSpot), m_morph(this)
    {
        setObjectName(QStringLiteral("MyAdsDetachedTabPreview"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        resize(m_label.deviceIndependentSize().toSize());
        m_morph.setDuration(160);
        m_morph.setStartValue(0.0);
        m_morph.setEndValue(1.0);
        m_morph.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_morph, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(&m_followCursor, &QTimer::timeout, this, [this] {
            const QPoint position = QCursor::pos() - m_hotSpot;
            if (pos() != position) move(position);
        });
    }

    void start(const QPoint &cursorPosition)
    {
        move(cursorPosition - m_hotSpot);
        show();
        m_morph.start();
        m_followCursor.start(16);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const qreal progress = m_morph.currentValue().toReal();
        const QPointF topLeft = m_startBounds.topLeft()
            + (m_tabBounds.topLeft() - m_startBounds.topLeft()) * progress;
        const QSizeF size = m_startBounds.size()
            + (m_tabBounds.size() - m_startBounds.size()) * progress;
        drawBeveledTab(&painter, QRectF(topLeft, size), m_color, progress);
        painter.drawPixmap(0, 0, m_label);
    }

private:
    QPixmap m_label;
    QRectF m_startBounds;
    QRectF m_tabBounds;
    QColor m_color;
    QPoint m_hotSpot;
    QVariantAnimation m_morph;
    QTimer m_followCursor;
};

class TabReorderAnimation final : public QWidget
{
public:
    struct Tab
    {
        QString id;
        QPixmap pixmap;
        QPoint start;
        QPoint end;
    };

    TabReorderAnimation(const DockTabStyle &tabStyle, QWidget *parent)
        : QWidget(parent), m_tabStyle(tabStyle)
    {
        setObjectName(QStringLiteral("MyAdsTabReorderAnimation"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_animation.setDuration(200);
        m_animation.setStartValue(0.0);
        m_animation.setEndValue(1.0);
        connect(&m_animation, &QVariantAnimation::valueChanged, this, [this] { update(); });
        hide();
    }

    void start(QList<Tab> tabs, QPixmap draggedTab)
    {
        m_animation.stop();
        m_tabs = std::move(tabs);
        m_draggedTab = std::move(draggedTab);
        setGeometry(parentWidget()->rect());
        raise();
        show();
        m_animation.start();
    }

    void finish()
    {
        m_animation.stop();
        hide();
        m_tabs.clear();
        m_draggedTab = {};
    }

    QHash<QString, QPoint> positions() const
    {
        QHash<QString, QPoint> result;
        for (const auto &tab : m_tabs) result.insert(tab.id, position(tab));
        return result;
    }

    void moveDraggedTab(const QPoint &position)
    {
        m_draggedPosition = position;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        m_tabStyle.drawStrip(&painter, rect());
        for (const auto &tab : m_tabs)
            painter.drawPixmap(position(tab), tab.pixmap);
        painter.drawPixmap(m_draggedPosition, m_draggedTab);
    }

private:
    QPoint position(const Tab &tab) const
    {
        const qreal progress = m_animation.currentValue().toReal();
        return tab.start + (tab.end - tab.start) * m_slideCurve.valueForProgress(progress);
    }

    const DockTabStyle &m_tabStyle;
    QVariantAnimation m_animation;
    QList<Tab> m_tabs;
    QPixmap m_draggedTab;
    QPoint m_draggedPosition;
    const QEasingCurve m_slideCurve{QEasingCurve::OutCubic};
};

class DraggableTabBar final : public QTabBar
{
public:
    DraggableTabBar(PaneWidget *pane, QScrollArea *scrollArea, const DockTheme &theme)
        : QTabBar(scrollArea), m_pane(pane), m_scrollArea(scrollArea), m_tabStyle(new DockTabStyle),
          m_reorderAnimation(new TabReorderAnimation(*m_tabStyle, this))
    {
        m_tabStyle->setParent(this);
        m_tabStyle->setColors(theme);
        setStyle(m_tabStyle);
        setObjectName(QStringLiteral("MyAdsTabBar"));
        setFocusPolicy(Qt::TabFocus);
        setDrawBase(false);
        setTabsClosable(true);
        setExpanding(false);
        setElideMode(Qt::ElideRight);
        setUsesScrollButtons(false);
        setFixedHeight(DockTabBarHeight);
        setAttribute(Qt::WA_Hover);
        connect(this, &QTabBar::currentChanged, this, [this](int index) {
            revealTab(index);
            updateCloseButtons();
        });
    }

    void revealTab(int index)
    {
        if (index < 0 || m_pressedIndex >= 0) return;
        const QRect tab = tabRect(index);
        m_scrollArea->ensureVisible(tab.left() + tab.width() / 2, tab.center().y(),
                                    (tab.width() + 1) / 2, 0);
    }

    void updateCloseButtons()
    {
        int hovered = -1;
        const QWidget *viewport = m_scrollArea->viewport();
        if (m_hoverPosition && viewport->rect().contains(viewport->mapFromGlobal(*m_hoverPosition)))
            hovered = tabAt(mapFromGlobal(*m_hoverPosition));

        for (int i = 0; i < count(); ++i) {
            if (auto *button = qobject_cast<QToolButton *>(tabButton(i, QTabBar::RightSide))) {
                // Keep the button's size reserved so hovering never shifts the tab contents.
                button->setVisible(button->isEnabledTo(this) && (i == currentIndex() || i == hovered));
            }
        }
    }

    void applyTheme(const DockTheme &theme)
    {
        m_reorderAnimation->finish();
        m_tabStyle->setColors(theme);
        update();
    }

    void holdTabWidthsForClose()
    {
        const QWidget *viewport = m_scrollArea->viewport();
        if (m_pane->iconOnly() || !m_heldTabWidths.isEmpty() || count() < 2 ||
            !m_hoverPosition || !viewport->rect().contains(viewport->mapFromGlobal(*m_hoverPosition)))
            return;

        for (int i = 0; i < count(); ++i) {
            if (isTabVisible(i))
                m_heldTabWidths.insert(tabData(i).toString(), tabRect(i).width());
        }
    }

    void forgetTabWidth(const QString &contentId)
    {
        m_heldTabWidths.remove(contentId);
    }

    QSize tabSizeHint(int index) const override
    {
        if (!m_pane->iconOnly())
            return QSize(m_heldTabWidths.value(tabData(index).toString(), PreferredTabWidth), height());
        const QString id = tabData(index).toString();
        if (!m_pane->isContentCloseable(id)) return QSize(30, height());
        return QSize(QTabBar::tabSizeHint(index).width(), height());
    }

protected:
    void initStyleOption(QStyleOptionTab *option, int tabIndex) const override
    {
        QTabBar::initStyleOption(option, tabIndex);
        option->state &= ~QStyle::State_HasFocus;
    }

    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        m_tabStyle->drawStrip(&painter, rect());
        painter.end();
        QTabBar::paintEvent(event);
    }

    bool event(QEvent *event) override
    {
        const bool handled = QTabBar::event(event);
        switch (event->type()) {
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            m_hoverPosition = static_cast<QHoverEvent *>(event)->globalPosition().toPoint();
            updateCloseButtons();
            break;
        case QEvent::HoverLeave:
        case QEvent::Hide:
            m_hoverPosition.reset();
            if (!m_heldTabWidths.isEmpty()) {
                m_heldTabWidths.clear();
                // Refresh QTabBar's cached layout with the normal size hints.
                setElideMode(elideMode());
            }
            updateCloseButtons();
            break;
        case QEvent::Show:
            updateCloseButtons();
            break;
        case QEvent::Move:
            if (m_hoverPosition) updateCloseButtons();
            break;
        default:
            break;
        }
        return handled;
    }

    QSize minimumTabSizeHint(int index) const override
    {
        if (!m_pane->iconOnly())
            return QSize(m_heldTabWidths.value(tabData(index).toString(), MinimumTabWidth), height());
        return QTabBar::minimumTabSizeHint(index);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        auto *scrollBar = m_scrollArea->horizontalScrollBar();
        if (scrollBar->maximum() == 0 || m_pressedIndex >= 0) {
            event->ignore();
            return;
        }

        const QPoint pixels = event->pixelDelta();
        const QPoint angle = event->angleDelta();
        const int delta = pixels.isNull()
            ? (angle.x() != 0 ? angle.x() : angle.y()) * MinimumTabWidth / 120
            : (pixels.x() != 0 ? pixels.x() : pixels.y());
        const int direction = layoutDirection() == Qt::RightToLeft ? -1 : 1;
        scrollBar->setValue(scrollBar->value() - direction * delta);
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_reorderAnimation->finish();
        m_pressPosition = event->position().toPoint();
        m_pressedIndex = tabAt(m_pressPosition);
        m_pressedContentId = m_pressedIndex < 0 ? QString() : tabData(m_pressedIndex).toString();
        if (m_pressedIndex >= 0) {
            QStyleOptionTab tab;
            initStyleOption(&tab, m_pressedIndex);
            m_tabStyle->setDragPosition(tab.position);
            m_pressOffset = m_pressPosition - tab.rect.topLeft();
            m_pressTabCenter = tab.rect.center().x();
        }
        QTabBar::mousePressEvent(event);
        // Some styles select on release, which loses clicks that end just outside the tab.
        if (event->button() == Qt::LeftButton && isTabEnabled(m_pressedIndex) &&
            !qobject_cast<QToolButton *>(childAt(m_pressPosition))) {
            setCurrentIndex(m_pressedIndex);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_reorderAnimation->finish();
        m_pressedIndex = -1;
        m_pressedContentId.clear();
        QTabBar::mouseReleaseEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPoint position = event->position().toPoint();
        if (m_pressedIndex < 0 || !(event->buttons() & Qt::LeftButton) ||
            (!m_reorderAnimation->isVisible() && (position - m_pressPosition).manhattanLength() < 8)) {
            QTabBar::mouseMoveEvent(event);
            return;
        }
        const QString contentId = m_pressedContentId;
        if (contentId.isEmpty()) return;
        int currentIndex = -1;
        for (int i = 0; i < count(); ++i)
            if (tabData(i).toString() == contentId) { currentIndex = i; break; }
        if (currentIndex < 0) return;

        const QRect visibleTabs(mapFromParent(QPoint()), parentWidget()->size());
        if (visibleTabs.contains(position)) {
            const int draggedCenter = m_pressTabCenter + position.x() - m_pressPosition.x();
            const QRect currentTab = tabRect(currentIndex);
            const int centerOffset = currentTab.center().x() - currentTab.left();
            int destination = currentIndex;
            int distance = qAbs(currentTab.center().x() - draggedCenter);
            for (int i = 0; i < count(); ++i) {
                const QRect bounds = tabRect(i);
                if (bounds.isEmpty() || !visibleTabs.contains(bounds.center())) continue;
                // Use the dragged tab's center after insertion, including for unequal widths.
                const int candidateLeft = bounds.left() < currentTab.left()
                    ? bounds.left() : bounds.right() + 1 - currentTab.width();
                const int candidateDistance = qAbs(candidateLeft + centerOffset - draggedCenter);
                if (candidateDistance < distance) {
                    destination = i;
                    distance = candidateDistance;
                }
            }
            updateTabDrag(currentIndex, destination, position.x() - m_pressOffset.x());
            return;
        }

        const int originalIndex = m_pressedIndex;
        const int overlap = style()->pixelMetric(QStyle::PM_TabBarTabOverlap, nullptr, this);
        QStyleOptionTab tab = tabPixmapOption(currentIndex, overlap, true);
        const QPoint hotSpot = m_pressOffset + QPoint(overlap, 0);
        DetachedTabPreview preview(tabPixmap(currentIndex, tab, overlap, QStyle::CE_TabBarTabLabel),
                                   m_tabStyle->selectedTabBounds(tab), tab.rect,
                                   m_tabStyle->selectedColor(), hotSpot);
        // QDrag consumes the real release. Clear the pressed state first.
        const QPoint releasePosition = tabRect(currentIndex).center();
        QMouseEvent release(QEvent::MouseButtonRelease, releasePosition,
                            mapToGlobal(releasePosition), Qt::LeftButton,
                            Qt::NoButton, event->modifiers());
        mouseReleaseEvent(&release);

        QJsonObject payload{{QStringLiteral("pane_id"), m_pane->paneId()},
                            {QStringLiteral("content_id"), contentId}};
        auto *mime = new QMimeData;
        mime->setData(MyAdsTabMimeType, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        // Keep the platform drag cursor while the preview window paints the morph.
        QPixmap transparent(1, 1);
        transparent.fill(Qt::transparent);
        drag->setPixmap(transparent);
        drag->setHotSpot(QPoint(0, 0));
        const QPointer<DraggableTabBar> guard(this);
        preview.start(event->globalPosition().toPoint());
        const Qt::DropAction action = drag->exec(Qt::MoveAction);
        preview.hide();
        if (!guard) return;
        if (action == Qt::IgnoreAction) {
            for (int i = 0; i < count(); ++i) {
                if (tabData(i).toString() == contentId) {
                    moveTab(i, qBound(0, originalIndex, count() - 1));
                    break;
                }
            }
        }
        emit m_pane->dragFinished();
    }

    void tabLayoutChange() override
    {
        if (m_reorderAnimation) m_reorderAnimation->finish();
        QTabBar::tabLayoutChange();
        updateCloseButtons();
    }

private:
    void updateTabDrag(int from, int to, int left)
    {
        const int overlap = style()->pixelMetric(QStyle::PM_TabBarTabOverlap, nullptr, this);
        const QPoint inset(overlap, 0);
        const QPoint draggedPosition(left - overlap, tabRect(from).top());
        if (from == to && m_reorderAnimation->isVisible()) {
            m_reorderAnimation->moveDraggedTab(draggedPosition);
            return;
        }

        auto positions = m_reorderAnimation->positions();
        for (int i = 0; i < count(); ++i) {
            const QString id = tabData(i).toString();
            if (!positions.contains(id)) positions.insert(id, tabRect(i).topLeft() - inset);
        }
        if (from != to) moveTab(from, to);

        QList<TabReorderAnimation::Tab> tabs;
        for (int i = 0; i < count(); ++i) {
            if (i == to) continue;
            const QRect bounds = tabRect(i);
            if (!bounds.isValid()) continue;
            const QString id = tabData(i).toString();
            const QPoint end = bounds.topLeft() - inset;
            const QPoint start = positions.value(id, end);
            if (!rect().intersects(bounds.united(QRect(start + inset, bounds.size())))) continue;
            tabs.append({id, tabPixmap(i, overlap, false), start, end});
        }
        m_reorderAnimation->moveDraggedTab(draggedPosition);
        m_reorderAnimation->start(std::move(tabs), tabPixmap(to, overlap, true));
    }

    QStyleOptionTab tabPixmapOption(int index, int overlap, bool moving) const
    {
        QStyleOptionTab tab;
        initStyleOption(&tab, index);
        if (moving) tab.position = QStyleOptionTab::Moving;
        tab.rect.moveTopLeft(QPoint(overlap, 0));
        return tab;
    }

    QPixmap tabPixmap(int index, int overlap, bool moving)
    {
        return tabPixmap(index, tabPixmapOption(index, overlap, moving), overlap, QStyle::CE_TabBarTab);
    }

    QPixmap tabPixmap(int index, const QStyleOptionTab &tab, int overlap,
                      QStyle::ControlElement element)
    {
        const QSize size = tab.rect.size() + QSize(2 * overlap, 0);
        QPixmap pixmap(size * devicePixelRatioF());
        pixmap.setDevicePixelRatio(devicePixelRatioF());
        pixmap.fill(Qt::transparent);
        QStylePainter painter(&pixmap, this);
        painter.setFont(font());
        painter.drawControl(element, tab);
        if (QWidget *button = tabButton(index, QTabBar::RightSide); button && button->isVisible()) {
            const QRect buttonRect = style()->subElementRect(QStyle::SE_TabBarTabRightButton, &tab, this);
            button->render(&painter, buttonRect.topLeft(), QRegion(), QWidget::DrawChildren);
        }
        painter.end();
        return pixmap;
    }

    PaneWidget *m_pane;
    QScrollArea *m_scrollArea;
    DockTabStyle *m_tabStyle;
    TabReorderAnimation *m_reorderAnimation;
    QHash<QString, int> m_heldTabWidths;
    std::optional<QPoint> m_hoverPosition;
    QPoint m_pressPosition;
    QPoint m_pressOffset;
    int m_pressTabCenter = 0;
    int m_pressedIndex = -1;
    QString m_pressedContentId;
};

bool decodeTab(const QMimeData *mime, QString *paneId, QString *contentId)
{
    if (!mime || !mime->hasFormat(MyAdsTabMimeType)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(mime->data(MyAdsTabMimeType));
    if (!document.isObject()) return false;
    *paneId = document.object().value(QStringLiteral("pane_id")).toString();
    *contentId = document.object().value(QStringLiteral("content_id")).toString();
    return !paneId->isEmpty() && !contentId->isEmpty();
}

} // namespace

PaneWidget::PaneWidget(const QString &paneId, QWidget *parent, bool retainFocusOnTabChange)
    : QWidget(parent), m_paneId(paneId), m_retainFocusOnTabChange(retainFocusOnTabChange)
{
    setProperty("myadsPane", true);
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto *tabScrollArea = new QScrollArea(this);
    tabScrollArea->setFrameShape(QFrame::NoFrame);
    tabScrollArea->setFocusPolicy(Qt::NoFocus);
    tabScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScrollArea->setWidgetResizable(true);
    m_tabBar = new DraggableTabBar(this, tabScrollArea, m_theme);
    tabScrollArea->setWidget(m_tabBar);
    tabScrollArea->setFixedHeight(m_tabBar->height());
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const int index = m_tabBar->tabAt(position);
        if (index < 0) return;
        QMenu *menu = createTabContextMenu(m_tabBar->tabData(index).toString());
        if (!menu) return;
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(m_tabBar->mapToGlobal(position));
    });
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("MyAdsPageStack"));
    m_tabRow = new QHBoxLayout;
    m_tabRow->setContentsMargins(0, 0, 0, 0);
    m_tabRow->setSpacing(0);
    m_tabRow->addWidget(tabScrollArea, 1);
    layout->addLayout(m_tabRow);
    layout->addWidget(m_stack, 1);
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        switchContent(index);
        emit currentContentChanged();
    });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
        const QString id = m_tabBar->tabData(index).toString();
        if (m_closeable.value(id, true)) removeContent(id);
    });
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        QWidget *page = m_stack->widget(from);
        if (!page) return;
        if (page == m_stack->currentWidget()) clearWindowFocus();
        m_stack->removeWidget(page);
        m_stack->insertWidget(to, page);
        switchContent(m_tabBar->currentIndex());
    });
}

QString PaneWidget::paneId() const { return m_paneId; }

int PaneWidget::tabInsertionIndex(const QPoint &globalPosition) const
{
    const QWidget *viewport = m_tabBar->parentWidget();
    if (!viewport->rect().contains(viewport->mapFromGlobal(globalPosition))) return -1;
    const QPoint local = m_tabBar->mapFromGlobal(globalPosition);
    const bool rtl = m_tabBar->layoutDirection() == Qt::RightToLeft;
    for (int i = 0; i < m_tabBar->count(); ++i) {
        const int center = m_tabBar->tabRect(i).center().x();
        if (rtl ? local.x() >= center : local.x() <= center) return i;
    }
    return m_tabBar->count();
}

QRect PaneWidget::tabInsertionRect(int index) const
{
    if (index < 0 || index > m_tabBar->count()) return {};
    const bool rtl = m_tabBar->layoutDirection() == Qt::RightToLeft;
    int x = rtl ? m_tabBar->width() - 1 : 0;
    if (index < m_tabBar->count()) {
        const QRect tab = m_tabBar->tabRect(index);
        x = rtl ? tab.right() + 1 : tab.left();
    } else if (index > 0) {
        const QRect tab = m_tabBar->tabRect(index - 1);
        x = rtl ? tab.left() : tab.right() + 1;
    }
    const QWidget *viewport = m_tabBar->parentWidget();
    x = qBound(1, m_tabBar->mapToParent(QPoint(x, 0)).x(), qMax(1, viewport->width() - 2));
    return QRect(viewport->mapTo(this, QPoint(x - 1, 2)),
                 QSize(3, qMax(1, m_tabBar->height() - 4)));
}

QRect PaneWidget::contentRect() const { return m_stack->geometry(); }

QMenu *PaneWidget::createTabContextMenu(const QString &contentId)
{
    const int index = indexOf(contentId);
    if (index < 0) return nullptr;
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("MyAdsTabContextMenu"));
    const QStringList ids = contentIds();
    const auto addCloseAction = [this, menu](const QString &text, const QString &name,
                                            const QStringList &candidates) {
        QStringList closeableIds;
        for (const QString &id : candidates)
            if (isContentCloseable(id)) closeableIds.append(id);
        QAction *action = menu->addAction(text);
        action->setObjectName(name);
        action->setEnabled(!closeableIds.isEmpty());
        connect(action, &QAction::triggered, this, [this, closeableIds] {
            for (const QString &id : closeableIds)
                if (contentWidget(id) && isContentCloseable(id) && !removeContent(id)) break;
        });
    };
    QStringList others = ids;
    others.removeAll(contentId);
    addCloseAction(tr("关闭其他"), QStringLiteral("closeOtherTabs"), others);
    addCloseAction(tr("关闭右侧"), QStringLiteral("closeTabsToRight"), ids.mid(index + 1));
    emit tabContextMenuCreated(menu, contentId);
    return menu;
}

void PaneWidget::applyTheme(const DockTheme &theme)
{
    m_theme = theme;
    for (int i = 0; i < m_tabBar->count(); ++i) {
        if (auto *button = qobject_cast<QToolButton *>(
                m_tabBar->tabButton(i, QTabBar::RightSide)))
            button->setIcon(closeIcon(theme.closeIcon));
    }
    static_cast<DraggableTabBar *>(m_tabBar)->applyTheme(theme);
    update();
}

bool PaneWidget::iconOnly() const noexcept { return m_iconOnly; }

void PaneWidget::setIconOnly(bool iconOnly)
{
    if (m_iconOnly == iconOnly) return;
    m_iconOnly = iconOnly;
    for (int i = 0; i < m_tabBar->count(); ++i) {
        const QString id = m_tabBar->tabData(i).toString();
        m_tabBar->setTabText(i, iconOnly ? QString() : m_titles.value(id));
    }
    m_tabBar->updateGeometry();
}

int PaneWidget::indexOf(const QString &contentId) const
{
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (m_tabBar->tabData(i).toString() == contentId) return i;
    return -1;
}

void PaneWidget::updateCloseButton(int index)
{
    const QString id = m_tabBar->tabData(index).toString();
    QWidget *oldButton = m_tabBar->tabButton(index, QTabBar::RightSide);
    auto *button = qobject_cast<QToolButton *>(oldButton);
    if (!button || button->objectName() != QStringLiteral("MyAdsCloseButton")) {
        button = new QToolButton(m_tabBar);
        button->setObjectName(QStringLiteral("MyAdsCloseButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(16, 16));
        connect(button, &QToolButton::clicked, this, [this, button] {
            for (int i = 0; i < m_tabBar->count(); ++i) {
                if (m_tabBar->tabButton(i, QTabBar::RightSide) == button) {
                    const QString contentId = m_tabBar->tabData(i).toString();
                    if (m_closeable.value(contentId, true)) removeContent(contentId);
                    return;
                }
            }
        });
        m_tabBar->setTabButton(index, QTabBar::RightSide, button);
        if (oldButton) oldButton->deleteLater();
    }
    const bool closeable = m_closeable.value(id, true);
    button->setIcon(closeIcon(m_theme.closeIcon));
    button->setFixedSize(closeable ? QSize(20, 20) : QSize(0, 0));
    button->setEnabled(closeable);
    static_cast<DraggableTabBar *>(m_tabBar)->updateCloseButtons();
}

bool PaneWidget::addContent(const QString &contentId, const QString &title, QWidget *widget,
                            const QIcon &icon, bool closeable, int index, ClosePolicy closePolicy)
{
    if (contentId.trimmed().isEmpty() || !widget || indexOf(contentId) >= 0) return false;
    const int target = index < 0 || index > m_tabBar->count() ? m_tabBar->count() : index;
    if (m_stack->count() == 0) clearWindowFocus();
    m_stack->insertWidget(target, widget);
    const int inserted = icon.isNull()
        ? m_tabBar->insertTab(target, m_iconOnly ? QString() : title)
        : m_tabBar->insertTab(target, icon, m_iconOnly ? QString() : title);
    m_tabBar->setTabData(inserted, contentId);
    m_tabBar->setTabToolTip(inserted, title);
    m_titles.insert(contentId, title);
    m_closeable.insert(contentId, closeable);
    m_closePolicies.insert(contentId, closePolicy);
    updateCloseButton(inserted);
    const bool willEmitCurrentChanged = m_tabBar->currentIndex() != inserted;
    m_tabBar->setCurrentIndex(inserted);
    // Inserting a tab can extend the scroll range on the next layout pass.
    auto *tabBar = static_cast<DraggableTabBar *>(m_tabBar);
    QTimer::singleShot(0, tabBar, [tabBar, contentId] {
        const int current = tabBar->currentIndex();
        if (tabBar->tabData(current).toString() == contentId)
            tabBar->revealTab(current);
    });
    switchContent(inserted);
    if (!willEmitCurrentChanged) emit currentContentChanged();
    return true;
}

PaneContent PaneWidget::takeContent(const QString &contentId)
{
    PaneContent content;
    const int index = indexOf(contentId);
    if (index < 0) return content;
    content.contentId = contentId;
    content.title = m_titles.value(contentId);
    content.widget = m_stack->widget(index);
    content.icon = m_tabBar->tabIcon(index);
    content.closeable = m_closeable.value(contentId, true);
    content.closePolicy = contentClosePolicy(contentId);
    content.index = index;
    content.currentContentId = currentContentId();
    if (content.widget == m_stack->currentWidget()) clearWindowFocus();
    const QSignalBlocker blocker(m_tabBar);
    auto *tabBar = static_cast<DraggableTabBar *>(m_tabBar);
    tabBar->forgetTabWidth(contentId);
    m_tabBar->removeTab(index);
    m_stack->removeWidget(content.widget);
    m_titles.remove(contentId);
    m_closeable.remove(contentId);
    m_closePolicies.remove(contentId);
    tabBar->updateCloseButtons();
    switchContent(m_tabBar->currentIndex());
    if (content.currentContentId != currentContentId()) emit currentContentChanged();
    return content;
}

bool PaneWidget::restoreContent(const PaneContent &content)
{
    if (!addContent(content.contentId, content.title, content.widget, content.icon,
                    content.closeable, content.index, content.closePolicy)) return false;
    if (!content.currentContentId.isEmpty()) setCurrentContentId(content.currentContentId);
    return true;
}

bool PaneWidget::removeContent(const QString &contentId)
{
    QWidget *page = contentWidget(contentId);
    if (!page || !isContentCloseable(contentId) || m_pendingCloseRequests.contains(contentId))
        return false;

    // Dispatch the event without QWidget::close(): the docking layer owns tab
    // removal, visibility and page lifetime (including WA_DeleteOnClose pages).
    // A modal save prompt can process events, so guard against re-entry and
    // revalidate ownership before touching the tab again.
    const QPointer<PaneWidget> paneGuard(this);
    const QPointer<QWidget> pageGuard(page);
    m_pendingCloseRequests.insert(contentId);
    QCloseEvent request;
    QApplication::sendEvent(page, &request);
    if (!paneGuard) return false;
    m_pendingCloseRequests.remove(contentId);
    if (!request.isAccepted() || !pageGuard || contentWidget(contentId) != pageGuard.data() ||
        !isContentCloseable(contentId)) return false;

    static_cast<DraggableTabBar *>(m_tabBar)->holdTabWidthsForClose();
    PaneContent content = takeContent(contentId);
    if (!content.widget) return false;
    content.widget->hide();
    if (content.closePolicy == ClosePolicy::DeleteLater) {
        // Release heavy pages after the current UI event has completed.
        content.widget->deleteLater();
    } else {
        // Retained pages belong to the caller and survive an empty pane being
        // removed. The caller must reuse or destroy the detached widget.
        content.widget->setParent(nullptr);
    }
    emit contentClosed(contentId);
    if (m_tabBar->count() == 0) emit paneEmpty(this);
    return true;
}

QStringList PaneWidget::contentIds() const
{
    QStringList result;
    for (int i = 0; i < m_tabBar->count(); ++i)
        result.append(m_tabBar->tabData(i).toString());
    return result;
}

int PaneWidget::contentCount() const { return m_tabBar->count(); }
QString PaneWidget::currentContentId() const
{
    return m_tabBar->currentIndex() < 0 ? QString()
        : m_tabBar->tabData(m_tabBar->currentIndex()).toString();
}

bool PaneWidget::setCurrentContentId(const QString &contentId)
{
    const int index = indexOf(contentId);
    if (index < 0) return false;
    clearWindowFocus();
    m_tabBar->setCurrentIndex(index);
    switchContent(index);
    if (isVisible()) emit activationRequested();
    return true;
}

void PaneWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(0, 0, width(), DockTabBarHeight, m_theme.tabBarBackground);
    painter.fillRect(0, DockTabBarHeight - 1, width(), 1, m_theme.border);
}

void PaneWidget::addTabBarButton(QWidget *button, Qt::Edge edge)
{
    if (m_tabRow->indexOf(button) >= 0) return;
    m_tabRow->insertWidget(edge == Qt::LeftEdge ? 0 : m_tabRow->count(), button, 0, Qt::AlignVCenter);
}

void PaneWidget::focusTabBar()
{
    m_tabBar->setFocus(Qt::ShortcutFocusReason);
}

void PaneWidget::clearWindowFocus()
{
    if (!isVisible()) return;
    auto *focused = window()->focusWidget();
    if (!focused) return;
    if (m_retainFocusOnTabChange) {
        if (isAncestorOf(focused)) focusTabBar();
        return;
    }
    focused->clearFocus();
}

void PaneWidget::switchContent(int index)
{
    if (m_stack->currentIndex() == index) return;
    // Clear the outgoing focus before Qt can transfer it to the incoming page.
    clearWindowFocus();
    m_stack->setCurrentIndex(index);
}

QString PaneWidget::contentTitle(const QString &id) const { return m_titles.value(id); }

QWidget *PaneWidget::contentWidget(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? nullptr : m_stack->widget(index);
}
QIcon PaneWidget::contentIcon(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? QIcon() : m_tabBar->tabIcon(index);
}
bool PaneWidget::isContentCloseable(const QString &id) const
{
    return m_closeable.value(id, true);
}

ClosePolicy PaneWidget::contentClosePolicy(const QString &id) const
{
    return m_closePolicies.value(id, ClosePolicy::DeleteLater);
}

void PaneWidget::dragEnterEvent(QDragEnterEvent *event)
{
    QString paneId, contentId;
    if (decodeTab(event->mimeData(), &paneId, &contentId)) event->acceptProposedAction();
}

void PaneWidget::dragMoveEvent(QDragMoveEvent *event)
{
    QString paneId, contentId;
    if (!decodeTab(event->mimeData(), &paneId, &contentId)) return;
    emit dragMoved(this, mapToGlobal(event->position().toPoint()));
    event->acceptProposedAction();
}

void PaneWidget::dropEvent(QDropEvent *event)
{
    QString paneId, contentId;
    if (!decodeTab(event->mimeData(), &paneId, &contentId)) return;
    emit tabDropped(this, mapToGlobal(event->position().toPoint()), paneId, contentId);
    event->acceptProposedAction();
    emit dragFinished();
}

void PaneWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    emit dragFinished();
    QWidget::dragLeaveEvent(event);
}

} // namespace darkeye::myads

