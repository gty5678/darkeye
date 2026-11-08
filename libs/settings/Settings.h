#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QSize>
#include <QUrl>

namespace darkeye
{

struct AppSettings final
{//软件的基础设置的
    struct Update final
    {
        QString lastAutoCheckWeek;
        // Automatic checks and new-version notifications are opt-in.
        bool automaticCheck = false;
        bool updateNotification = false;
    };

    QString themeId = QStringLiteral("LIGHT");
    QString customPrimary;

    bool firstLaunch = true;
    bool maximized = false;
    QSize windowSize{800, 600};
    QPoint windowPosition{100, 100};

    bool workLargeCoverView = false;
    bool workTagSelectorVisible = true;
    bool shelfTagSelectorVisible = true;

    QString localVideoPlayer;
    QStringList videoPaths;
    Update update;
};

struct CrawlerSettings final
{
    struct WebDav final
    {
        bool enabled = false;
        QString profileName = QStringLiteral("default");
        QUrl baseUrl;
        QString remoteRoot = QStringLiteral("/darkeye");
        int timeoutSeconds = 20;
        bool autoUploadOnBackup = false;
    };

    QUrl workApiBaseUrl{QStringLiteral("http://127.0.0.1:56790/api/v1/work")};
    QUrl actressApiBaseUrl{QStringLiteral("http://127.0.0.1:56790/api/v1/actress")};
    QUrl coverFetchApiUrl{QStringLiteral("http://127.0.0.1:56790/api/v1/image")};
    QUrl topActressesApiUrl{QStringLiteral("http://127.0.0.1:56790/api/v1/top-actresses")};
    QString collectorExecutable;
    bool autoStartCollector = false;
    QStringList unfinishedSerials;
    WebDav webDav;
};

struct LlamaCppSettings final
{
    QString serverExecutable;
    QString modelPath;
    QString host = QStringLiteral("127.0.0.1");
    int port = 8080;
    QString mode = QStringLiteral("cpu");
    int contextSize = 2048;
    int gpuLayers = 99;
    int threads = 6;
    int threadsBatch = 24;
    int batchSize = 512;
    int microBatchSize = 256;
    bool mlock = true;
    bool autoSyncTranslation = true;
    bool autoStart = false;
};

struct TranslationSettings final
{
    QString engine = QStringLiteral("llm");
    QString model;
    QString baseUrl;
    QString apiKey;
    int timeoutSeconds = 12;
    int retries = 2;
    QString fallback = QStringLiteral("empty");
    LlamaCppSettings llama;
};

} // namespace darkeye

namespace darkeye::settings
{

bool ensureDefaults(QString settingsFile, QString *errorMessage = nullptr);
bool ensureDefaults(QString *errorMessage = nullptr);
[[nodiscard]] AppSettings app(QString settingsFile = {});
[[nodiscard]] CrawlerSettings crawler(QString settingsFile = {});
[[nodiscard]] TranslationSettings translation(QString settingsFile = {});
void saveApp(const AppSettings &value, QString settingsFile = {});
void saveCrawler(const CrawlerSettings &value, QString settingsFile = {});
void saveTranslation(const TranslationSettings &value, QString settingsFile = {});

} // namespace darkeye::settings
