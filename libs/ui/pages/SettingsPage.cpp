#include "ui/pages/SettingsPage.h"
#include "ui/pages/NfoImport.h"

#include "darkeye_ui/components/AnimatedIndicators.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/LinkCard.h"
#include "darkeye_ui/components/ModernScrollMenu.h"
#include "darkeye_ui/components/TokenControls.h"
#include "ui/components/PathManagement.h"
#include "darkeye_ui/components/TokenViews.h"
#include "services/VideoLibraryService.h"
#include "services/LlmTranslationService.h"
#include "services/LlamaRuntime.h"
#include "services/UpdateService.h"
#include "database/DatabaseMaintenanceService.h"
#include "database/WebDavBackupService.h"
#include "database/WebDavCredentialStore.h"
#include "database/SqliteConnection.h"
#include "database/SchemaManager.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/WorkRepository.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QUrl>
#include <QProgressDialog>
#include <QProcess>
#include <QPointer>
#include <QImageReader>
#include <QEventLoop>
#include <QThreadPool>
#include <QSettings>
#include <QTimer>
#include <QXmlStreamReader>

#include <tuple>
#include <utility>
#include <algorithm>
#include <atomic>
#include <memory>

namespace darkeye
{
namespace
{

QString detectedDatabaseVersion(const QString &path)
{
    SqliteConnection connection;
    QString error;
    if (!connection.open(path, true, &error)) {
        return QStringLiteral("未检测到");
    }
    const QString version = connection.schemaVersion(&error);
    if (!error.isEmpty()) {
        return QStringLiteral("未检测到");
    }
    return version.isEmpty() ? QStringLiteral("无版本") : version;
}

QString latestManifestUrl()
{
    const QString configPath = QDir(settings::Paths().configDirectory())
                                   .filePath(QStringLiteral("update.ini"));
    QSettings config(configPath, QSettings::IniFormat);
    const QString configured = config.value(QStringLiteral("Update/LatestJsonUrl")).toString().trimmed();
    return configured.isEmpty() ? QStringLiteral("https://darkeye.win/latest.json") : configured;
}

QString executableFileFilter()
{
#ifdef Q_OS_WIN
    return QStringLiteral("可执行文件 (*.exe);;所有文件 (*)");
#else
    return QStringLiteral("可执行文件 (*)");
#endif
}

QString updaterExecutablePath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("DarkEyeUpdater.exe"));
}

QString collectorBridgeUrl(const QUrl &workApi)
{
    QUrl result = workApi;
    QString path = result.path();
    const QString suffix = QStringLiteral("/api/v1/work");
    if (path.endsWith(suffix)) path.chop(suffix.size());
    else path.clear();
    result.setPath(path);
    result.setQuery({});
    result.setFragment({});
    QString url = result.toString();
    while (url.endsWith(u'/')) url.chop(1);
    return url;
}

QStringList updaterArguments()
{
    return {QStringLiteral("--install-dir"), QCoreApplication::applicationDirPath(),
            QStringLiteral("--current-version"), QStringLiteral(DARKEYE_VERSION),
            QStringLiteral("--main-exe"), QFileInfo(QCoreApplication::applicationFilePath()).fileName(),
            QStringLiteral("--latest-json-url"), latestManifestUrl(),
            QStringLiteral("--keep"), QStringLiteral("data"),
            QStringLiteral("--pid"), QString::number(QCoreApplication::applicationPid())};
}

bool startUpdater(QWidget *parent, const QStringList &additionalArguments)
{
    const QString updater = updaterExecutablePath();
    if (!QFileInfo::exists(updater)) {
        QMessageBox::critical(parent, QStringLiteral("更新失败"),
                              QStringLiteral("未找到更新程序：%1").arg(updater));
        return false;
    }
    QStringList arguments = updaterArguments();
    arguments.append(additionalArguments);
    if (!QProcess::startDetached(updater, arguments, QCoreApplication::applicationDirPath())) {
        QMessageBox::critical(parent, QStringLiteral("更新失败"), QStringLiteral("无法启动更新程序。"));
        return false;
    }
    return true;
}

} // namespace

NfoSettingsPage::NfoSettingsPage(QSqlDatabase publicDatabase, settings::Paths paths, QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)), m_paths(std::move(paths)) {}

void NfoSettingsPage::lazyLoad()
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new DesignLabel(QStringLiteral("批量导入使用“视频”设置中配置的视频文件夹路径；请先在那里添加有效路径。"), this));
    const auto addButton = [this, layout](const QString &text, const QString &tip, auto callback) {
        auto *button = new DesignButton(text, this); button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this, callback); layout->addWidget(button);
    };
    addButton(QStringLiteral("从视频路径扫描并导入 Jvedio NFO"), QStringLiteral("递归查找 .nfo；已存在番号会跳过。"), [this] { importFolder(false, true); });
    addButton(QStringLiteral("从文件夹导入 Jvedio NFO"), QStringLiteral("选择任意文件夹后递归导入其中的 .nfo。"), [this] { importFolder(false, false); });
    addButton(QStringLiteral("从 Jvedio NFO 导入作品"), QStringLiteral("选择一个 Jvedio/Kodi 风格的 .nfo 文件。"), [this] { importFile(false); });
    layout->addWidget(new DesignLabel(QStringLiteral("以下为 MDCZ 风格 NFO 独立导入入口。"), this));
    addButton(QStringLiteral("从视频路径扫描并导入 MDCZ NFO"), QStringLiteral("递归查找 .nfo；已存在番号会跳过。"), [this] { importFolder(true, true); });
    addButton(QStringLiteral("从文件夹导入 MDCZ NFO"), QStringLiteral("选择任意文件夹后递归导入其中的 .nfo。"), [this] { importFolder(true, false); });
    addButton(QStringLiteral("从 MDCZ NFO 导入作品"), QStringLiteral("选择一个 MDCZ 风格的 .nfo 文件。"), [this] { importFile(true); });
    layout->addStretch();
}

void NfoSettingsPage::importFile(bool mdcz)
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 NFO 文件"), {}, QStringLiteral("NFO 文件 (*.nfo);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    QString message;
    if (nfo::importNfo(m_publicDatabase, path, mdcz, m_paths, &message)) {
        emit referencesChanged(ReferenceKind::Maker); emit referencesChanged(ReferenceKind::Label);
        emit referencesChanged(ReferenceKind::Series); emit tagsChanged(); emit actressesChanged();
        emit actorsChanged(); emit worksChanged(); QMessageBox::information(this, QStringLiteral("导入成功"), message);
    }
    else QMessageBox::warning(this, QStringLiteral("未导入"), message);
}

