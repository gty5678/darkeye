#include "ui/pages/ModifyActressPage.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "services/ActressSyncService.h"
#include "services/ImageFetchService.h"
#include "services/LlmTranslationService.h"
#include "settings/Settings.h"
#include "ui/components/ActressNavPage.h"

#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include "settings/Paths.h"
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace darkeye
{

ModifyActressPage::ModifyActressPage(QSqlDatabase database, ThemeService &themes,
                                     QString imageDirectory, QWidget *parent,
                                     QString coverDirectory, QString navConfigFile)
    : PersonEditorPage(std::move(database), themes, std::move(imageDirectory), parent,
                       std::move(coverDirectory))
{
    setObjectName(QStringLiteral("ModifyActressPage"));
    configureAvatar(QStringLiteral("女优头像"), QStringLiteral("把女优头像拖进来"));
    m_sync = new ActressSyncService(database, settings::crawler().actressApiBaseUrl, {}, {}, this);
    m_translation = new LlmTranslationService(this);
    m_avatarFetch = new ImageFetchService(settings::crawler().coverFetchApiUrl, this);
    connect(m_sync, &ActressSyncService::finished, this,
            [this, &themes](const ActressSyncResult &result)
    {
        if (!result.succeeded)
        {
            Toast::showError(window(), QStringLiteral("女优采集失败：%1").arg(result.errorMessage),
                             &themes);
            return;
        }
        QString errorMessage;
        if (!applyCapture(result.capture, &errorMessage))
        {
            Toast::showWarning(window(), errorMessage, &themes);
            return;
        }
        Toast::showSuccess(window(), QStringLiteral("已填入采集资料，请核对后提交修改"), &themes);
    });
    connect(m_avatarFetch, &ImageFetchService::requestFinished, this,
            [this, &themes](quint64 requestId, bool succeeded, const QString &destination,
                            const QString &errorMessage)
    {
        if (requestId != m_avatarFetchRequestId)
            return;
        m_avatarFetchRequestId = 0;
        const bool belongsToCurrentPerson = m_avatarFetchPersonId == personId();
        m_avatarFetchPersonId = 0;
        if (!belongsToCurrentPerson)
            return;
        if (!succeeded)
        {
            Toast::showWarning(window(), QStringLiteral("女优头像下载失败：%1").arg(errorMessage),
                               &themes);
            return;
        }
        setAvatarImagePath(destination);
        Toast::showSuccess(window(), QStringLiteral("已下载头像，请核对后提交修改"), &themes);
    });

    // Python 的女演员编辑页将外部资料置于自由记录上方的独立窗格。
    auto *links = new QWidget(this);
    auto *linksLayout = new QVBoxLayout(links);
    linksLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(links);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    m_navPage = new ActressNavPage(std::move(navConfigFile), content);
    contentLayout->addWidget(m_navPage);
    contentLayout->addStretch();
    scroll->setWidget(content);
    linksLayout->addWidget(scroll);
    addActressExternalLinksPanel(links);

    // Keep the operation pane in the exact order and roles of Python's
    // ModifyActressPage.  The crawler action is deliberately not an external
    // link: it fills this form and still requires an explicit submit.
    setCancelButtonVisible(false);
    auto *crawlerUpdate = new DesignButton(QStringLiteral("爬虫尝试更新(需手动提交)"), this);
    crawlerUpdate->setProperty("testId", QStringLiteral("ActressCrawlerUpdateButton"));
    crawlerUpdate->setToolTip(QStringLiteral("从 Minnano AV 采集资料并填入当前表单；需要手动提交"));
    auto *translateMissing = new DesignButton(QStringLiteral("补全中文名(翻译)"), this);
    translateMissing->setProperty("testId", QStringLiteral("ActressTranslateMissingButton"));
    auto *translateOverwrite = new DesignButton(QStringLiteral("覆盖翻译中文名"), this);
    translateOverwrite->setProperty("testId", QStringLiteral("ActressTranslateOverwriteButton"));
    auto *openMinnano = new DesignButton(QStringLiteral("跳转手动选择(需手动提交)"), this);
    openMinnano->setProperty("testId", QStringLiteral("ActressManualMinnanoButton"));
    openMinnano->setToolTip(QStringLiteral("当搜索结果有多名女优时，打开 Minnano 手动确认"));
    auto *smallActions = new QWidget(this);
    auto *smallActionsLayout = new QHBoxLayout(smallActions);
    smallActionsLayout->setContentsMargins(0, 0, 0, 0);
    auto *show = new IconButton(QStringLiteral("eye"), &themes, smallActions);
    show->setToolTip(QStringLiteral("查看女优详情"));
    show->setProperty("testId", QStringLiteral("ActressShowButton"));
    auto *remove = new IconButton(QStringLiteral("trash_2"), &themes, smallActions);
    remove->setToolTip(QStringLiteral("删除女优"));
    remove->setProperty("testId", QStringLiteral("ActressDeleteButton"));
    smallActionsLayout->addWidget(show);
    smallActionsLayout->addWidget(remove);
    smallActionsLayout->addStretch();
    addActionButton(crawlerUpdate);
    addActionButton(translateMissing);
    addActionButton(translateOverwrite);
    addActionButton(openMinnano);
    addActionButton(smallActions);
    m_translateMissingButton = translateMissing;
    m_translateOverwriteButton = translateOverwrite;
    connect(crawlerUpdate, &QPushButton::clicked, this, [this, &themes] {
        if (!m_sync->fetchCapture(personId()))
            Toast::showWarning(window(), QStringLiteral("无法开始女优采集：请确认日文名或稍后重试"),
                               &themes);
    });
    connect(translateMissing, &QPushButton::clicked, this, [this] { translateChineseNames(false); });
    connect(translateOverwrite, &QPushButton::clicked, this, [this] { translateChineseNames(true); });
    connect(openMinnano, &QPushButton::clicked, this, [this] {
        const QString name = primaryJapaneseName();
        if (!name.isEmpty())
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.minnano-av.com/search_result.php?search_scope=actress&search_word=%1&search=+Go")
                                                .arg(QString::fromUtf8(QUrl::toPercentEncoding(name)))));
    });
    connect(show, &QPushButton::clicked, this, [this] {
        if (personId() > 0) emit personViewRequested(PersonKind::Actress, personId());
    });
    connect(remove, &QPushButton::clicked, this, [this, &themes] {
        if (personId() <= 0 || QMessageBox::question(this, QStringLiteral("删除女优"),
            QStringLiteral("确定删除当前女优？存在关联作品时无法删除。")) != QMessageBox::Yes)
            return;
        QString errorMessage;
        const qint64 deletedId = personId();
        if (!deleteCurrentPerson(&errorMessage))
        {
            Toast::showWarning(window(), errorMessage, &themes);
            return;
        }
        emit personDeleted(PersonKind::Actress, deletedId);
        Toast::showSuccess(window(), QStringLiteral("女优已删除"), &themes);
    });
    connect(this, &PersonEditorPage::namesChanged, this, [this] {
        m_navPage->setNames(primaryJapaneseName(), primaryChineseName());
    });
    connect(m_translation, &LlmTranslationService::translationFinished, this,
            [this, &themes](quint64 requestId, const QString &translation, const QString &error) {
                // A reply from an earlier edit session must never mutate the newly opened actress.
                if (requestId != m_translationRequestId)
                    return;
                m_translationRequestId = 0;
                if (!error.isEmpty())
                    Toast::showWarning(window(), QStringLiteral("中文名翻译失败：%1").arg(error), &themes);
                else if (m_translationCursor < m_translationRows.size() && !translation.trimmed().isEmpty())
                {
                    m_nameTranslations[m_translationRows.at(m_translationCursor)] = translation;
                    ++m_successfulNameTranslations;
                }
                ++m_translationCursor;
                translateNextName();
            });
}

