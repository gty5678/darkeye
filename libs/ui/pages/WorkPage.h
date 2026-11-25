#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/repositories/WorkRepository.h"
#include "settings/Settings.h"

#include <QSet>
#include <QSqlDatabase>
#include <QUrl>
#include <optional>

class QFutureWatcherBase;

namespace darkeye
{

class IconButton;
class LazyScrollArea;
class WorkFilter;

class WorkPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit WorkPage(
        QSqlDatabase database, ThemeService &themeService,
        QString coverDirectory = {}, QSqlDatabase privateDatabase = {},
        QString fanartDirectory = {},
        QUrl imageFetchEndpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")),
        QWidget *parent = nullptr, WorkFilter *filter = nullptr);
    ~WorkPage() override;

    void refresh();
    void refreshReferences();
    void refreshTags();
    void refreshFromWorkspace(bool tagsChanged, bool referencesChanged);
    void focusSearch();
    [[nodiscard]] QWidget *captureContent();
    void filterByActor(qint64 actorId);
    void filterByTag(qint64 tagId);
    [[nodiscard]] QSet<qint64> filteredWorkIds();
    [[nodiscard]] QList<WorkSummary> filteredWorks() const;

signals:
    void detailRequested(qint64 workId);
    void editRequested(qint64 workId);
    void worksChanged();
    void filteredWorksChanged();

private:
    void lazyLoad() override;
    void buildUi();
    void refreshData();
    void toggleCoverSize();
    void loadCardPage(quint64 request, int pageIndex, int pageSize);
    void showWorkDetails(qint64 workId);
    void loadWork(qint64 workId);

    ThemeService &m_themeService;
    QSqlDatabase m_database;
    WorkRepository m_repository;
    QString m_coverDirectory;
    QString m_fanartDirectory;
    QUrl m_imageFetchEndpoint;
    WorkFilter *m_filter;
    std::optional<WorkSearch> m_appliedSearch;
    std::optional<QSet<qint64>> m_filteredWorkIds;
    QFutureWatcherBase *m_pageQuery = nullptr;
    IconButton *m_viewButton = nullptr;
    LazyScrollArea *m_lazyArea = nullptr;
    bool m_largeCoverView = false;
};

} // namespace darkeye