void NfoSettingsPage::importFolder(bool mdcz, bool useVideoPaths)
{
    QStringList roots;
    if (useVideoPaths) roots = settings::app().videoPaths;
    else {
        const QString root = QFileDialog::getExistingDirectory(this, QStringLiteral("选择包含 NFO 的文件夹"));
        if (!root.isEmpty()) roots.append(root);
    }
    if (roots.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先配置至少一个有效的视频文件夹路径。")); return; }
    QStringList files;
    for (const QString &root : roots) {
        QDirIterator it(root, {QStringLiteral("*.nfo"), QStringLiteral("*.NFO")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.next());
    }
    files.removeDuplicates();
    if (files.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未发现 .nfo 文件。")); return; }
    if (m_batchImportRunning) return;
    m_batchImportRunning = true;
    auto *progress = new QProgressDialog(QStringLiteral("准备导入…"), QStringLiteral("取消"), 0, files.size(), this);
    progress->setWindowTitle(mdcz ? QStringLiteral("批量导入 MDCZ NFO") : QStringLiteral("批量导入 NFO"));
    progress->setWindowModality(Qt::WindowModal); progress->setAutoClose(false); progress->show();
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    connect(progress, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
    const QString databasePath = m_publicDatabase.databaseName(); const settings::Paths paths = m_paths;
    QPointer<NfoSettingsPage> page(this); QPointer<QProgressDialog> guardedProgress(progress);
    QThreadPool::globalInstance()->start([files, mdcz, cancelled, databasePath, paths, page, guardedProgress] {
        SqliteConnection connection; QString openError;
        int imported = 0, skipped = 0, failed = 0; QStringList errors; bool stopped = false;
        if (!connection.open(databasePath, false, &openError)) { failed = files.size(); errors.append(openError); }
        else for (qsizetype i = 0; i < files.size(); ++i) {
            if (cancelled->load()) { stopped = true; break; }
            const QString file = files.at(i);
            QMetaObject::invokeMethod(qApp, [guardedProgress, i, total = files.size(), file] { if (guardedProgress) { guardedProgress->setValue(i); guardedProgress->setLabelText(QStringLiteral("正在导入 (%1/%2)：%3").arg(i + 1).arg(total).arg(QFileInfo(file).fileName())); } }, Qt::QueuedConnection);
            QString message; if (nfo::importNfo(connection.database(), file, mdcz, paths, &message)) ++imported;
            else if (message.contains(QStringLiteral("已在库中"))) ++skipped;
            else { ++failed; if (errors.size() < 8) errors.append(QFileInfo(file).fileName() + QStringLiteral(": ") + message); }
        }
        QMetaObject::invokeMethod(qApp, [page, guardedProgress, imported, skipped, failed, errors, stopped, total = files.size()] {
            if (guardedProgress) guardedProgress->deleteLater(); if (!page) return; page->m_batchImportRunning = false;
            if (imported) { emit page->referencesChanged(ReferenceKind::Maker); emit page->referencesChanged(ReferenceKind::Label); emit page->referencesChanged(ReferenceKind::Series); emit page->tagsChanged(); emit page->actressesChanged(); emit page->actorsChanged(); emit page->worksChanged(); }
            QString result = QStringLiteral("共扫描 %1 个 NFO。\n新导入：%2\n跳过（番号已存在）：%3\n失败：%4").arg(total).arg(imported).arg(skipped).arg(failed); if (stopped) result.prepend(QStringLiteral("已取消，以下为已处理部分的结果。\n")); if (!errors.isEmpty()) result += QStringLiteral("\n\n") + errors.join(QLatin1Char('\n')); QMessageBox::information(page, QStringLiteral("批量导入完成"), result);
        }, Qt::QueuedConnection);
    });
}

AboutSettingsPage::AboutSettingsPage(ThemeService &themeService, QWidget *parent, settings::Paths paths)
    : LazyWidget(parent), m_themeService(themeService), m_runtimePaths(std::move(paths))
{
}

void AboutSettingsPage::lazyLoad()
{
    auto *layout = new QVBoxLayout(this);

    auto *logo = new QLabel(this);
    logo->setObjectName(QStringLiteral("AboutSoftwareLogo"));
    logo->setFixedSize(192, 192);
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(QIcon(QStringLiteral(":/icons/logo.svg"))
                        .pixmap(logo->size(), logo->devicePixelRatioF()));
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(16);

    auto *versionRow = new QHBoxLayout;
    auto *version = new DesignLabel(
        QStringLiteral("当前版本 %1").arg(QStringLiteral(DARKEYE_VERSION)), this);
    versionRow->addWidget(version);

    auto *checkUpdate = new DesignButton(QStringLiteral("检查更新"), this);
    checkUpdate->setToolTip(QStringLiteral("检查官方发布的最新版本。"));
    connect(checkUpdate, &QPushButton::clicked, this, [this, checkUpdate] {
        checkUpdate->setEnabled(false);
        checkUpdate->setText(QStringLiteral("检查中…"));
        auto *service = new UpdateService(this);
        connect(service, &UpdateService::finished, this,
                [this, checkUpdate, service](const utils::UpdateCheckResult &result) {
            checkUpdate->setText(QStringLiteral("检查更新"));
            checkUpdate->setEnabled(true);
            service->deleteLater();
            if (!result.success) {
                QMessageBox::critical(this, result.title, result.message);
                return;
            }
            if (!result.updateAvailable) {
                QMessageBox::information(this, result.title, result.message);
                return;
            }
#ifndef Q_OS_WIN
            if (QMessageBox::question(this, result.title,
                    result.message + QStringLiteral("\n\n打开发布页下载适合本系统的版本？")) == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/gty5678/darkeye-cpp/releases")));
            return;
#endif
            const QString prompt = result.message
                + QStringLiteral("\n\n已检测到新版本。软件将退出以完成更新，是否立即更新？");
            if (QMessageBox::question(this, result.title, prompt,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::Yes) != QMessageBox::Yes) {
                QMessageBox::information(this, QStringLiteral("已取消更新"),
                                         QStringLiteral("你已取消更新。"));
                return;
            }
            if (!startUpdater(this, {})) return;
            QMessageBox::information(this, QStringLiteral("开始更新"),
                                     result.message + QStringLiteral("\n\n已启动更新程序，软件即将退出完成更新。"));
            QTimer::singleShot(200, qApp, &QCoreApplication::quit);
        });
        service->check(QUrl::fromUserInput(latestManifestUrl()),
                       QStringLiteral(DARKEYE_VERSION));
    });
    versionRow->addWidget(checkUpdate);

    auto *localUpdate = new DesignButton(QStringLiteral("使用本地安装包更新…"), this);
    localUpdate->setToolTip(
        QStringLiteral("选择从 GitHub Release 下载的 zip 或 tar.zst，由更新程序离线安装。"));
    connect(localUpdate, &QPushButton::clicked, this, [this] {
        const QString explanation = QStringLiteral(
            "本地安装将无视版本号，直接覆盖安装目录下的文件。只能升级不能降级。"
            "跨最小版本可随意升降，中版本、大版本只能升级。\n\n"
            "覆盖安装时仅保留目录下的 data 文件夹内数据；其余文件将被新版本覆盖。\n\n确定继续？");
        if (QMessageBox::question(this, QStringLiteral("覆盖安装说明"), explanation,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        const QString packagePath = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择本地安装包"), {},
            QStringLiteral("安装包 (*.zip *.tar.zst);;ZIP 压缩包 (*.zip);;Zstandard (*.tar.zst);;所有文件 (*.*)"));
        if (packagePath.isEmpty()) return;
        if (!QFileInfo(packagePath).isFile()) {
            QMessageBox::critical(this, QStringLiteral("更新失败"), QStringLiteral("所选文件无效。"));
            return;
        }
        const QString prompt = QStringLiteral("将使用以下文件更新：\n%1\n\n软件将退出以完成更新，是否继续？")
                                   .arg(QDir::toNativeSeparators(packagePath));
        if (QMessageBox::question(this, QStringLiteral("使用本地安装包更新"), prompt,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            QMessageBox::information(this, QStringLiteral("已取消更新"), QStringLiteral("你已取消更新。"));
            return;
        }
        if (!startUpdater(this, {QStringLiteral("--local-package"), packagePath})) return;
        QMessageBox::information(this, QStringLiteral("开始更新"),
                                 QStringLiteral("已启动更新程序，软件即将退出完成更新。"));
        QTimer::singleShot(200, qApp, &QCoreApplication::quit);
    });
#ifndef Q_OS_WIN
    // Native installers / package managers perform upgrades on these platforms.
    localUpdate->hide();
#endif
    versionRow->addWidget(localUpdate);

    const auto addExternalButton = [this, versionRow](const QString &text, const QString &url)
    {
        auto *button = new DesignButton(text, this);
        connect(button, &QPushButton::clicked, this,
                [url] { QDesktopServices::openUrl(QUrl(url)); });
        versionRow->addWidget(button);
    };
    addExternalButton(QStringLiteral("意见反馈"),
                      QStringLiteral("https://github.com/de4321/darkeye/issues"));
    addExternalButton(QStringLiteral("版本记录"),
                      QStringLiteral("https://de4321.github.io/darkeye/CHANGELOG/"));
    versionRow->addStretch();
    layout->addLayout(versionRow);

    auto *updateOptions = new QHBoxLayout;
    const AppSettings appSettings = settings::app();
    auto *automaticUpdate = new TokenCheckBox(QStringLiteral("自动检查更新（每周一次）"), this);
    automaticUpdate->setChecked(appSettings.update.automaticCheck);
    auto *updateNotification =
        new TokenCheckBox(QStringLiteral("有新版本时提醒我"), this);
    updateNotification->setChecked(appSettings.update.updateNotification);
    connect(automaticUpdate, &QCheckBox::toggled, this, [updateNotification](bool enabled) {
        updateNotification->setEnabled(enabled);
        if (!enabled)
            updateNotification->setChecked(false);
        AppSettings saved = settings::app();
        saved.update.automaticCheck = enabled;
        if (!enabled)
            saved.update.updateNotification = false;
        settings::saveApp(saved);
    });
    connect(updateNotification, &QCheckBox::toggled, this, [](bool enabled) {
        AppSettings saved = settings::app();
        saved.update.updateNotification = enabled;
        settings::saveApp(saved);
    });
    updateNotification->setEnabled(appSettings.update.automaticCheck);
    updateOptions->addWidget(automaticUpdate);
    updateOptions->addWidget(updateNotification);
    updateOptions->addStretch();
    layout->addLayout(updateOptions);

    auto *runtimePaths = new QFormLayout;
    runtimePaths->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto addRuntimePath = [this, runtimePaths](const QString &title, const QString &path)
    {
        auto *value = new DesignLabel(QDir::toNativeSeparators(QDir(path).absolutePath()), this);
        value->setTextFormat(Qt::PlainText);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        value->setWordWrap(true);
        runtimePaths->addRow(new DesignLabel(title, this), value);
    };
    addRuntimePath(QStringLiteral("数据目录（data-dir）"), m_runtimePaths.dataDirectory());
    addRuntimePath(QStringLiteral("配置目录（config）"), m_runtimePaths.configDirectory());
    addRuntimePath(QStringLiteral("知识库目录（avwiki）"), m_runtimePaths.wikiDirectory());
    layout->addSpacing(12);
    layout->addLayout(runtimePaths);

    auto *downloadRow = new QHBoxLayout;
    downloadRow->addWidget(new DesignLabel(QStringLiteral("下载浏览器插件"), this));
    const auto addDownloadButton = [this, downloadRow](const QString &text)
    {
        auto *button = new DesignButton(text, this);
        connect(button, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(
                QUrl(QStringLiteral("https://github.com/de4321/darkeye/releases")));
        });
        downloadRow->addWidget(button);
    };
    addDownloadButton(QStringLiteral("Firefox 插件"));
    addDownloadButton(QStringLiteral("Chrome/Edge 插件"));
    downloadRow->addStretch();
    layout->addLayout(downloadRow);

    auto *links = new QHBoxLayout;
    links->setSpacing(16);
    const auto addLink = [this](QVBoxLayout *column, const QString &title,
                                const QString &description, const QString &url)
    {
        auto *card = new TokenLinkCard(title, description, url, &m_themeService, this);
        card->setMinimumWidth(0);
        card->setMaximumWidth(280);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        column->addWidget(card);
    };
    auto *projectLinks = new QVBoxLayout;
    projectLinks->setSpacing(8);
    projectLinks->setAlignment(Qt::AlignTop);
    projectLinks->addWidget(new DesignLabel(QStringLiteral("项目链接"), this));
    const QList<std::tuple<QString, QString, QString>> projects = {
        {QStringLiteral("GitHub"), QStringLiteral("源代码仓库与问题反馈"),
         QStringLiteral("https://github.com/de4321/darkeye")},
        {QStringLiteral("Discord"), QStringLiteral("社区讨论与支持频道"),
         QStringLiteral("https://discord.gg/N7wJVNVA")},
        {QStringLiteral("官网"), QStringLiteral("产品介绍与主页"),
         QStringLiteral("https://de4321.github.io/darkeye-webpage/")},
        {QStringLiteral("文档"), QStringLiteral("使用说明与开发文档"),
         QStringLiteral("https://de4321.github.io/darkeye/")},
    };
    for (const auto &[title, description, url] : projects)
    {
        addLink(projectLinks, title, description, url);
    }

    auto *referenceLinks = new QVBoxLayout;
    referenceLinks->setSpacing(8);
    referenceLinks->setAlignment(Qt::AlignTop);
    referenceLinks->addWidget(new DesignLabel(QStringLiteral("参考项目"), this));
    const QList<std::tuple<QString, QString, QString>> references = {
        {QStringLiteral("mdcz"), QStringLiteral("开源媒体库元数据刮削与管理"),
         QStringLiteral("https://github.com/ShotHeadman/mdcz")},
        {QStringLiteral("Jvedio"), QStringLiteral("Windows 本地影片管理与刮削工具"),
         QStringLiteral("https://github.com/hitchao/Jvedio")},
        {QStringLiteral("JavSP"), QStringLiteral("JAV 刮削工具"),
         QStringLiteral("https://github.com/Yuukiy/JavSP")},
        {QStringLiteral("JAV-JHS"), QStringLiteral("油猴脚本，站点体验增强"),
         QStringLiteral("https://sleazyfork.org/zh-CN/scripts/558525-jav-jhs")},
    };
    for (const auto &[title, description, url] : references)
    {
        addLink(referenceLinks, title, description, url);
    }
    links->addLayout(projectLinks);
    links->addLayout(referenceLinks);
    links->addStretch();
    layout->addLayout(links);
    layout->addStretch();
}

VideoSettingsPage::VideoSettingsPage(QSqlDatabase publicDatabase,
                                     QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase))
{
}

void VideoSettingsPage::lazyLoad()
{
    auto *layout = new QVBoxLayout(this);

    auto *playerRow = new QHBoxLayout;
    playerRow->addWidget(new DesignLabel(QStringLiteral("本地播放器（可选）："), this));
    m_player = new DesignLineEdit(this);
    m_player->setPlaceholderText(
        QStringLiteral("留空则使用系统默认程序；书架/DVD 与作品页播放本地文件时生效"));
    m_player->setClearButtonEnabled(true);
    const AppSettings appSettings = settings::app();
    m_player->setText(appSettings.localVideoPlayer);
    playerRow->addWidget(m_player, 1);
    auto *browse = new DesignButton(QStringLiteral("浏览…"), this);
    browse->setToolTip(QStringLiteral("选择播放器可执行文件（如 VLC、MPC-HC 等）"));
    playerRow->addWidget(browse);
    layout->addLayout(playerRow);

    m_paths = new MultiplePathManagement(QStringLiteral("视频文件夹路径管理："), this);
    m_paths->setObjectName(QStringLiteral("VideoPathManagement"));
    m_paths->setMinimumHeight(300);
    m_paths->loadPaths(appSettings.videoPaths);
    layout->addWidget(m_paths);

    auto *scan = new DesignButton(QStringLiteral("扫描本地视频提取番号并录入数据库"), this);
    scan->setToolTip(
        QStringLiteral("扫描本地视频的路径下的所有视频，并提取视频番号，将没有的番号尝试去抓取信息"));
    layout->addWidget(scan);
    auto *match = new DesignButton(QStringLiteral("同步作品本地视频路径"), this);
    match->setToolTip(QStringLiteral(
        "扫描已配置文件夹中的视频，从文件名提取番号并与库中作品匹配，"
        "将匹配到的本地绝对路径写入作品表的 video_url（多条英文逗号分隔、去重）；"
        "以本次扫描结果为准完全覆盖，不保留库中旧路径"));
    layout->addWidget(match);

    connect(m_player, &QLineEdit::editingFinished, this, &VideoSettingsPage::savePlayer);
    connect(browse, &QPushButton::clicked, this, &VideoSettingsPage::browsePlayer);
    connect(m_paths, &MultiplePathManagement::pathsChanged, this,
            &VideoSettingsPage::savePaths);
    connect(scan, &QPushButton::clicked, this, &VideoSettingsPage::scanMissingSerials);
    connect(match, &QPushButton::clicked, this, &VideoSettingsPage::synchronizeVideoUrls);
}

void VideoSettingsPage::savePlayer()
{
    AppSettings appSettings = settings::app();
    appSettings.localVideoPlayer = m_player->text().trimmed();
    settings::saveApp(appSettings);
}

void VideoSettingsPage::savePaths()
{
    QStringList paths;
    for (const QString &path : m_paths->paths())
    {
        const QString normalized = path.trimmed();
        if (!normalized.isEmpty() && normalized != QStringLiteral("."))
        {
            paths.append(normalized);
        }
    }
    AppSettings appSettings = settings::app();
    appSettings.videoPaths = paths;
    settings::saveApp(appSettings);
}

void VideoSettingsPage::browsePlayer()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择播放器可执行文件"), QString{},
        executableFileFilter());
    if (!path.isEmpty())
    {
        m_player->setText(path);
        savePlayer();
    }
}

QStringList VideoSettingsPage::configuredPaths() const
{
    QStringList paths;
    for (const QString &path : m_paths->paths())
    {
        const QString normalized = path.trimmed();
        if (!normalized.isEmpty() && normalized != QStringLiteral("."))
            paths.append(normalized);
    }
    return paths;
}

void VideoSettingsPage::showFilesWithoutSerial(
    const QList<QPair<QString, QString>> &entries)
{
    if (entries.isEmpty()) return;
    constexpr qsizetype limit = 80;
    QStringList lines;
    for (qsizetype index = 0; index < qMin(limit, entries.size()); ++index)
        lines.append(entries.at(index).first + QLatin1Char('\n') + entries.at(index).second);
    QString suffix;
    if (entries.size() > limit)
        suffix = QStringLiteral("\n\n… 另有 %1 条未列出（共 %2 个文件）")
                     .arg(entries.size() - limit).arg(entries.size());
    QMessageBox::warning(this, QStringLiteral("无法提取番号"),
                         QStringLiteral("以下视频未能从文件名识别番号：\n\n")
                             + lines.join(QStringLiteral("\n\n")) + suffix);
}

void VideoSettingsPage::scanMissingSerials()
{
    const QStringList paths = configuredPaths();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在上方配置至少一个视频文件夹路径"));
        return;
    }
    const VideoLibraryScanResult result =
        VideoLibraryService(m_publicDatabase).scanMissingSerials(paths);
    if (!result.succeeded)
    {
        QMessageBox::critical(this, QStringLiteral("扫描失败"), result.errorMessage);
        return;
    }
    showFilesWithoutSerial(result.filesWithoutSerial);
    if (result.missingSerials.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("本地视频的番号均已存在于数据库中"));
        return;
    }
    emit quickWorkRequested(result.missingSerials);
}

void VideoSettingsPage::synchronizeVideoUrls()
{
    const QStringList paths = configuredPaths();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在上方配置至少一个视频文件夹路径"));
        return;
    }
    const VideoLibraryScanResult result =
        VideoLibraryService(m_publicDatabase).synchronizeVideoUrls(paths);
    if (!result.succeeded)
    {
        QMessageBox::critical(this, QStringLiteral("同步失败"), result.errorMessage);
        return;
    }
    showFilesWithoutSerial(result.filesWithoutSerial);
    if (result.updatedWorks > 0) emit worksChanged();
    QMessageBox::information(
        this, QStringLiteral("完成"),
        QStringLiteral("共扫描 %1 个视频文件；无法提取番号 %2 个；"
                       "番号在库中无匹配 %3 个；已更新/清理 %4 条作品的 video_url。")
            .arg(result.scannedFiles).arg(result.filesWithoutSerial.size())
            .arg(result.unmatchedSerials).arg(result.updatedWorks));
}