bool ModifyActressPage::loadActress(qint64 actressId)
{
    // Replies cannot be cancelled reliably once sent.  Detach any prior run before the form
    // changes identity so a late reply is ignored instead of being written into this actress.
    m_translationRequestId = 0;
    m_translationRows.clear();
    m_translationCursor = 0;
    if (m_translateMissingButton != nullptr && m_translateOverwriteButton != nullptr)
    {
        m_translateMissingButton->setEnabled(true);
        m_translateOverwriteButton->setEnabled(true);
        m_translateMissingButton->setText(QStringLiteral("补全中文名(翻译)"));
        m_translateOverwriteButton->setText(QStringLiteral("覆盖翻译中文名"));
    }
    const bool loaded = loadPerson(PersonKind::Actress, actressId);
    if (loaded)
        m_navPage->setNames(primaryJapaneseName(), primaryChineseName());
    return loaded;
}

bool ModifyActressPage::applyCapture(const QJsonObject &payload, QString *errorMessage)
{
    if (personId() <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有正在编辑的女优");
        return false;
    }
    const QJsonObject context = payload.value(QStringLiteral("context")).toObject();
    const qint64 contextualId =
        context.value(QStringLiteral("actress_id")).toVariant().toLongLong();
    if (contextualId > 0 && contextualId != personId())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("采集上下文与当前编辑女优不一致，已忽略");
        return false;
    }
    const QJsonObject payloadData = payload.value(QStringLiteral("data")).toObject();
    const QJsonObject capture = payloadData.isEmpty() ? payload : payloadData;
    if (capture.isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("采集结果为空");
        return false;
    }
    applyActressCaptureFields(capture);
    downloadCapturedAvatar(capture);
    m_navPage->setNames(primaryJapaneseName(), primaryChineseName());
    return true;
}

