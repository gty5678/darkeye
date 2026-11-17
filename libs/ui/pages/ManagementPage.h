#pragma once

#include "database/repositories/ReferenceRepository.h"
#include "domain/Person.h"
#include "darkeye_ui/base/LazyWidget.h"

#include <QList>
#include <QUrl>

class QTabWidget;

namespace darkeye
{

class ThemeService;
class AddWorkTabPage3;
class WorkBatchStateWidget;
class CrawlerScheduler;
namespace graph { class GraphManager; }

class ManagementPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit ManagementPage(
        QSqlDatabase database, ThemeService &themes, CrawlerScheduler &crawlerScheduler,
        QSqlDatabase privateDatabase = {},
        QString coverDirectory = {},
        QString actressImageDirectory = {},
        QString fanartDirectory = {},
        QUrl imageFetchEndpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")),
        QUrl topActressesEndpoint =
            QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/top-actresses")),
        graph::GraphManager *graphManager = nullptr,
        QWidget *parent = nullptr,
        QString actressPortraitDirectory = {}, QString actorPortraitDirectory = {});

    bool loadWork(qint64 workId);
    /// Refresh an already-instantiated work editor after actor/actress data changes.
    void refreshPersonSelectors();

signals:
    void referencesChanged(ReferenceKind kind);
    void tagsChanged();
    void workChanged(qint64 workId);
    void worksChanged();
    void actressesChanged();
    void actressesCreated(const QList<qint64> &actressIds);
    void workRequested(qint64 workId);
    void actressRequested(qint64 actressId);
    void personDetailRequested(PersonKind kind, qint64 personId);
    void personEditRequested(PersonKind kind, qint64 personId);

private:
    enum TabIndex
    {
        WorkEditorTab,
        TagManagementTab,
        MakerManagementTab,
        LabelManagementTab,
        SeriesManagementTab,
        BatchOperationsTab,
        SummaryQueryTab,
        PersonalRecordManagementTab,
        SoftDeleteTab,
        RecycleBinTab,
    };

    void lazyLoad() override;
    void ensureTabLoaded(int index);

    QSqlDatabase m_database;
    QSqlDatabase m_privateDatabase;
    ThemeService &m_themes;
    QString m_actressPortraitDirectory;
    QString m_actorPortraitDirectory;
    QString m_coverDirectory;
    QString m_actressImageDirectory;
    QString m_fanartDirectory;
    QUrl m_imageFetchEndpoint;
    QUrl m_topActressesEndpoint;
    CrawlerScheduler &m_crawlerScheduler;
    graph::GraphManager *m_graphManager = nullptr;
    QTabWidget *m_tabs = nullptr;
    AddWorkTabPage3 *m_workEditor = nullptr;
    WorkBatchStateWidget *m_softDelete = nullptr;
    WorkBatchStateWidget *m_recycleBin = nullptr;
};

} // namespace darkeye