ShortcutSettingsPage::ShortcutSettingsPage(const QString &shortcutsFile, QWidget *parent)
    : LazyWidget(parent), m_shortcutsFile(shortcutsFile)
{
}

void ShortcutSettingsPage::lazyLoad()
{
    QFile file(m_shortcutsFile);
    if (file.open(QIODevice::ReadOnly))
    {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
        {
            m_userShortcuts = document.object();
        }
    }

    struct ShortcutDefinition
    {
        QString id;
        QString name;
        QString key;
    };
    const QList<ShortcutDefinition> definitions = {
        {QStringLiteral("add_masturbation_record"), QStringLiteral("添加撸管记录"),
         QStringLiteral("M")},
        {QStringLiteral("add_quick_work"), QStringLiteral("快速添加番号"), QStringLiteral("W")},
        {QStringLiteral("add_makelove_record"), QStringLiteral("添加做爱记录"),
         QStringLiteral("L")},
        {QStringLiteral("add_sexual_rousal_record"), QStringLiteral("添加晨勃记录"),
         QStringLiteral("A")},
        {QStringLiteral("open_help"), QStringLiteral("打开文档"), QStringLiteral("H")},
        {QStringLiteral("search"), QStringLiteral("搜索"), QStringLiteral("Ctrl+F")},
        {QStringLiteral("capture"), QStringLiteral("部分截图"), QStringLiteral("C")},
        {QStringLiteral("allcapture"), QStringLiteral("全软件截图"), QStringLiteral("Shift+C")},
    };

    auto *layout = new QVBoxLayout(this);
    for (const auto &definition : definitions)
    {
        auto *row = new QWidget(this);
        row->setProperty("designScrollMenuContent", true);
        row->setObjectName(QStringLiteral("ShortcutSettingRow_%1").arg(definition.id));
        auto *rowLayout = new QHBoxLayout(row);
        auto *label = new DesignLabel(definition.name, row);
        label->setFixedWidth(100);
        auto *editor = new TokenKeySequenceEdit(row);
        editor->setFixedWidth(150);
        editor->setKeySequence(QKeySequence(
            m_userShortcuts.value(definition.id).toString(definition.key)));
        auto *reset = new DesignButton(QStringLiteral("恢复"), row);
        reset->setFixedWidth(50);
        rowLayout->addWidget(label);
        rowLayout->addWidget(editor);
        rowLayout->addWidget(reset);
        rowLayout->addStretch();
        layout->addWidget(row);
        connect(editor, &QKeySequenceEdit::editingFinished, this,
                [this, id = definition.id, editor] { applyShortcut(id, editor); });
        connect(reset, &QPushButton::clicked, this,
                [this, id = definition.id, key = definition.key, editor] {
                    resetShortcut(id, key, editor);
                });
    }
    layout->addStretch();
    layout->addWidget(
        new DesignLabel(QStringLiteral("<small>配置保存位置：%1</small>").arg(settings::Paths().shortcutsFile().toHtmlEscaped()),
                        this));
}

void ShortcutSettingsPage::applyShortcut(const QString &actionId,
                                         TokenKeySequenceEdit *editor)
{
    const QString shortcut = editor->keySequence().toString();
    m_userShortcuts.insert(actionId, shortcut);
    save();
    if (auto *action = window()->findChild<QAction *>(actionId))
    {
        action->setShortcut(QKeySequence(shortcut));
    }
}

void ShortcutSettingsPage::resetShortcut(const QString &actionId, const QString &defaultKey,
                                         TokenKeySequenceEdit *editor)
{
    m_userShortcuts.remove(actionId);
    save();
    editor->setKeySequence(QKeySequence(defaultKey));
    if (auto *action = window()->findChild<QAction *>(actionId))
    {
        action->setShortcut(QKeySequence(defaultKey));
    }
}

void ShortcutSettingsPage::save() const
{
    QSaveFile file(m_shortcutsFile);
    if (!file.open(QIODevice::WriteOnly))
    {
        return;
    }
    file.write(QJsonDocument(m_userShortcuts).toJson(QJsonDocument::Indented));
    file.commit();
}

CommonSettingsPage::CommonSettingsPage(ThemeService &themeService,
                                       QWidget *parent)
    : LazyWidget(parent), m_themeService(themeService)
{
}