void ModifyActressPage::downloadCapturedAvatar(const QJsonObject &capture)
{
    const QUrl source(capture.value(QStringLiteral("头像地址")).toString().trimmed());
    if (!source.isValid() || source.scheme().isEmpty())
        return;
    if (m_avatarFetchRequestId != 0)
        m_avatarFetch->cancel(m_avatarFetchRequestId);
    const QString temporaryRoot = settings::Paths().temporaryDirectory();
    const QString destination = QDir(temporaryRoot).filePath(
        QStringLiteral("darkeye/minnano_%1_%2.jpg").arg(personId()).arg(QUuid::createUuid().toString(QUuid::Id128)));
    m_avatarFetchRequestId = m_avatarFetch->fetchToJpeg(source, destination);
    m_avatarFetchPersonId = personId();
}

void ModifyActressPage::translateChineseNames(bool overwrite)
{
    if (m_translationRequestId != 0 || m_translationCursor < m_translationRows.size())
        return;
    const QList<QString> japanese = japaneseNames();
    const QList<QString> chinese = chineseNames();
    m_nameTranslations = QList<QString>(japanese.size());
    m_translationRows.clear();
    m_translationOverwrite = overwrite;
    m_successfulNameTranslations = 0;
    for (int row = 0; row < japanese.size(); ++row)
    {
        if (!japanese.at(row).isEmpty() && (overwrite || chinese.at(row).isEmpty()))
            m_translationRows.append(row);
    }
    if (m_translationRows.isEmpty())
    {
        Toast::showMessage(window(), QStringLiteral("没有需要翻译的中文名"));
        return;
    }
    m_translationCursor = 0;
    m_translateMissingButton->setEnabled(false);
    m_translateOverwriteButton->setEnabled(false);
    m_translateMissingButton->setText(QStringLiteral("中文名翻译中…"));
    m_translateOverwriteButton->setText(QStringLiteral("覆盖翻译中…"));
    translateNextName();
}

void ModifyActressPage::translateNextName()
{
    if (m_translationCursor < m_translationRows.size())
    {
        const QList<QString> japanese = japaneseNames();
        m_translationRequestId =
            m_translation->translate(japanese.at(m_translationRows.at(m_translationCursor)),
                                     QStringLiteral("zh-CN"), QStringLiteral("actress_name"));
        return;
    }
    if (!m_translationRows.isEmpty())
    {
        replaceChineseNames(m_nameTranslations, m_translationOverwrite);
        m_navPage->setNames(primaryJapaneseName(), primaryChineseName());
        m_translationRows.clear();
        m_translateMissingButton->setEnabled(true);
        m_translateOverwriteButton->setEnabled(true);
        m_translateMissingButton->setText(QStringLiteral("补全中文名(翻译)"));
        m_translateOverwriteButton->setText(QStringLiteral("覆盖翻译中文名"));
        Toast::showSuccess(window(),
                           QStringLiteral("中文名翻译完成：已回填 %1 条，请核对后提交修改")
                               .arg(m_successfulNameTranslations),
                           nullptr);
    }
}

} // namespace darkeye
