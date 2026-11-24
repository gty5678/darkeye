#include "darkeye_ui/components/LazyScrollArea.h"

#include "darkeye_ui/layouts/WaterfallLayout.h"

#include <QEvent>
#include <QLabel>
#include <QScrollBar>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <utility>

namespace darkeye {

LazyScrollArea::LazyScrollArea(int columnWidth, QWidget *parent, int spacing, int verticalSpacing)
    : QScrollArea(parent), m_contentWidget(new QWidget(this)),
      m_waterfallWidget(m_contentWidget),
      m_layout(new WaterfallLayout(m_waterfallWidget, columnWidth, 0, spacing, verticalSpacing))
{
    setObjectName(QStringLiteral("DesignLazyScrollArea"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    m_layout->setContentsMargins(0, 0, 0, 0);
    setWidget(m_contentWidget);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &LazyScrollArea::handleScroll);
}

void LazyScrollArea::setLoader(Loader loader)
{
    m_asyncLoader = {};
    m_loader = std::move(loader);
    reset();
}

void LazyScrollArea::setAsyncLoader(AsyncLoader loader)
{
    m_loader = {};
    m_asyncLoader = std::move(loader);
    if (m_asyncLoader && !m_loadStatus) {
        auto *statusLayout = new QVBoxLayout(viewport());
        statusLayout->setSizeConstraint(QLayout::SetNoConstraint);
        statusLayout->setContentsMargins(0, 0, 0, 12);
        statusLayout->addStretch();
        m_loadStatus = new QLabel(viewport());
        m_loadStatus->setObjectName(QStringLiteral("LazyScrollLoadStatus"));
        m_loadStatus->setTextFormat(Qt::RichText);
        m_loadStatus->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        m_loadStatus->setAutoFillBackground(true);
        m_loadStatus->setMargin(8);
        statusLayout->addWidget(m_loadStatus, 0, Qt::AlignHCenter);
        connect(m_loadStatus, &QLabel::linkActivated, this, [this] { loadNextPage(); });
    }
    reset();
}

void LazyScrollArea::setViewportItemHandler(std::function<void(QWidget *, bool)> handler)
{
    auto previousHandler = std::exchange(m_viewportItemHandler, std::move(handler));
    const auto previousItems = std::exchange(m_activeItems, {});
    if (previousHandler) {
        for (QWidget *widget : previousItems)
            previousHandler(widget, false);
    }
    scheduleViewportUpdate();
}

void LazyScrollArea::reset(ScrollPosition position)
{
    m_scrollRestoreTarget = position == ScrollPosition::Preserve
        ? qMax(m_scrollRestoreTarget, verticalScrollBar()->value()) : 0;
    ++m_generation;
    m_loading = true;
    const auto previousItems = std::exchange(m_activeItems, {});
    if (m_viewportItemHandler) {
        for (QWidget *widget : previousItems)
            m_viewportItemHandler(widget, false);
    }
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            // Match the Python implementation: remove cards from the content
            // widget before scheduling deletion.  deleteLater() alone leaves
            // the old card visible until the next event-loop turn, where it
            // can briefly overlap the newly laid-out page during a refresh.
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    m_currentPage = 0;
    m_reachedEnd = false;
    m_loadFailed = false;
    m_scrollCheckRetries = 0;
    verticalScrollBar()->setValue(0);
    m_layout->invalidate();
    updateContentGeometry();
    if (m_loadStatus) m_loadStatus->hide();
    m_loading = false;
    if (m_loader || m_asyncLoader) {
        loadNextPage();
    }
}

bool LazyScrollArea::loadNextPage()
{
    if ((!m_loader && !m_asyncLoader) || m_loading || m_reachedEnd) {
        return false;
    }

    m_loading = true;
    m_loadFailed = false;
    const quint64 request = ++m_generation;
    if (m_asyncLoader) {
        m_loadStatus->setText(tr("正在加载…"));
        m_loadStatus->show();
        m_loadStatus->raise();
        m_asyncLoader(request, m_currentPage, m_pageSize);
        return true;
    }

    const QList<QWidget *> widgets = m_loader(m_currentPage, m_pageSize);
    for (QWidget *widget : widgets) {
        if (widget != nullptr) {
            m_layout->addWidget(widget);
        }
    }
    finishPage(static_cast<int>(widgets.size()), widgets.size() >= m_pageSize);
    return !widgets.isEmpty();
}

bool LazyScrollArea::isCurrentRequest(quint64 request) const
{
    return m_loading && request == m_generation;
}

void LazyScrollArea::completePage(quint64 request, int itemCount, bool hasMore,
                                  ItemFactory factory)
{
    if (!isCurrentRequest(request)) return;
    appendBatch(request, 0, itemCount, hasMore, std::move(factory));
}

void LazyScrollArea::failPage(quint64 request)
{
    if (!isCurrentRequest(request)) return;
    m_loadFailed = true;
    m_loading = false;
    m_loadStatus->setText(tr("加载失败 · <a href=\"retry\">重试</a>"));
}

void LazyScrollArea::appendBatch(quint64 request, int first, int itemCount, bool hasMore,
                                 ItemFactory factory)
{
    if (!isCurrentRequest(request)) return;

    constexpr int batchSize = 8;
    const int end = qMin(first + batchSize, itemCount);
    for (int i = first; i < end; ++i) {
        if (QWidget *widget = factory(i)) m_layout->addWidget(widget);
    }
    if (end == itemCount) {
        finishPage(itemCount, hasMore);
        return;
    }
    updateContentGeometry();
    QTimer::singleShot(1, this, [this, request, end, itemCount, hasMore,
                                 factory = std::move(factory)]() mutable {
        appendBatch(request, end, itemCount, hasMore, std::move(factory));
    });
}

void LazyScrollArea::finishPage(int itemCount, bool hasMore)
{
    m_reachedEnd = !hasMore;
    if (itemCount > 0) ++m_currentPage;
    if (m_asyncLoader) {
        if (m_currentPage == 0 && m_reachedEnd)
            m_loadStatus->setText(tr("没有匹配结果"));
        else
            m_loadStatus->hide();
    }
    m_layout->invalidate();
    updateContentGeometry();
    m_loading = false;
    scheduleScrollableCheck();
}

void LazyScrollArea::setHeaderWidget(QWidget *widget)
{
    if (m_headerWidget == widget) return;
    if (widget != nullptr) ensureContentLayout();
    if (m_headerWidget != nullptr) {
        m_contentLayout->removeWidget(m_headerWidget);
        m_headerWidget->setParent(nullptr);
    }
    m_headerWidget = widget;
    if (widget != nullptr) {
        widget->setParent(m_contentWidget);
        m_contentLayout->insertWidget(0, widget, 0, Qt::AlignHCenter | Qt::AlignTop);
    }
    m_contentWidget->updateGeometry();
}

QWidget *LazyScrollArea::headerWidget() const { return m_headerWidget; }

int LazyScrollArea::pageSize() const
{
    return m_pageSize;
}

void LazyScrollArea::setPageSize(int pageSize)
{
    if (pageSize > 0) {
        m_pageSize = pageSize;
    }
}

int LazyScrollArea::currentPage() const
{
    return m_currentPage;
}

bool LazyScrollArea::reachedEnd() const
{
    return m_reachedEnd;
}

bool LazyScrollArea::loading() const
{
    return m_loading;
}

int LazyScrollArea::itemCount() const
{
    return m_layout->count();
}

int LazyScrollArea::columnWidth() const
{
    return m_layout->columnWidth();
}

void LazyScrollArea::setColumnWidth(int columnWidth)
{
    if (columnWidth > 0) {
        m_layout->setColumnWidth(columnWidth);
        updateContentGeometry();
    }
}

void LazyScrollArea::setUniformRows(bool enabled)
{
    m_layout->setUniformRows(enabled);
    updateContentGeometry();
}

void LazyScrollArea::setPrefetchDistance(int distance)
{
    m_prefetchDistance = qMax(0, distance);
    scheduleViewportUpdate();
}

void LazyScrollArea::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    updateContentGeometry();
}

bool LazyScrollArea::viewportEvent(QEvent *event)
{
    const bool handled = QScrollArea::viewportEvent(event);
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide
        || event->type() == QEvent::Resize)
        scheduleViewportUpdate();
    return handled;
}