void CommonSettingsPage::lazyLoad()
{
    auto *layout = new QFormLayout(this);

    m_primaryColorRow = new QWidget(this);
    m_primaryColorRow->setProperty("designScrollMenuContent", true);
    m_primaryColorRow->setObjectName(QStringLiteral("PrimaryColorRow"));
    auto *primaryLayout = new QHBoxLayout(m_primaryColorRow);
    primaryLayout->setContentsMargins(0, 0, 0, 0);
    const QString initialPrimary = m_themeService.customPrimary().isEmpty()
        ? ThemeService::tokens(m_themeService.current()).primary
        : m_themeService.customPrimary();
    m_colorPicker = new ColorPicker(QColor(initialPrimary), false, ColorPicker::Shape::Circle,
                                    m_primaryColorRow);
    primaryLayout->addWidget(m_colorPicker);
    primaryLayout->addStretch();

    m_themeSelector = new DesignComboBox(this);
    m_themeSelector->setAccessibleName(QStringLiteral("主题"));

    layout->addRow(new DesignLabel(QStringLiteral("主色"), this), m_primaryColorRow);
    layout->addRow(new DesignLabel(QStringLiteral("主题"), this), m_themeSelector);

    connect(m_colorPicker, &ColorPicker::colorConfirmed, this,
            &CommonSettingsPage::savePrimaryColor);
    connect(&m_themeService, &ThemeService::themeChanged, this,
            [this](ThemeId) { updatePrimaryPickerState(); });
    updatePrimaryPickerState();
}

QComboBox *CommonSettingsPage::themeSelector() const
{
    const_cast<CommonSettingsPage *>(this)->initialize();
    return m_themeSelector;
}

void CommonSettingsPage::updatePrimaryPickerState()
{
    const ThemeId theme = m_themeService.current();
    const bool supportsCustomPrimary = theme == ThemeId::Light || theme == ThemeId::Dark;
    m_primaryColorRow->setEnabled(supportsCustomPrimary);
    if (!supportsCustomPrimary)
    {
        AppSettings appSettings = settings::app();
        appSettings.customPrimary.clear();
        settings::saveApp(appSettings);
        return;
    }
    const QString color = m_themeService.customPrimary().isEmpty()
        ? ThemeService::tokens(theme).primary
        : m_themeService.customPrimary();
    m_colorPicker->setColor(color);
}

void CommonSettingsPage::savePrimaryColor(const QString &color)
{
    if (m_themeService.current() != ThemeId::Light && m_themeService.current() != ThemeId::Dark)
    {
        return;
    }
    m_themeService.setTheme(m_themeService.current(), color);
    AppSettings appSettings = settings::app();
    appSettings.customPrimary = color;
    settings::saveApp(appSettings);
}

CrawlerSettingsPage::CrawlerSettingsPage(QWidget *parent)
    : LazyWidget(parent)
{
}

void CrawlerSettingsPage::lazyLoad()
{
    const CrawlerSettings values = settings::crawler();
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new DesignLabel(QStringLiteral("<h3>信息补充器相关设置</h3>"), this));
    auto *form = new QFormLayout;

    const auto addUrlRow = [this, form](const QString &label, const QUrl &value,
                                        const QUrl &defaultValue, QLineEdit **field) {
        *field = new DesignLineEdit(this);
        (*field)->setText(value.toString());
        (*field)->setClearButtonEnabled(true);
        auto *row = new QWidget(this);
        row->setProperty("designScrollMenuContent", true);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *reset = new DesignButton(QStringLiteral("还原默认"), row);
        rowLayout->addWidget(*field, 1);
        rowLayout->addWidget(reset);
        form->addRow(new DesignLabel(label, this), row);
        connect(*field, &QLineEdit::editingFinished, this, &CrawlerSettingsPage::save);
        connect(reset, &QPushButton::clicked, this,
                [this, field, defaultValue] { resetCrawlerUrl(*field, defaultValue); });
    };
    const CrawlerSettings defaults;
    addUrlRow(QStringLiteral("作品 API 前缀"), values.workApiBaseUrl, defaults.workApiBaseUrl, &m_workApi);
    addUrlRow(QStringLiteral("女优 API 前缀"), values.actressApiBaseUrl, defaults.actressApiBaseUrl, &m_actressApi);
    addUrlRow(QStringLiteral("图片下载 API"), values.coverFetchApiUrl, defaults.coverFetchApiUrl, &m_coverApi);
    addUrlRow(QStringLiteral("热门女优 API"), values.topActressesApiUrl, defaults.topActressesApiUrl, &m_topActressesApi);
    form->addRow(new DesignLabel(QStringLiteral("说明"), this),
                 new DesignLabel(QStringLiteral("作品/女优为完整前缀，程序会追加 /{serial} 或 /{name}；"
                                                 "图片下载、热门女优请填写完整地址。"), this));

    m_collectorExecutable = new DesignLineEdit(this);
    m_collectorExecutable->setText(values.collectorExecutable);
    m_collectorExecutable->setPlaceholderText(QStringLiteral("可选：信息补充器的可执行文件"));
    m_collectorExecutable->setClearButtonEnabled(true);
    auto *collectorRow = new QHBoxLayout;
    collectorRow->addWidget(m_collectorExecutable, 1);
    auto *browse = new DesignButton(QStringLiteral("浏览…"), this);
    collectorRow->addWidget(browse);
    form->addRow(new DesignLabel(QStringLiteral("信息补充器可执行文件"), this), collectorRow);
    m_autoStartCollector = new ToggleSwitch(48, 24, nullptr, this);
    m_autoStartCollector->setChecked(values.autoStartCollector);
    form->addRow(new DesignLabel(QStringLiteral("打开软件自动启动信息补充器"), this),
                 m_autoStartCollector);
    auto *controls = new QWidget(this);
    controls->setProperty("designScrollMenuContent", true);
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    auto *start = new DesignButton(QStringLiteral("启动"), controls);
    auto *test = new DesignButton(QStringLiteral("测试"), controls);
    start->setToolTip(QStringLiteral("独立启动信息补充器；程序退出时不会结束该进程。"));
    test->setToolTip(QStringLiteral("请求采集器的 /api/v1/exist 接口验证连通性。"));
    controlsLayout->addWidget(start);
    controlsLayout->addWidget(test);
    controlsLayout->addStretch();
    form->addRow(new DesignLabel(QStringLiteral("信息服务器控制"), this), controls);
    m_collectorStatus = new DesignLabel(QStringLiteral("状态：未检测"), this);
    form->addRow(new DesignLabel(QStringLiteral("运行状态"), this), m_collectorStatus);
    layout->addLayout(form);
    layout->addStretch();

    connect(m_collectorExecutable, &QLineEdit::editingFinished, this, &CrawlerSettingsPage::save);
    connect(browse, &QPushButton::clicked, this, &CrawlerSettingsPage::browseCollector);
    connect(m_autoStartCollector, &ToggleSwitch::toggled, this,
            [this](bool) { save(); });
    connect(start, &QPushButton::clicked, this, &CrawlerSettingsPage::startCollector);
    connect(test, &QPushButton::clicked, this, &CrawlerSettingsPage::testCollector);
    QTimer::singleShot(0, this, &CrawlerSettingsPage::testCollector);
}

void CrawlerSettingsPage::save()
{
    CrawlerSettings values = settings::crawler();
    values.workApiBaseUrl = QUrl::fromUserInput(m_workApi->text().trimmed());
    values.actressApiBaseUrl = QUrl::fromUserInput(m_actressApi->text().trimmed());
    values.coverFetchApiUrl = QUrl::fromUserInput(m_coverApi->text().trimmed());
    values.topActressesApiUrl = QUrl::fromUserInput(m_topActressesApi->text().trimmed());
    values.collectorExecutable = m_collectorExecutable->text().trimmed();
    values.autoStartCollector = m_autoStartCollector->isChecked();
    settings::saveCrawler(values);
}

void CrawlerSettingsPage::browseCollector()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择信息补充器可执行文件"), QString{},
        executableFileFilter());
    if (!path.isEmpty())
    {
        m_collectorExecutable->setText(path);
        save();
    }
}

void CrawlerSettingsPage::resetCrawlerUrl(QLineEdit *field, const QUrl &value)
{
    field->setText(value.toString());
    save();
}

void CrawlerSettingsPage::setCollectorStatus(const QString &status)
{
    if (m_collectorStatus) m_collectorStatus->setText(QStringLiteral("状态：") + status);
}

void CrawlerSettingsPage::startCollector()
{
    save();
    const QString executable = m_collectorExecutable->text().trimmed();
    if (executable.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择信息补充器可执行文件。"));
        return;
    }
    if (!QFileInfo::exists(executable) || !QProcess::startDetached(executable, {}, QFileInfo(executable).absolutePath())) {
        setCollectorStatus(QStringLiteral("启动失败"));
        QMessageBox::warning(this, QStringLiteral("无法启动"), QStringLiteral("无法启动信息补充器：%1").arg(executable));
        return;
    }
    setCollectorStatus(QStringLiteral("已启动，等待服务就绪…"));
    QTimer::singleShot(1000, this, &CrawlerSettingsPage::testCollector);
}

void CrawlerSettingsPage::testCollector()
{
    save();
    const QUrl base(collectorBridgeUrl(QUrl::fromUserInput(m_workApi->text().trimmed())));
    QUrl endpoint = base;
    endpoint.setPath(endpoint.path() + QStringLiteral("/api/v1/exist"));
    if (!endpoint.isValid() || endpoint.scheme().isEmpty()) {
        setCollectorStatus(QStringLiteral("地址无效"));
        return;
    }
    setCollectorStatus(QStringLiteral("检测服务…"));
    auto *manager = new QNetworkAccessManager(this);
    QNetworkReply *reply = manager->get(QNetworkRequest(endpoint));
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager] {
        const bool reachable = reply->error() == QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
        setCollectorStatus(reachable ? QStringLiteral("运行中（HTTP 已就绪）")
                                     : QStringLiteral("未启动或不可达：") + reply->errorString());
        reply->deleteLater(); manager->deleteLater();
    });
    timeout->start(5000);
}

TranslationSettingsPage::TranslationSettingsPage(QWidget *parent)
    : LazyWidget(parent)
{
}

TranslationSettingsPage::~TranslationSettingsPage() = default;

