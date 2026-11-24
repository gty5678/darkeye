#pragma once

#include <QList>
#include <QScrollArea>
#include <QSet>
#include <functional>

class QWidget;
class QLabel;
class QVBoxLayout;
class QResizeEvent;

namespace darkeye {

class WaterfallLayout;

class LazyScrollArea final : public QScrollArea
{
public:
    using Loader = std::function<QList<QWidget *>(int pageIndex, int pageSize)>;
    using AsyncLoader = std::function<void(quint64 request, int pageIndex, int pageSize)>;
    using ItemFactory = std::function<QWidget *(int index)>;
    enum class ScrollPosition { Top, Preserve };

    explicit LazyScrollArea(int columnWidth = 220, QWidget *parent = nullptr, int spacing = 10,
                            int verticalSpacing = -1);

    void setLoader(Loader loader);
    // Finish an async request on the UI thread. The factory runs there in batches.
    void setAsyncLoader(AsyncLoader loader);
    bool isCurrentRequest(quint64 request) const;
    void completePage(quint64 request, int itemCount, bool hasMore, ItemFactory factory);
    void failPage(quint64 request);
    void setViewportItemHandler(std::function<void(QWidget *, bool)> handler);
    void reset(ScrollPosition position = ScrollPosition::Top);
    // With an async loader, true means the request was started.
    bool loadNextPage();
    void setHeaderWidget(QWidget *widget);
    QWidget *headerWidget() const;

    int pageSize() const;
    void setPageSize(int pageSize);
    int currentPage() const;
    bool reachedEnd() const;
    bool loading() const;
    int itemCount() const;

    int columnWidth() const;
    void setColumnWidth(int columnWidth);
    void setUniformRows(bool enabled);
    void setPrefetchDistance(int distance);

protected:
    bool viewportEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void scheduleViewportUpdate();
    void updateViewportItems();
    void handleScroll(int value);
    void scheduleScrollableCheck();
    void checkScrollableAndLoad(quint64 generation);
    void ensureContentLayout();
    void updateContentGeometry();
    void appendBatch(quint64 request, int first, int itemCount, bool hasMore,
                     ItemFactory factory);
    void finishPage(int itemCount, bool hasMore);

    QWidget *m_contentWidget = nullptr;
    QWidget *m_waterfallWidget = nullptr;
    QWidget *m_headerWidget = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    WaterfallLayout *m_layout = nullptr;
    Loader m_loader;
    AsyncLoader m_asyncLoader;
    QLabel *m_loadStatus = nullptr;
    std::function<void(QWidget *, bool)> m_viewportItemHandler;
    QSet<QWidget *> m_activeItems;
    bool m_viewportUpdateScheduled = false;
    int m_pageSize = 70;
    int m_currentPage = 0;
    int m_prefetchDistance = 300;
    int m_scrollCheckRetries = 0;
    int m_scrollRestoreTarget = 0;
    quint64 m_generation = 0;
    bool m_reachedEnd = false;
    bool m_loading = false;
    bool m_loadFailed = false;
};

} // namespace darkeye
