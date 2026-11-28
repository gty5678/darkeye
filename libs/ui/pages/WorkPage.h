#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/repositories/WorkRepository.h"
#include "settings/Settings.h"

#include <QSet>
#include <QSqlDatabase>
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
        QWidget *parent = nullptr, WorkFilter *filter = nullptr);
    ~WorkPage() override;

    void refresh();
    void refreshFromWorkspace(bool tagsChanged, bool referencesChanged);
    void focusSearch();
    [[nodiscard]] QWidget *captureContent();
    [[nodiscard]] QSet<qint64> filteredWorkIds();
    [[nodiscard]] QList<WorkSummary> filteredWorks() const;

signals:
    void detailRequested(qint64 workId);
    void editRequested(qint64 workId);
    void filteredWorksChanged();

private:
    void lazyLoad() override;
    void buildUi();
    void refreshData();
    void toggleCoverSize();
    void loadCardPage(quint64 request, int pageIndex, int pageSize);

    ThemeService &m_themeService;
    QSqlDatabase m_database;
    WorkRepository m_repository;
    QString m_coverDirectory;
    WorkFilter *m_filter;
    std::optional<WorkSearch> m_appliedSearch;
    std::optional<QSet<qint64>> m_filteredWorkIds;
    QFutureWatcherBase *m_pageQuery = nullptr;
    IconButton *m_viewButton = nullptr;
    LazyScrollArea *m_lazyArea = nullptr;
    bool m_largeCoverView = false;
};

} // namespace darkeye
