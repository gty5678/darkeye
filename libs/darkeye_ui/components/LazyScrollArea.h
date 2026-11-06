#pragma once

#include <QList>
#include <QScrollArea>
#include <QSet>
#include <functional>

class QWidget;
class QVBoxLayout;
class QResizeEvent;

namespace darkeye {

class WaterfallLayout;

class LazyScrollArea final : public QScrollArea
{
public:
    using Loader = std::function<QList<QWidget *>(int pageIndex, int pageSize)>;

    explicit LazyScrollArea(int columnWidth = 220, QWidget *parent = nullptr);

    void setLoader(Loader loader);
    void setViewportItemHandler(std::function<void(QWidget *, bool)> handler);
    void reset();
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

    QWidget *m_contentWidget = nullptr;
    QWidget *m_waterfallWidget = nullptr;
    QWidget *m_headerWidget = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    WaterfallLayout *m_layout = nullptr;
    Loader m_loader;
    std::function<void(QWidget *, bool)> m_viewportItemHandler;
    QSet<QWidget *> m_activeItems;
    bool m_viewportUpdateScheduled = false;
    int m_pageSize = 70;
    int m_currentPage = 0;
    int m_prefetchDistance = 300;
    int m_scrollCheckRetries = 0;
    quint64 m_generation = 0;
    bool m_reachedEnd = false;
    bool m_loading = false;
};

} // namespace darkeye
