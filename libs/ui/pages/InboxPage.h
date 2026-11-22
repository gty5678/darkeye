#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QHash>
#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QStringList>

class QLabel;
class QListWidget;

namespace darkeye
{

class CrawlerScheduler;
class CrawlerPersistenceService;

class InboxPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit InboxPage(CrawlerScheduler &crawlerScheduler,
                       CrawlerPersistenceService &crawlerPersistence,
                       QWidget *parent = nullptr);

private:
    void lazyLoad() override;
    void refresh();
    void rebuildCompletedList();

    CrawlerScheduler &m_crawlerScheduler;
    CrawlerPersistenceService &m_crawlerPersistence;
    QLabel *m_schedulerLabel = nullptr;
    QLabel *m_pendingLabel = nullptr;
    QLabel *m_activeLabel = nullptr;
    QLabel *m_completedLabel = nullptr;
    QListWidget *m_pendingList = nullptr;
    QListWidget *m_activeList = nullptr;
    QListWidget *m_completedList = nullptr;
    QHash<QString, QMap<QString, bool>> m_completeness;
    QSet<QString> m_coverDownloads;
    QStringList m_finishedSerials;
    QHash<QString, bool> m_finishedSuccess;
    QHash<QString, QString> m_finishedErrors;
    QStringList m_pendingFingerprint;
    QStringList m_activeFingerprint;
    QByteArray m_completedFingerprint;
};

} // namespace darkeye
