#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/WorkRepository.h"
#include "ui/layouts/myads/WorkspaceWidget.h"

#include <QJsonObject>
#include <QList>
#include <QUrl>
#include <QHash>
#include <optional>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QShowEvent;
class QSpinBox;
class QToolButton;
class QTimer;

namespace darkeye
{

class CrawlerFieldSelector;
class CompleterLineEdit;
class FanartStripWidget;
class ImageDropWidget;
class IdCheckList;
class PersonTransferSelector;
class WorkTagSelector;
class ThemeService;
class WikiTextEdit;
class IconButton;
class DesignButton;
class CrawlerScheduler;
class ImageFetchService;

// 对应 Python 的 AddWorkTabPage3.py。它也作为作品页的
// 共用编辑器使用，因此不再额外维护一套添加作品逻辑。
class AddWorkTabPage3 final : public LazyWidget
{
    Q_OBJECT

public:
    explicit AddWorkTabPage3(
        QSqlDatabase database, ThemeService &themes, CrawlerScheduler &crawlerScheduler,
        QString coverDirectory = {},
        QString fanartDirectory = {},
        QUrl imageFetchEndpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")),
        QWidget *parent = nullptr,
        QString actressPortraitDirectory = {}, QString actorPortraitDirectory = {});
    ~AddWorkTabPage3() override;

    void setWorkCreationEnabled(bool enabled);
    void beginCreate();
    bool loadWork(qint64 workId);
    /// Mirrors Python's asynchronous editor load used by route-based work editing.
    void loadWorkAsync(qint64 workId);
    void refreshReferences(bool preserveSelection = false);
    void refreshAssociations();
    void refreshCompletions();
    [[nodiscard]] QString currentSerialNumber() const;

signals:
    void workSaved(qint64 workId, bool created);
    void peopleChanged(PersonKind kind);
    void tagsChanged();
    void referencesChanged(ReferenceKind kind);
    /// Emitted only for actresses created while applying a GUI crawl result.
    /// The application shell uses this to queue Minnano enrichment.
    void actressesCreated(const QList<qint64> &actressIds);
    void workLinkRequested(qint64 workId);
    void actressLinkRequested(qint64 actressId);
    void personDetailRequested(PersonKind kind, qint64 personId);
    void personEditRequested(PersonKind kind, qint64 personId);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void lazyLoad() override;
    void applyLoadedWork(std::optional<WorkDetails> details, QString errorMessage,
                         quint64 requestSequence);
    void applyWork(const Work &work);
    [[nodiscard]] Work editorWork() const;
    void save();
    void clearEditor();
    void clearEditorExceptSerial();
    void populateReferenceCombo(QComboBox *combo, ReferenceKind kind,
                                std::optional<qint64> selectedId = std::nullopt);
    void loadInitialReferencesAsync();
    void buildDefaultWorkspace();
    void saveWorkspaceLayout();
    void openCurrentWorkDetail();
    void playLocalVideo();
    void checkSerialAvailability();
    void updateLocalVideoButtonStyle();
    void updateEditorActions();
    void updateModifiedFieldHighlights();
    void markEditorChanged();
    QWidget *createManualNavigation();
    void crawlSelectedFields();
    void translateJapanese(const QPlainTextEdit &source, QPlainTextEdit &destination);
    void applyCrawledData(const QString &serialNumber, const QJsonObject &payload,
                          const QSet<QString> &selectedFields, bool withGui);
    void fetchNextCrawledCover();
    void fetchHighQualityCover();
    [[nodiscard]] myads::ContentConfig contentConfig(const QString &slot) const;
    [[nodiscard]] std::optional<myads::ContentConfig>
    createWorkspaceContent(const QJsonObject &descriptor) const;

    QSqlDatabase m_database;
    WorkRepository m_repository;
    ReferenceRepository m_references;
    PersonRepository m_people;
    ThemeService &m_themes;
    QString m_actressPortraitDirectory;
    QString m_actorPortraitDirectory;
    QString m_coverDirectory;
    QString m_fanartDirectory;
    QUrl m_imageFetchEndpoint;
    CrawlerScheduler &m_crawlerScheduler;
    ImageFetchService *m_crawlerImageFetch = nullptr;
    ImageFetchService *m_highQualityCoverFetch = nullptr;
    QString m_crawlSerial;
    QStringList m_crawlCoverUrls;
    int m_crawlCoverIndex = 0;
    bool m_highQualityCoverFetching = false;
    QString m_workspaceLayoutPath;
    QTimer *m_workspaceSaveTimer = nullptr;
    std::optional<Work> m_currentWork;
    bool m_workCreationEnabled = true;
    bool m_associationsLoaded = false;
    bool m_fanartDirty = false;
    bool m_loadingEditor = false;
    bool m_normalizingSerial = false;
    quint64 m_loadRequestSequence = 0;
    quint64 m_referenceLoadSequence = 0;
    QList<qint64> m_loadedActressIds;
    QList<qint64> m_loadedActorIds;
    QList<qint64> m_loadedTagIds;
    std::optional<qint64> m_serialLookupWorkId;
    ImageDropWidget *m_imageDrop = nullptr;
    CompleterLineEdit *m_serialNumber = nullptr;
    QPlainTextEdit *m_chineseTitle = nullptr;
    QPlainTextEdit *m_japaneseTitle = nullptr;
    CompleterLineEdit *m_director = nullptr;
    QLineEdit *m_releaseDate = nullptr;
    QSpinBox *m_runtime = nullptr;
    QString m_imageUrl;
    QString m_videoUrl;
    QComboBox *m_maker = nullptr;
    QComboBox *m_label = nullptr;
    QComboBox *m_series = nullptr;
    WikiTextEdit *m_notes = nullptr;
    QPlainTextEdit *m_chineseStory = nullptr;
    QPlainTextEdit *m_japaneseStory = nullptr;
    PersonTransferSelector *m_actresses = nullptr;
    PersonTransferSelector *m_actors = nullptr;
    WorkTagSelector *m_tags = nullptr;
    FanartStripWidget *m_fanart = nullptr;
    CrawlerFieldSelector *m_crawlerFields = nullptr;
    DesignButton *m_saveButton = nullptr;
    IconButton *m_detailButton = nullptr;
    QToolButton *m_playButton = nullptr;
    IconButton *m_translateTitleButton = nullptr;
    IconButton *m_translateStoryButton = nullptr;
    myads::WorkspaceWidget *m_workspace = nullptr;
    QHash<QString, QWidget *> m_slotWidgets;
};

} // namespace darkeye