void LazyScrollArea::scheduleViewportUpdate()
{
    if (!m_viewportItemHandler || m_viewportUpdateScheduled) return;
    m_viewportUpdateScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_viewportUpdateScheduled = false;
        updateViewportItems();
    });
}

void LazyScrollArea::updateViewportItems()
{
    if (!m_viewportItemHandler) return;

    QSet<QWidget *> active;
    if (viewport()->isVisible()) {
        m_layout->activate();
        const QPoint origin = m_waterfallWidget->mapFrom(viewport(), QPoint());
        const QRect prefetch = QRect(origin, viewport()->size())
            .adjusted(0, -m_prefetchDistance, 0, m_prefetchDistance);
        for (int index = 0; index < m_layout->count(); ++index) {
            QWidget *widget = m_layout->itemAt(index)->widget();
            if (widget && prefetch.intersects(widget->geometry()))
                active.insert(widget);
        }
    }
    for (QWidget *widget : std::as_const(m_activeItems))
        if (!active.contains(widget)) m_viewportItemHandler(widget, false);
    for (QWidget *widget : std::as_const(active))
        if (!m_activeItems.contains(widget)) m_viewportItemHandler(widget, true);
    m_activeItems = std::move(active);
}

void LazyScrollArea::handleScroll(int value)
{
    scheduleViewportUpdate();
    const QScrollBar *scrollBar = verticalScrollBar();
    if (!m_loadFailed && m_scrollRestoreTarget == 0
        && value >= scrollBar->maximum() - m_prefetchDistance) {
        loadNextPage();
    }
}

