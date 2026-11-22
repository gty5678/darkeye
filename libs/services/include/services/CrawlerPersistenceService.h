#pragma once

#include "settings/Settings.h"

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <functional>

class QSqlDatabase;

namespace darkeye
{

class ImageFetchService;
class LlmTranslationService;

class CrawlerPersistenceService final : public QObject
{
    Q_OBJECT

public:
    using SettingsProvider = std::function<TranslationSettings()>;

    explicit CrawlerPersistenceService(QString databasePath, QString coverDirectory,
                                       QUrl imageFetchEndpoint, QObject *parent = nullptr);
    explicit CrawlerPersistenceService(QString databasePath, QString coverDirectory,
                                       QUrl imageFetchEndpoint, SettingsProvider settingsProvider,
                                       QObject *parent = nullptr);
    explicit CrawlerPersistenceService(QString databasePath, QString coverDirectory,
                                       QUrl imageFetchEndpoint, TranslationSettings translationSettings,
                                       QObject *parent = nullptr);

    void persist(const QString &serialNumber, const QJsonObject &payload,
                 const QSet<QString> &selectedFields);

signals:
    /// Cover downloads begin only after the metadata transaction succeeds.
    /// This lets the Inbox distinguish persistence from the final download stage.
    void coverDownloadStarted(const QString &serialNumber, int total);
    void finished(const QString &serialNumber, bool succeeded, const QString &errorMessage);
    void workPersisted(qint64 workId, const QString &serialNumber);
    void worksChanged();
    void actressesChanged();
    void actorsChanged();
    void completenessChanged(const QString &serialNumber, const QMap<QString, bool> &flags);
    /// Newly created actresses need their own Minnano enrichment crawl, as in Python.
    void actressesCreated(const QList<qint64> &actressIds);

private:
    [[nodiscard]] bool persistDatabase(const QString &serialNumber, const QJsonObject &payload,
                                       const QSet<QString> &selectedFields, qint64 *workId,
                                       QString *errorMessage,
                                       QList<qint64> *createdActressIds = nullptr);
    void translateNextField();
    void persistTranslatedPayload();
    void fetchNextCover();

    QString m_databasePath;
    QString m_coverDirectory;
    ImageFetchService *m_imageFetch = nullptr;
    LlmTranslationService *m_translation = nullptr;
    bool m_busy = false;
    QString m_pendingSerial;
    QJsonObject m_pendingPayload;
    QSet<QString> m_pendingFields;
    int m_translationField = 0;
    QString m_activeSerial;
    qint64 m_activeWorkId = 0;
    QStringList m_coverUrls;
    int m_coverIndex = 0;
};

} // namespace darkeye
