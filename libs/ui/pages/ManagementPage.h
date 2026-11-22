#pragma once

#include "database/repositories/ReferenceRepository.h"
#include "domain/Person.h"
#include "darkeye_ui/base/LazyWidget.h"

#include <QList>
#include <QUrl>

namespace darkeye
{

class ThemeService;
class AddWorkTabPage3;
class WorkBatchStateWidget;
class CrawlerScheduler;

class ManagementPage final : public LazyWidget
{
    Q_OBJECT

public:
    enum class Section
    {
        WorkEditor,
        Tags,
        Makers,
        Labels,
        Series,
        SummaryQuery,
        PersonalRecords,
        SoftDelete,
        RecycleBin,
    };

    explicit ManagementPage(
        Section section, QSqlDatabase database, ThemeService &themes, CrawlerScheduler &crawlerScheduler,
        QSqlDatabase privateDatabase = {},
        QString coverDirectory = {},
        QString fanartDirectory = {},
        QUrl imageFetchEndpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")),
        QWidget *parent = nullptr,
        QString actressPortraitDirectory = {}, QString actorPortraitDirectory = {});

    void loadWork(qint64 workId);
    void refreshPersonSelectors();
    void refreshWorkLists();
    void refreshReferences();

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void referencesChanged(ReferenceKind kind);
    void tagsChanged();
    void workChanged(qint64 workId);
    void worksChanged();
    void peopleChanged(PersonKind kind);
    void actressesCreated(const QList<qint64> &actressIds);
    void workRequested(qint64 workId);
    void workEditRequested(qint64 workId);
    void actressRequested(qint64 actressId);
    void personDetailRequested(PersonKind kind, qint64 personId);
    void personEditRequested(PersonKind kind, qint64 personId);

private:
    void lazyLoad() override;
    QWidget *createSection();
    AddWorkTabPage3 *createWorkEditor();

    const Section m_section;
    QSqlDatabase m_database;
    QSqlDatabase m_privateDatabase;
    ThemeService &m_themes;
    QString m_actressPortraitDirectory;
    QString m_actorPortraitDirectory;
    QString m_coverDirectory;
    QString m_fanartDirectory;
    QUrl m_imageFetchEndpoint;
    CrawlerScheduler &m_crawlerScheduler;
    AddWorkTabPage3 *m_workEditor = nullptr;
    WorkBatchStateWidget *m_workState = nullptr;
};

} // namespace darkeye