void LazyScrollArea::scheduleScrollableCheck()
{
    if (m_reachedEnd && m_scrollRestoreTarget == 0) return;
    const quint64 generation = m_generation;
    // Allow QScrollArea and the waterfall layout to apply the newly appended
    // cards before inspecting the scrollbar range.  Checking in the same
    // event-loop turn observes a stale zero range and can preload every page
    // instead of waiting for an actual scroll.
    QTimer::singleShot(50, this, [this, generation] {
        checkScrollableAndLoad(generation);
    });
}

void LazyScrollArea::checkScrollableAndLoad(quint64 generation)
{
    if (generation != m_generation || m_loading || m_loadFailed) return;

    const int maximum = verticalScrollBar()->maximum();
    if (m_scrollRestoreTarget > 0) {
        if (maximum < m_scrollRestoreTarget && !m_reachedEnd) {
            loadNextPage();
            return;
        }
        verticalScrollBar()->setValue(m_scrollRestoreTarget);
        m_scrollRestoreTarget = 0;
        if (maximum > 0 && !m_reachedEnd)
            handleScroll(verticalScrollBar()->value());
    }
    if (m_reachedEnd || maximum > 0 || m_scrollCheckRetries >= 10) return;

    ++m_scrollCheckRetries;
    loadNextPage();
}

void LazyScrollArea::ensureContentLayout()
{
    if (m_contentLayout != nullptr) return;

    QWidget *waterfall = takeWidget();
    Q_ASSERT(waterfall == m_waterfallWidget);
    m_contentWidget = new QWidget(this);
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    waterfall->setParent(m_contentWidget);
    m_contentLayout->addWidget(waterfall, 0, Qt::AlignTop);
    m_contentLayout->addStretch();
    setWidget(m_contentWidget);
}

void LazyScrollArea::updateContentGeometry()
{
    // QScrollArea does not reliably turn a custom layout's height-for-width
    // result into its child's height.  Without an explicit minimum height the
    // cards can paint below the widget bounds while the scrollbar only covers
    // part of the waterfall.
    const int contentWidth = qMax(1, viewport()->width());
    m_waterfallWidget->setMinimumHeight(m_layout->heightForWidth(contentWidth));
    m_waterfallWidget->updateGeometry();
    m_contentWidget->updateGeometry();
    scheduleViewportUpdate();
}

} // namespace darkeye
