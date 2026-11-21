#pragma once

#include "settings/Settings.h"
#include "settings/Paths.h"
#include "database/repositories/ReferenceRepository.h"
#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QDialog>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QWidget>

class QActionGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSpinBox;

namespace darkeye
{

class ColorPicker;
class ToggleSwitch;
class MultiplePathManagement;

class AboutSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit AboutSettingsPage(ThemeService &themeService, QWidget *parent = nullptr,
                               settings::Paths paths = {});

private:
    void lazyLoad() override;

    ThemeService &m_themeService;
    settings::Paths m_runtimePaths;
};

class VideoSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit VideoSettingsPage(QSqlDatabase publicDatabase = {},
                               QWidget *parent = nullptr);

signals:
    void worksChanged();
    void quickWorkRequested(const QStringList &serials);

private:
    void lazyLoad() override;
    void savePlayer();
    void savePaths();
    void browsePlayer();
    void scanMissingSerials();
    void synchronizeVideoUrls();
    [[nodiscard]] QStringList configuredPaths() const;
    void showFilesWithoutSerial(const QList<QPair<QString, QString>> &entries);

    QLineEdit *m_player = nullptr;
    MultiplePathManagement *m_paths = nullptr;
    QSqlDatabase m_publicDatabase;
};

class ShortcutSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit ShortcutSettingsPage(const QString &shortcutsFile, const QActionGroup &actions,
                                  QWidget *parent = nullptr);

private:
    void lazyLoad() override;
    bool saveShortcut(const QString &actionId, const QJsonValue &value);

    QString m_shortcutsFile;
    const QActionGroup &m_actions;
    QJsonObject m_userShortcuts;
};

class CommonSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit CommonSettingsPage(ThemeService &themeService,
                                QWidget *parent = nullptr, QString settingsFile = {});

    [[nodiscard]] QComboBox *themeSelector() const;

signals:
    void sidebarAutoCollapseChanged(bool enabled);

private:
    void lazyLoad() override;
    void updatePrimaryPickerState();
    void savePrimaryColor(const QString &color);

    ThemeService &m_themeService;
    QString m_settingsFile;
    QWidget *m_primaryColorRow = nullptr;
    ColorPicker *m_colorPicker = nullptr;
    QComboBox *m_themeSelector = nullptr;
};

class CrawlerSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit CrawlerSettingsPage(ThemeService &themeService, QWidget *parent = nullptr);

private:
    void lazyLoad() override;
    void save();
    void browseCollector();
    void startCollector();
    void testCollector();
    void resetCrawlerUrl(QLineEdit *field, const QUrl &value);
    void setCollectorStatus(const QString &status);

    ThemeService &m_themeService;
    QLineEdit *m_workApi = nullptr;
    QLineEdit *m_actressApi = nullptr;
    QLineEdit *m_coverApi = nullptr;
    QLineEdit *m_topActressesApi = nullptr;
    QLineEdit *m_collectorExecutable = nullptr;
    ToggleSwitch *m_autoStartCollector = nullptr;
    QLabel *m_collectorStatus = nullptr;
};

class NfoSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit NfoSettingsPage(QSqlDatabase publicDatabase, settings::Paths paths,
                             QWidget *parent = nullptr);

signals:
    void referencesChanged(ReferenceKind kind);
    void tagsChanged();
    void actressesChanged();
    void actorsChanged();
    void worksChanged();

private:
    void lazyLoad() override;
    void importFile(bool mdcz);
    void importFolder(bool mdcz, bool useVideoPaths);
    QSqlDatabase m_publicDatabase;
    settings::Paths m_paths;
    bool m_batchImportRunning = false;
};

class TranslationSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit TranslationSettingsPage(ThemeService &themeService, QWidget *parent = nullptr);
    ~TranslationSettingsPage() override;