void TranslationSettingsPage::lazyLoad()
{
    const TranslationSettings values = settings::translation();
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    const auto spin = [this](int minimum, int maximum, int value) {
        auto *control = new TokenSpinBox(this);
        control->setRange(minimum, maximum);
        control->setValue(value);
        connect(control, &QSpinBox::valueChanged, this, [this](int) { save(); });
        return control;
    };

    m_engine = new DesignComboBox(this);
    m_engine->addItem(QStringLiteral("LLM（llama.cpp / OpenAI 兼容）"), QStringLiteral("llm"));
    m_engine->setCurrentIndex(m_engine->findData(values.engine.trimmed().toLower()));
    if (m_engine->currentIndex() < 0) m_engine->setCurrentIndex(0);
    form->addRow(new DesignLabel(QStringLiteral("翻译引擎"), this), m_engine);
    const auto line = [this](QFormLayout *target, const QString &label, const QString &value,
                             QLineEdit **field) {
        *field = new DesignLineEdit(this);
        (*field)->setText(value);
        (*field)->setClearButtonEnabled(true);
        target->addRow(new DesignLabel(label, this), *field);
        connect(*field, &QLineEdit::editingFinished, this, &TranslationSettingsPage::save);
    };
    line(form, QStringLiteral("模型"), values.model, &m_model);
    line(form, QStringLiteral("Base URL"), values.baseUrl, &m_baseUrl);
    line(form, QStringLiteral("API Key"), values.apiKey, &m_apiKey);
    m_model->setPlaceholderText(QStringLiteral("例如: deepseek-chat / gpt-4.1-mini"));
    m_baseUrl->setPlaceholderText(QStringLiteral("例如: https://api.openai.com/v1"));
    m_apiKey->setPlaceholderText(QStringLiteral("输入 API Key"));
    m_timeout = spin(1, 120, values.timeoutSeconds);
    m_retries = spin(0, 10, values.retries);
    form->addRow(new DesignLabel(QStringLiteral("超时（秒）"), this), m_timeout);
    form->addRow(new DesignLabel(QStringLiteral("重试次数"), this), m_retries);
    m_fallback = new DesignComboBox(this);
    m_fallback->addItem(QStringLiteral("失败返回空字符串"), QStringLiteral("empty"));
    m_fallback->addItem(QStringLiteral("失败返回原文"), QStringLiteral("source"));
    m_fallback->setCurrentIndex(m_fallback->findData(values.fallback.trimmed().toLower()));
    if (m_fallback->currentIndex() < 0) m_fallback->setCurrentIndex(0);
    form->addRow(new DesignLabel(QStringLiteral("失败回退"), this), m_fallback);
    m_testInput = new DesignPlainTextEdit(this);
    m_testInput->setPlaceholderText(QStringLiteral("输入要测试翻译的文本（日文）"));
    m_testInput->setFixedHeight(96);
    m_testOutput = new DesignPlainTextEdit(this);
    m_testOutput->setReadOnly(true);
    m_testOutput->setFixedHeight(96);
    auto *testTranslationButton = new DesignButton(QStringLiteral("测试翻译"), this);
    form->addRow(new DesignLabel(QStringLiteral("测试输入"), this), m_testInput);
    form->addRow(new DesignLabel(QStringLiteral("测试输出"), this), m_testOutput);
    form->addRow(testTranslationButton);
    layout->addLayout(form);

    layout->addWidget(new DesignLabel(QStringLiteral("<h3>llama.cpp 辅助</h3>"), this));
    auto *llamaForm = new QFormLayout;
    auto *llamaLinks = new QWidget(this);
    llamaLinks->setProperty("designScrollMenuContent", true);
    auto *llamaLinksLayout = new QHBoxLayout(llamaLinks);
    llamaLinksLayout->setContentsMargins(0, 0, 0, 0);
    const auto addLlamaLink = [this, llamaLinksLayout](const QString &text, const QString &url) {
        auto *button = new DesignButton(text, this);
        connect(button, &QPushButton::clicked, this,
                [url] { QDesktopServices::openUrl(QUrl(url)); });
        llamaLinksLayout->addWidget(button);
    };
    addLlamaLink(QStringLiteral("打开 llama.cpp Releases"),
                 QStringLiteral("https://github.com/ggml-org/llama.cpp/releases"));
    addLlamaLink(QStringLiteral("打开 7B 模型页"),
                 QStringLiteral("https://huggingface.co/SakuraLLM/Sakura-7B-Qwen2.5-v1.0-GGUF/tree/main"));
    addLlamaLink(QStringLiteral("打开 14B 模型页"),
                 QStringLiteral("https://huggingface.co/SakuraLLM/Sakura-14B-Qwen3-v1.5-GGUF/tree/main"));
    addLlamaLink(QStringLiteral("打开教程"),
                 QStringLiteral("https://de4321.github.io/darkeye/usage/#llamacpp"));
    llamaLinksLayout->addStretch();
    llamaForm->addRow(new DesignLabel(QStringLiteral("快速下载"), this), llamaLinks);
    m_serverExecutable = new DesignLineEdit(this);
    m_serverExecutable->setText(values.llama.serverExecutable);
    m_serverExecutable->setPlaceholderText(QStringLiteral("llama-server 的绝对路径"));
    m_modelPath = new DesignLineEdit(this);
    m_modelPath->setText(values.llama.modelPath);
    m_modelPath->setPlaceholderText(QStringLiteral("GGUF 模型文件路径"));
    connect(m_serverExecutable, &QLineEdit::editingFinished, this, &TranslationSettingsPage::save);
    connect(m_modelPath, &QLineEdit::editingFinished, this, &TranslationSettingsPage::save);
    auto *serverBrowse = new DesignButton(QStringLiteral("浏览…"), this);
    auto *modelBrowse = new DesignButton(QStringLiteral("浏览…"), this);
    auto *serverRow = new QHBoxLayout;
    serverRow->addWidget(m_serverExecutable, 1);
    serverRow->addWidget(serverBrowse);
    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(m_modelPath, 1);
    modelRow->addWidget(modelBrowse);
    llamaForm->addRow(new DesignLabel(QStringLiteral("llama-server"), this), serverRow);
    llamaForm->addRow(new DesignLabel(QStringLiteral("GGUF 模型"), this), modelRow);
    m_host = new DesignLineEdit(this);
    m_host->setText(values.llama.host);
    m_host->setPlaceholderText(QStringLiteral("127.0.0.1"));
    connect(m_host, &QLineEdit::editingFinished, this, &TranslationSettingsPage::save);
    m_port = spin(1, 65535, values.llama.port);
    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(new DesignLabel(QStringLiteral("host"), this));
    hostRow->addWidget(m_host, 1);
    hostRow->addWidget(new DesignLabel(QStringLiteral("port"), this));
    hostRow->addWidget(m_port);
    llamaForm->addRow(new DesignLabel(QStringLiteral("监听地址"), this), hostRow);
    m_mode = new DesignComboBox(this);
    m_mode->addItem(QStringLiteral("GPU"), QStringLiteral("gpu"));
    m_mode->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
    m_mode->setCurrentIndex(m_mode->findData(values.llama.mode.trimmed().toLower()));
    if (m_mode->currentIndex() < 0) m_mode->setCurrentIndex(0);
    llamaForm->addRow(new DesignLabel(QStringLiteral("运行模式"), this), m_mode);
    m_llamaPreset = new DesignComboBox(this);
    m_llamaPreset->addItem(QStringLiteral("不应用预设"), QStringLiteral("none"));
    m_llamaPreset->addItem(QStringLiteral("8G 显卡预设"), QStringLiteral("gpu_8g"));
    m_llamaPreset->addItem(QStringLiteral("低显存预设"), QStringLiteral("gpu_low"));
    m_llamaPreset->addItem(QStringLiteral("8核 CPU 预设"), QStringLiteral("cpu_8"));
    m_llamaPreset->addItem(QStringLiteral("16核 CPU 预设"), QStringLiteral("cpu_16"));
    llamaForm->addRow(new DesignLabel(QStringLiteral("参数预设"), this), m_llamaPreset);
    m_contextSize = spin(256, 32768, values.llama.contextSize);
    m_gpuLayers = spin(0, 200, values.llama.gpuLayers);
    m_threads = spin(1, 256, values.llama.threads);
    m_threadsBatch = spin(1, 512, values.llama.threadsBatch);
    m_batchSize = spin(1, 8192, values.llama.batchSize);
    m_microBatchSize = spin(1, 4096, values.llama.microBatchSize);
    llamaForm->addRow(new DesignLabel(QStringLiteral("上下文大小"), this), m_contextSize);
    llamaForm->addRow(new DesignLabel(QStringLiteral("GPU layers"), this), m_gpuLayers);
    llamaForm->addRow(new DesignLabel(QStringLiteral("threads"), this), m_threads);
    llamaForm->addRow(new DesignLabel(QStringLiteral("threads-batch"), this), m_threadsBatch);
    llamaForm->addRow(new DesignLabel(QStringLiteral("batch-size"), this), m_batchSize);
    llamaForm->addRow(new DesignLabel(QStringLiteral("ubatch-size"), this), m_microBatchSize);
    m_mlock = new ToggleSwitch(48, 24, nullptr, this);
    m_autoSync = new ToggleSwitch(48, 24, nullptr, this);
    m_autoStart = new ToggleSwitch(48, 24, nullptr, this);
    m_mlock->setChecked(values.llama.mlock);
    m_autoSync->setChecked(values.llama.autoSyncTranslation);
    m_autoStart->setChecked(values.llama.autoStart);
    llamaForm->addRow(new DesignLabel(QStringLiteral("mlock"), this), m_mlock);
    llamaForm->addRow(new DesignLabel(QStringLiteral("自动回填翻译配置"), this), m_autoSync);
    llamaForm->addRow(new DesignLabel(QStringLiteral("打开软件自动启动"), this), m_autoStart);
    m_commandPreview = new DesignPlainTextEdit(this);
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setFixedHeight(72);
    llamaForm->addRow(new DesignLabel(QStringLiteral("命令预览"), this), m_commandPreview);
    auto *controls = new QWidget(this);
    controls->setProperty("designScrollMenuContent", true);
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    m_startLlamaButton = new DesignButton(QStringLiteral("启动 llama-server"), controls);
    m_stopLlamaButton = new DesignButton(QStringLiteral("停止"), controls);
    auto *probe = new DesignButton(QStringLiteral("测试 /v1/models"), controls);
    controlsLayout->addWidget(m_startLlamaButton); controlsLayout->addWidget(m_stopLlamaButton); controlsLayout->addWidget(probe); controlsLayout->addStretch();
    llamaForm->addRow(new DesignLabel(QStringLiteral("控制"), this), controls);
    m_llamaStatus = new DesignLabel(QStringLiteral("状态：未启动"), this);
    llamaForm->addRow(new DesignLabel(QStringLiteral("运行状态"), this), m_llamaStatus);
    m_llamaLog = new DesignPlainTextEdit(this);
    m_llamaLog->setReadOnly(true);
    m_llamaLog->setFixedHeight(96);
    llamaForm->addRow(new DesignLabel(QStringLiteral("日志输出"), this), m_llamaLog);
    llamaForm->addRow(new DesignLabel(QStringLiteral("提示"), this),
                      new DesignLabel(QStringLiteral("显存不足请先降 GPU layers；启动后可点 /v1/models 检查。"), this));
    layout->addLayout(llamaForm);
    layout->addStretch();

    connect(m_fallback, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { save(); });
    connect(m_engine, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { save(); updateLlmFields(); });
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateModeFields(); save(); });
    connect(m_llamaPreset, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { applyLlamaPreset(); });
    connect(m_mlock, &ToggleSwitch::toggled, this, [this](bool) { save(); });
    connect(m_autoSync, &ToggleSwitch::toggled, this, [this](bool) { save(); });
    connect(m_autoStart, &ToggleSwitch::toggled, this, [this](bool) { save(); });
    connect(serverBrowse, &QPushButton::clicked, this, &TranslationSettingsPage::browseServerExecutable);
    connect(modelBrowse, &QPushButton::clicked, this, &TranslationSettingsPage::browseModel);
    connect(m_startLlamaButton, &QPushButton::clicked, this, &TranslationSettingsPage::startLlamaServer);
    connect(m_stopLlamaButton, &QPushButton::clicked, this, &TranslationSettingsPage::stopLlamaServer);
    connect(probe, &QPushButton::clicked, this, &TranslationSettingsPage::testLlamaServer);
    connect(testTranslationButton, &QPushButton::clicked, this, &TranslationSettingsPage::testTranslation);
    const auto refreshPreview = [this] { updateCommandPreview(); };
    connect(m_serverExecutable, &QLineEdit::editingFinished, this, refreshPreview);
    connect(m_modelPath, &QLineEdit::editingFinished, this, refreshPreview);
    connect(m_host, &QLineEdit::editingFinished, this, refreshPreview);
    connect(m_port, qOverload<int>(&QSpinBox::valueChanged), this, [refreshPreview](int) { refreshPreview(); });
    connect(m_contextSize, qOverload<int>(&QSpinBox::valueChanged), this, [refreshPreview](int) { refreshPreview(); });
    connect(m_gpuLayers, qOverload<int>(&QSpinBox::valueChanged), this, [refreshPreview](int) { refreshPreview(); });
    connect(m_threads, qOverload<int>(&QSpinBox::valueChanged), this, [refreshPreview](int) { refreshPreview(); });
    connect(m_threadsBatch, qOverload<int>(&QSpinBox::valueChanged), this, [refreshPreview](int) { refreshPreview(); });
    connect(m_batchSize, qOverload<int>(&QSpinBox::valueChanged), this, [refreshPreview](int) { refreshPreview(); });
    connect(m_microBatchSize, qOverload<int>(&QSpinBox::valueChanged), this, [refreshPreview](int) { refreshPreview(); });
    connect(m_mlock, &ToggleSwitch::toggled, this, [refreshPreview](bool) { refreshPreview(); });
    auto &runtime = get_llama_runtime();
    connect(&runtime, &LlamaRuntime::statusChanged, this, &TranslationSettingsPage::setLlamaStatus);
    connect(&runtime, &LlamaRuntime::logAppended, this, &TranslationSettingsPage::appendLlamaLog);
    connect(&runtime, &LlamaRuntime::runningChanged, this,
            [this](bool) { updateLlamaRunButtons(); });
    setLlamaStatus(runtime.status());
    appendLlamaLog(runtime.logs().join(u'\n'));
    updateLlmFields();
    updateModeFields();
    updateCommandPreview();
    updateLlamaRunButtons();
}

void TranslationSettingsPage::save()
{
    TranslationSettings values = settings::translation();
    values.engine = m_engine->currentData().toString();
    values.model = m_model->text().trimmed();
    values.baseUrl = m_baseUrl->text().trimmed();
    values.apiKey = m_apiKey->text();
    values.timeoutSeconds = m_timeout->value();
    values.retries = m_retries->value();
    values.fallback = m_fallback->currentData().toString();
    values.llama.serverExecutable = m_serverExecutable->text().trimmed();
    values.llama.modelPath = m_modelPath->text().trimmed();
    values.llama.host = m_host->text().trimmed();
    values.llama.port = m_port->value();
    values.llama.mode = m_mode->currentData().toString();
    values.llama.contextSize = m_contextSize->value();
    values.llama.gpuLayers = m_gpuLayers->value();
    values.llama.threads = m_threads->value();
    values.llama.threadsBatch = m_threadsBatch->value();
    values.llama.batchSize = m_batchSize->value();
    values.llama.microBatchSize = m_microBatchSize->value();
    values.llama.mlock = m_mlock->isChecked();
    values.llama.autoSyncTranslation = m_autoSync->isChecked();
    values.llama.autoStart = m_autoStart->isChecked();
    settings::saveTranslation(values);
}

void TranslationSettingsPage::updateLlmFields()
{
    m_model->setEnabled(true);
    m_baseUrl->setEnabled(true);
    m_apiKey->setEnabled(true);
}

void TranslationSettingsPage::updateModeFields()
{
    m_gpuLayers->setEnabled(m_mode->currentData().toString() == QStringLiteral("gpu"));
}

QStringList TranslationSettingsPage::llamaArguments() const
{
    LlamaCppSettings values;
    values.modelPath = m_modelPath->text().trimmed();
    values.host = m_host->text().trimmed();
    values.port = m_port->value();
    values.contextSize = m_contextSize->value();
    values.threads = m_threads->value();
    values.threadsBatch = m_threadsBatch->value();
    values.batchSize = m_batchSize->value();
    values.microBatchSize = m_microBatchSize->value();
    values.mode = m_mode->currentData().toString();
    values.gpuLayers = m_gpuLayers->value();
    values.mlock = m_mlock->isChecked();
    return LlamaRuntime::buildArguments(values);
}

void TranslationSettingsPage::updateCommandPreview()
{
    const QString executable = m_serverExecutable->text().trimmed();
    m_commandPreview->setPlainText(executable.isEmpty()
        ? QStringLiteral("请先选择 llama-server 路径。")
        : QStringLiteral("\"") + executable + QStringLiteral("\" ") + llamaArguments().join(u' '));
}

void TranslationSettingsPage::setLlamaStatus(const QString &status)
{
    m_llamaStatus->setText(QStringLiteral("状态：") + status);
}

void TranslationSettingsPage::appendLlamaLog(const QString &text)
{
    QStringList lines = m_llamaLog->toPlainText().split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) lines.append(trimmed);
    }
    constexpr qsizetype maxLogLines = 120;
    if (lines.size() > maxLogLines)
        lines = lines.sliced(lines.size() - maxLogLines);
    m_llamaLog->setPlainText(lines.join(u'\n'));
    m_llamaLog->moveCursor(QTextCursor::End);
}

void TranslationSettingsPage::applyLlamaPreset()
{
    const QString preset = m_llamaPreset->currentData().toString();
    if (preset == QStringLiteral("gpu_8g")) {
        m_mode->setCurrentIndex(m_mode->findData(QStringLiteral("gpu")));
        m_gpuLayers->setValue(99); m_batchSize->setValue(512);
        m_microBatchSize->setValue(256); m_contextSize->setValue(1024);
    } else if (preset == QStringLiteral("gpu_low")) {
        m_mode->setCurrentIndex(m_mode->findData(QStringLiteral("gpu")));
        m_gpuLayers->setValue(30); m_batchSize->setValue(128);
        m_microBatchSize->setValue(64); m_contextSize->setValue(1024);
    } else if (preset == QStringLiteral("cpu_8")) {
        m_mode->setCurrentIndex(m_mode->findData(QStringLiteral("cpu")));
        m_gpuLayers->setValue(0); m_threads->setValue(8); m_threadsBatch->setValue(8);
        m_batchSize->setValue(128); m_microBatchSize->setValue(128); m_contextSize->setValue(1024);
    } else if (preset == QStringLiteral("cpu_16")) {
        m_mode->setCurrentIndex(m_mode->findData(QStringLiteral("cpu")));
        m_gpuLayers->setValue(0); m_threads->setValue(16); m_threadsBatch->setValue(16);
        m_batchSize->setValue(128); m_microBatchSize->setValue(128); m_contextSize->setValue(1024);
    }
    updateModeFields();
    save();
    updateCommandPreview();
}

void TranslationSettingsPage::updateLlamaRunButtons()
{
    const bool running = get_llama_runtime().isRunning();
    m_startLlamaButton->setEnabled(!running);
    m_stopLlamaButton->setEnabled(running);
}

void TranslationSettingsPage::startLlamaServer()
{
    save(); updateCommandPreview();
    const QString error = get_llama_runtime().start(settings::translation().llama);
    if (!error.isEmpty())
        QMessageBox::information(this, QStringLiteral("提示"), error);
}

void TranslationSettingsPage::stopLlamaServer()
{
    get_llama_runtime().stop();
}

void TranslationSettingsPage::testLlamaServer()
{
    QUrl endpoint(QStringLiteral("http://%1:%2/v1/models").arg(
        m_host->text().trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : m_host->text().trimmed()).arg(m_port->value()));
    appendLlamaLog(QStringLiteral("检测 /v1/models …"));
    auto *manager = new QNetworkAccessManager(this);
    auto *reply = manager->get(QNetworkRequest(endpoint));
    auto *timeout = new QTimer(reply); timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager] {
        const bool reachable = reply->error() == QNetworkReply::NoError;
        const QString result = reachable ? QStringLiteral("/v1/models 可用")
                                         : QStringLiteral("不可达：") + reply->errorString();
        appendLlamaLog(result);
        reply->deleteLater(); manager->deleteLater();
    });
    timeout->start(5000);
}

void TranslationSettingsPage::testTranslation()
{
    const QString source = m_testInput->toPlainText().trimmed();
    if (source.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先输入测试文本。")); return; }
    save();
    auto *translator = new LlmTranslationService(this);
    m_testOutput->setPlainText(QStringLiteral("翻译中…"));
    connect(translator, &LlmTranslationService::translationFinished, this,
            [this, translator](quint64, const QString &translation, const QString &error) {
                m_testOutput->setPlainText(error.isEmpty() ? translation : QStringLiteral("翻译失败：") + error);
                translator->deleteLater();
            });
    const quint64 requestId = translator->translate(source);
    Q_UNUSED(requestId);
}

void TranslationSettingsPage::browseServerExecutable()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 llama-server"), QString{},
        executableFileFilter());
    if (!path.isEmpty()) { m_serverExecutable->setText(path); save(); updateCommandPreview(); }
}

void TranslationSettingsPage::browseModel()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 GGUF 模型文件"), QString{},
        QStringLiteral("GGUF (*.gguf);;所有文件 (*.*)"));
    if (!path.isEmpty()) { m_modelPath->setText(path); save(); updateCommandPreview(); }
}