private:
    void lazyLoad() override;
    void save();
    void updateLlmFields();
    void updateModeFields();
    void browseServerExecutable();
    void browseModel();
    void startLlamaServer();
    void stopLlamaServer();
    void testLlamaServer();
    void testTranslation();
    void updateCommandPreview();
    QStringList llamaArguments() const;
    void setLlamaStatus(const QString &status);
    void appendLlamaLog(const QString &text);
    void applyLlamaPreset();
    void updateLlamaRunButtons();

    ThemeService &m_themeService;
    QComboBox *m_engine = nullptr;
    QLineEdit *m_model = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QComboBox *m_fallback = nullptr;
    QLineEdit *m_serverExecutable = nullptr;
    QLineEdit *m_modelPath = nullptr;
    QLineEdit *m_host = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_llamaPreset = nullptr;
    ToggleSwitch *m_mlock = nullptr;
    ToggleSwitch *m_autoSync = nullptr;
    ToggleSwitch *m_autoStart = nullptr;
    QSpinBox *m_timeout = nullptr;
    QSpinBox *m_retries = nullptr;
    QSpinBox *m_port = nullptr;
    QSpinBox *m_contextSize = nullptr;
    QSpinBox *m_gpuLayers = nullptr;
    QSpinBox *m_threads = nullptr;
    QSpinBox *m_threadsBatch = nullptr;
    QSpinBox *m_batchSize = nullptr;
    QSpinBox *m_microBatchSize = nullptr;
    QPlainTextEdit *m_commandPreview = nullptr;
    QPlainTextEdit *m_llamaLog = nullptr;
    QPlainTextEdit *m_testInput = nullptr;
    QPlainTextEdit *m_testOutput = nullptr;
    QLabel *m_llamaStatus = nullptr;
    QPushButton *m_startLlamaButton = nullptr;
    QPushButton *m_stopLlamaButton = nullptr;
};

struct DatabaseMaintenanceResult;

class DatabaseSettingsPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit DatabaseSettingsPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                                  settings::Paths paths, ThemeService &themeService,
                                  QWidget *parent = nullptr);

private:
    void lazyLoad() override;
    void createPublicSnapshot();
    void restorePublicSnapshot();
    void createSimpleBackup(bool privateDatabase);
    void restoreSimpleBackup(bool privateDatabase);
    void vacuumDatabases();
    void checkImages();
    void rebuildPrivateLinks();
    void saveWebDavSettings();
    void refreshWebDavCredentialStatus();
    void saveWebDavCredentials();
    void clearWebDavCredentials();
    void testWebDavConnection();
    void uploadLatestBackup();
    void listWebDavBackups();
    void restoreWebDavBackup();
    void showResult(const QString &title, const DatabaseMaintenanceResult &result) const;

    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    settings::Paths m_paths;
    ThemeService &m_themeService;
    ToggleSwitch *m_webDavEnabled = nullptr;
    QLineEdit *m_webDavProfile = nullptr;
    QLineEdit *m_webDavBaseUrl = nullptr;
    QLineEdit *m_webDavRemoteRoot = nullptr;
    QSpinBox *m_webDavTimeout = nullptr;
    ToggleSwitch *m_webDavAutoUpload = nullptr;
    QLabel *m_webDavCredentialStatus = nullptr;
};

class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(ThemeService &themeService, const QActionGroup &shortcutActions,
                          QSqlDatabase publicDatabase = {}, QSqlDatabase privateDatabase = {},
                          settings::Paths paths = {},
                          QWidget *parent = nullptr);

    [[nodiscard]] QComboBox *themeSelector() const;

signals:
    void sidebarAutoCollapseChanged(bool enabled);
    void referencesChanged(ReferenceKind kind);
    void tagsChanged();
    void actressesChanged();
    void actorsChanged();
    void worksChanged();
    void quickWorkRequested(const QStringList &serials);

private:
    void paintEvent(QPaintEvent *event) override;

    ThemeService &m_themeService;
    CommonSettingsPage *m_commonPage = nullptr;
};

} // namespace darkeye