DatabaseSettingsPage::DatabaseSettingsPage(QSqlDatabase publicDatabase,
                                           QSqlDatabase privateDatabase,
                                           settings::Paths paths, QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_paths(std::move(paths))
{
}

void DatabaseSettingsPage::lazyLoad()
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new DesignLabel(QStringLiteral("<h3>本地数据库运维</h3>"), this));
    auto *publicVersion = new DesignLabel(
        QStringLiteral("软件所需公共数据库版本：%1  |  当前公共数据库版本：%2")
            .arg(SchemaManager::requiredVersion(DatabaseKind::Public),
                 detectedDatabaseVersion(m_paths.publicDatabase())), this);
    publicVersion->setTextFormat(Qt::PlainText);
    publicVersion->setWordWrap(true);
    layout->addWidget(publicVersion);
    auto *privateVersion = new DesignLabel(
        QStringLiteral("软件所需私有数据库版本：%1  |  当前私有数据库版本：%2")
            .arg(SchemaManager::requiredVersion(DatabaseKind::Private),
                 detectedDatabaseVersion(m_paths.privateDatabase())), this);
    privateVersion->setTextFormat(Qt::PlainText);
    privateVersion->setWordWrap(true);
    layout->addWidget(privateVersion);
    layout->addWidget(new DesignLabel(QStringLiteral("备份、恢复与检查均仅在本机执行。"), this));
    const auto addButton = [this, layout](const QString &text, const QString &toolTip, auto callback) {
        auto *button = new DesignButton(text, this);
        button->setToolTip(toolTip);
        connect(button, &QPushButton::clicked, this, callback);
        layout->addWidget(button);
    };
    addButton(QStringLiteral("数据库清理碎片"), QStringLiteral("先备份公共库与私库，再执行 VACUUM。"), &DatabaseSettingsPage::vacuumDatabases);
    addButton(QStringLiteral("图片数据一致性检查"), QStringLiteral("检查数据库图片记录与本地文件夹；不会删除文件。"), &DatabaseSettingsPage::checkImages);
    addButton(QStringLiteral("全量备份公共数据库"), QStringLiteral("备份公共数据库和封面、剧照、人物图片。"), &DatabaseSettingsPage::createPublicSnapshot);
    addButton(QStringLiteral("全量还原公共数据库"), QStringLiteral("从 meta.json 恢复公共数据库及图片快照。"), &DatabaseSettingsPage::restorePublicSnapshot);
    addButton(QStringLiteral("精简备份公共数据库"), QStringLiteral("仅备份公共数据库 .db 文件。"), [this] { createSimpleBackup(false); });
    addButton(QStringLiteral("精简还原公共数据库"), QStringLiteral("从 .db 文件恢复公共数据库。"), [this] { restoreSimpleBackup(false); });
    addButton(QStringLiteral("备份私有数据库"), QStringLiteral("备份私有数据库 .db 文件。"), [this] { createSimpleBackup(true); });
    addButton(QStringLiteral("还原私有数据库"), QStringLiteral("从 .db 文件恢复私有数据库。"), [this] { restoreSimpleBackup(true); });
    addButton(QStringLiteral("重建私有库与公共库的关联"), QStringLiteral("公共库替换后，按番号和日文名重新建立私库关联。"), &DatabaseSettingsPage::rebuildPrivateLinks);
    layout->addWidget(new DesignLabel(QStringLiteral("<h3>WebDAV 云备份</h3>"), this));
    const CrawlerSettings crawler = settings::crawler(m_paths.settingsFile());
    auto *form = new QFormLayout;
    m_webDavEnabled = new ToggleSwitch(48, 24, nullptr, this); m_webDavEnabled->setChecked(crawler.webDav.enabled);
    m_webDavProfile = new DesignLineEdit(this);
    m_webDavProfile->setText(crawler.webDav.profileName);
    m_webDavBaseUrl = new DesignLineEdit(this);
    m_webDavBaseUrl->setText(crawler.webDav.baseUrl.toString());
    m_webDavRemoteRoot = new DesignLineEdit(this);
    m_webDavRemoteRoot->setText(crawler.webDav.remoteRoot);
    m_webDavTimeout = new TokenSpinBox(this); m_webDavTimeout->setRange(3, 300); m_webDavTimeout->setValue(crawler.webDav.timeoutSeconds);
    m_webDavAutoUpload = new ToggleSwitch(48, 24, nullptr, this); m_webDavAutoUpload->setChecked(crawler.webDav.autoUploadOnBackup);
    m_webDavCredentialStatus = new DesignLabel(QString{}, this);
    form->addRow(new DesignLabel(QStringLiteral("启用 WebDAV"), this), m_webDavEnabled);
    form->addRow(new DesignLabel(QStringLiteral("Profile"), this), m_webDavProfile);
    form->addRow(new DesignLabel(QStringLiteral("Base URL"), this), m_webDavBaseUrl);
    form->addRow(new DesignLabel(QStringLiteral("Remote Root"), this), m_webDavRemoteRoot);
    form->addRow(new DesignLabel(QStringLiteral("超时(秒)"), this), m_webDavTimeout);
    form->addRow(new DesignLabel(QStringLiteral("备份后自动上传"), this), m_webDavAutoUpload);
    form->addRow(new DesignLabel(QStringLiteral("凭据状态"), this), m_webDavCredentialStatus);
    auto *credentials = new QWidget(this); auto *credentialsLayout = new QHBoxLayout(credentials);
    credentials->setProperty("designScrollMenuContent", true);
    credentialsLayout->setContentsMargins(0, 0, 0, 0);
    auto *saveCredentials = new DesignButton(QStringLiteral("保存/更新凭据"), credentials);
    auto *clearCredentials = new DesignButton(QStringLiteral("清除凭据"), credentials);
    credentialsLayout->addWidget(saveCredentials); credentialsLayout->addWidget(clearCredentials); credentialsLayout->addStretch();
    form->addRow(new DesignLabel(QStringLiteral("凭据管理"), this), credentials);
    auto *operations = new QWidget(this); auto *operationsLayout = new QHBoxLayout(operations);
    operations->setProperty("designScrollMenuContent", true);
    operationsLayout->setContentsMargins(0, 0, 0, 0);
    auto *test = new DesignButton(QStringLiteral("测试连接"), operations);
    auto *upload = new DesignButton(QStringLiteral("上传最近一次本地备份"), operations);
    auto *list = new DesignButton(QStringLiteral("浏览云端备份"), operations);
    auto *restore = new DesignButton(QStringLiteral("从云端下载并恢复"), operations);
    operationsLayout->addWidget(test); operationsLayout->addWidget(upload); operationsLayout->addWidget(list); operationsLayout->addWidget(restore); operationsLayout->addStretch();
    form->addRow(new DesignLabel(QStringLiteral("云端操作"), this), operations);
    layout->addLayout(form);
    const auto persist = [this] { saveWebDavSettings(); };
    connect(m_webDavEnabled, &ToggleSwitch::toggled, this, persist);
    connect(m_webDavProfile, &QLineEdit::editingFinished, this, persist);
    connect(m_webDavBaseUrl, &QLineEdit::editingFinished, this, persist);
    connect(m_webDavRemoteRoot, &QLineEdit::editingFinished, this, persist);
    connect(m_webDavTimeout, qOverload<int>(&QSpinBox::valueChanged), this, [persist](int) { persist(); });
    connect(m_webDavAutoUpload, &ToggleSwitch::toggled, this, persist);
    connect(saveCredentials, &QPushButton::clicked, this, &DatabaseSettingsPage::saveWebDavCredentials);
    connect(clearCredentials, &QPushButton::clicked, this, &DatabaseSettingsPage::clearWebDavCredentials);
    connect(test, &QPushButton::clicked, this, &DatabaseSettingsPage::testWebDavConnection);
    connect(upload, &QPushButton::clicked, this, &DatabaseSettingsPage::uploadLatestBackup);
    connect(list, &QPushButton::clicked, this, &DatabaseSettingsPage::listWebDavBackups);
    connect(restore, &QPushButton::clicked, this, &DatabaseSettingsPage::restoreWebDavBackup);
    refreshWebDavCredentialStatus();
    layout->addStretch();
}

void DatabaseSettingsPage::showResult(const QString &title, const DatabaseMaintenanceResult &result) const
{
    const QString message = result.outputPath.isEmpty() ? result.message : result.message + QStringLiteral("\n\n位置：%1").arg(result.outputPath);
    if (result.succeeded) QMessageBox::information(const_cast<DatabaseSettingsPage *>(this), title, message);
    else QMessageBox::critical(const_cast<DatabaseSettingsPage *>(this), title, message);
}

void DatabaseSettingsPage::createPublicSnapshot()
{
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择完整快照保存位置"), m_paths.publicBackupDirectory());
    if (directory.isEmpty()) return;
    const auto result = WebDavBackupService::uploadPublicSnapshot(
        m_publicDatabase, directory, m_paths.workCoverDirectory(), m_paths.fanartDirectory(),
        m_paths.actressImageDirectory(), m_paths.actorImageDirectory(),
        settings::crawler(m_paths.settingsFile()).webDav);
    showResult(QStringLiteral("完整备份"), {.succeeded = result.succeeded,
                                          .message = result.message, .outputPath = result.localPath});
}

void DatabaseSettingsPage::restorePublicSnapshot()
{
    const QString metaPath = QFileDialog::getOpenFileName(this, QStringLiteral("选择快照 meta.json"), m_paths.publicBackupDirectory(), QStringLiteral("JSON 文件 (meta.json)"));
    if (metaPath.isEmpty()) return;
    if (QMessageBox::warning(this, QStringLiteral("确认恢复"), QStringLiteral("这会覆盖公共数据库中的数据，并合并恢复快照图片。是否继续？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    showResult(QStringLiteral("完整恢复"), DatabaseMaintenanceService::restorePublicSnapshot(m_publicDatabase, metaPath, m_paths.workCoverDirectory(), m_paths.fanartDirectory(), m_paths.actressImageDirectory(), m_paths.actorImageDirectory()));
}

void DatabaseSettingsPage::createSimpleBackup(bool privateDatabase)
{
    const QString initialDirectory = privateDatabase ? m_paths.privateBackupDirectory() : m_paths.publicBackupDirectory();
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择备份保存位置"), initialDirectory);
    if (directory.isEmpty()) return;
    const auto result = WebDavBackupService::uploadDatabaseBackup(
        privateDatabase ? m_privateDatabase : m_publicDatabase, directory,
        privateDatabase ? QStringLiteral("darkeye-private") : QStringLiteral("darkeye-public"),
        settings::crawler(m_paths.settingsFile()).webDav);
    if (result.succeeded) QMessageBox::information(this, QStringLiteral("数据库备份"), result.message);
    else QMessageBox::critical(this, QStringLiteral("数据库备份失败"), result.message);
}

void DatabaseSettingsPage::restoreSimpleBackup(bool privateDatabase)
{
    const QString initialDirectory = privateDatabase ? m_paths.privateBackupDirectory() : m_paths.publicBackupDirectory();
    const QString backupPath = QFileDialog::getOpenFileName(this, QStringLiteral("选择数据库备份"), initialDirectory, QStringLiteral("SQLite 数据库 (*.db)"));
    if (backupPath.isEmpty()) return;
    if (QMessageBox::warning(this, QStringLiteral("确认恢复"), QStringLiteral("这会覆盖当前数据库中的数据。是否继续？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    showResult(QStringLiteral("数据库恢复"), DatabaseMaintenanceService::restoreBackup(privateDatabase ? m_privateDatabase : m_publicDatabase, backupPath));
}

void DatabaseSettingsPage::vacuumDatabases()
{
    if (QMessageBox::question(this, QStringLiteral("确认清理"), QStringLiteral("将先创建本地备份，再整理公共库和私库碎片。是否继续？")) != QMessageBox::Yes) return;
    showResult(QStringLiteral("数据库清理"), DatabaseMaintenanceService::backupAndVacuum(m_publicDatabase, m_privateDatabase, m_paths.publicBackupDirectory(), m_paths.privateBackupDirectory()));
}

void DatabaseSettingsPage::checkImages()
{
    const QList<DatabaseMaintenanceResult> results = {
        DatabaseMaintenanceService::checkImageConsistency(m_publicDatabase, m_paths.workCoverDirectory(), QStringLiteral("work"), QStringLiteral("image_url")),
        DatabaseMaintenanceService::checkImageConsistency(m_publicDatabase, m_paths.actressImageDirectory(), QStringLiteral("actress"), QStringLiteral("image_urlA")),
        DatabaseMaintenanceService::checkImageConsistency(m_publicDatabase, m_paths.actorImageDirectory(), QStringLiteral("actor"), QStringLiteral("image_url")),
    };
    QStringList messages;
    bool succeeded = true;
    for (const auto &result : results) { messages.append(result.message); succeeded = succeeded && result.succeeded; }
    showResult(QStringLiteral("图片一致性检查"), {.succeeded = succeeded, .message = messages.join(QLatin1Char('\n'))});
}

void DatabaseSettingsPage::rebuildPrivateLinks()
{
    if (QMessageBox::warning(this, QStringLiteral("确认重建"), QStringLiteral("公共库变更后才需要此操作；它可能会为缺失的番号或女优创建空记录。是否继续？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    auto result = DatabaseMaintenanceService::rebuildPrivateLinks(m_publicDatabase, m_privateDatabase.databaseName());
    if (result.succeeded) result.message += QStringLiteral(" 新增作品 %1 条，新增女优 %2 条。").arg(result.createdWorks).arg(result.createdActresses);
    showResult(QStringLiteral("重建私库关联"), result);
}

void DatabaseSettingsPage::saveWebDavSettings()
{
    CrawlerSettings crawler = settings::crawler(m_paths.settingsFile());
    crawler.webDav.enabled = m_webDavEnabled->isChecked();
    crawler.webDav.profileName = m_webDavProfile->text().trimmed();
    if (crawler.webDav.profileName.isEmpty()) crawler.webDav.profileName = QStringLiteral("default");
    crawler.webDav.baseUrl = QUrl(m_webDavBaseUrl->text().trimmed());
    crawler.webDav.remoteRoot = m_webDavRemoteRoot->text().trimmed();
    crawler.webDav.timeoutSeconds = m_webDavTimeout->value();
    crawler.webDav.autoUploadOnBackup = m_webDavAutoUpload->isChecked();
    settings::saveCrawler(crawler, m_paths.settingsFile());
    refreshWebDavCredentialStatus();
}

void DatabaseSettingsPage::refreshWebDavCredentialStatus()
{
    const QString profile = m_webDavProfile->text().trimmed().isEmpty() ? QStringLiteral("default") : m_webDavProfile->text().trimmed();
    m_webDavCredentialStatus->setText(WebDavCredentialStore::has(profile) ? QStringLiteral("已保存") : QStringLiteral("未保存"));
}

void DatabaseSettingsPage::saveWebDavCredentials()
{
    const QString profile = m_webDavProfile->text().trimmed().isEmpty() ? QStringLiteral("default") : m_webDavProfile->text().trimmed();
    bool accepted = false;
    const QString username = QInputDialog::getText(this, QStringLiteral("保存 WebDAV 凭据"), QStringLiteral("请输入用户名："), QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted) return;
    const QString password = QInputDialog::getText(this, QStringLiteral("保存 WebDAV 凭据"), QStringLiteral("请输入密码："), QLineEdit::Password, {}, &accepted);
    if (!accepted) return;
    QString error;
    if (!WebDavCredentialStore::save(profile, {username, password}, &error)) { QMessageBox::warning(this, QStringLiteral("保存失败"), error); return; }
    refreshWebDavCredentialStatus();
    QMessageBox::information(this, QStringLiteral("保存成功"), QStringLiteral("WebDAV 凭据已写入系统凭据管理器。"));
}

void DatabaseSettingsPage::clearWebDavCredentials()
{
    const QString profile = m_webDavProfile->text().trimmed().isEmpty() ? QStringLiteral("default") : m_webDavProfile->text().trimmed();
    QString error;
    if (!WebDavCredentialStore::clear(profile, &error)) { QMessageBox::warning(this, QStringLiteral("清除失败"), error); return; }
    refreshWebDavCredentialStatus();
    QMessageBox::information(this, QStringLiteral("清除成功"), QStringLiteral("WebDAV 凭据已清除。"));
}

void DatabaseSettingsPage::testWebDavConnection()
{
    saveWebDavSettings();
    const auto result = WebDavBackupService::testConnection(settings::crawler(m_paths.settingsFile()).webDav);
    if (result.succeeded) QMessageBox::information(this, QStringLiteral("连接成功"), result.message);
    else QMessageBox::warning(this, QStringLiteral("连接失败"), result.message);
}

void DatabaseSettingsPage::uploadLatestBackup()
{
    saveWebDavSettings();
    bool accepted = false;
    const auto scope = QInputDialog::getItem(this, QStringLiteral("选择上传对象"), QStringLiteral("请选择要上传的备份："), {QStringLiteral("public"), QStringLiteral("private")}, 0, false, &accepted);
    if (!accepted || scope.isEmpty()) return;
    const bool privateDatabase = scope == QStringLiteral("private");
    const auto backup = DatabaseMaintenanceService::createBackup(privateDatabase ? m_privateDatabase : m_publicDatabase,
                                                                   privateDatabase ? m_paths.privateBackupDirectory() : m_paths.publicBackupDirectory(),
                                                                   privateDatabase ? QStringLiteral("darkeye-private") : QStringLiteral("darkeye-public"));
    if (!backup.succeeded) { showResult(QStringLiteral("上传失败"), backup); return; }
    const auto result = WebDavBackupService::uploadFile(backup.outputPath, settings::crawler(m_paths.settingsFile()).webDav);
    if (result.succeeded) QMessageBox::information(this, QStringLiteral("上传成功"), result.message);
    else QMessageBox::warning(this, QStringLiteral("上传失败"), result.message);
}

void DatabaseSettingsPage::listWebDavBackups()
{
    saveWebDavSettings(); QStringList files;
    const auto result = WebDavBackupService::listBackups(settings::crawler(m_paths.settingsFile()).webDav, &files);
    if (!result.succeeded) { QMessageBox::warning(this, QStringLiteral("列举失败"), result.message); return; }
    QMessageBox::information(this, QStringLiteral("云端备份列表"), files.isEmpty() ? QStringLiteral("云端暂无备份文件。") : result.message + QStringLiteral("\n\n") + files.join(QLatin1Char('\n')));
}

void DatabaseSettingsPage::restoreWebDavBackup()
{
    saveWebDavSettings(); QStringList files;
    const auto settings = darkeye::settings::crawler(m_paths.settingsFile()).webDav;
    const auto listed = WebDavBackupService::listBackups(settings, &files);
    if (!listed.succeeded) { QMessageBox::warning(this, QStringLiteral("获取列表失败"), listed.message); return; }
    bool accepted = false;
    const QString remotePath = QInputDialog::getItem(this, QStringLiteral("选择云端备份"), QStringLiteral("请选择要恢复的备份："), files, 0, false, &accepted);
    if (!accepted || remotePath.isEmpty()) return;
    if (QMessageBox::warning(this, QStringLiteral("确认恢复"), QStringLiteral("是否从云端下载并覆盖现有公共数据库？操作不可撤销！"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const auto result = WebDavBackupService::restoreDatabaseBackup(m_publicDatabase, remotePath, QDir(m_paths.dataDirectory()).filePath(QStringLiteral("temp/webdav_restore")), settings);
    if (result.succeeded) QMessageBox::information(this, QStringLiteral("恢复成功"), result.message);
    else QMessageBox::critical(this, QStringLiteral("恢复失败"), result.message);
}

SettingsPage::SettingsPage(ThemeService &themeService, const QString &shortcutsFile,
                           QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                           settings::Paths paths,
                           QWidget *parent)
    : LazyWidget(parent), m_themeService(themeService), m_shortcutsFile(shortcutsFile),
      m_publicDatabase(std::move(publicDatabase)), m_privateDatabase(std::move(privateDatabase)),
      m_paths(std::move(paths))
{
    setProperty("designScrollMenuContent", true);
}

void SettingsPage::lazyLoad()
{
    m_commonPage = new CommonSettingsPage(m_themeService, this);
    const QList<ModernScrollMenu::Section> sections = {
        {QStringLiteral("常规"), m_commonPage},
        {QStringLiteral("视频"), new VideoSettingsPage(m_publicDatabase, this)},
        {QStringLiteral("NFO"), new NfoSettingsPage(m_publicDatabase, m_paths, this)},
        {QStringLiteral("信息补充器"), new CrawlerSettingsPage(this)},
        {QStringLiteral("翻译"), new TranslationSettingsPage(this)},
        {QStringLiteral("数据库"), new DatabaseSettingsPage(m_publicDatabase, m_privateDatabase, m_paths, this)},
        {QStringLiteral("快捷键"), new ShortcutSettingsPage(m_shortcutsFile, this)},
        {QStringLiteral("关于软件"), new AboutSettingsPage(m_themeService, this, m_paths)},
    };
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new ModernScrollMenu(sections, this));
    if (auto *videoPage = findChild<VideoSettingsPage *>())
    {
        connect(videoPage, &VideoSettingsPage::worksChanged, this, &SettingsPage::worksChanged);
        connect(videoPage, &VideoSettingsPage::quickWorkRequested, this,
                &SettingsPage::quickWorkRequested);
    }
    if (auto *nfoPage = findChild<NfoSettingsPage *>())
    {
        connect(nfoPage, &NfoSettingsPage::referencesChanged, this, &SettingsPage::referencesChanged);
        connect(nfoPage, &NfoSettingsPage::tagsChanged, this, &SettingsPage::tagsChanged);
        connect(nfoPage, &NfoSettingsPage::actressesChanged, this, &SettingsPage::actressesChanged);
        connect(nfoPage, &NfoSettingsPage::actorsChanged, this, &SettingsPage::actorsChanged);
        connect(nfoPage, &NfoSettingsPage::worksChanged, this, &SettingsPage::worksChanged);
    }
}

QComboBox *SettingsPage::themeSelector() const
{
    const_cast<SettingsPage *>(this)->initialize();
    return m_commonPage->themeSelector();
}

} // namespace darkeye
