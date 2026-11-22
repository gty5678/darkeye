#include "ui/pages/SettingsPage.h"
#include "utils/Documentation.h"
#include "ui/pages/NfoImport.h"

#include "darkeye_ui/components/AnimatedIndicators.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/LinkCard.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/theme/ChamferPath.h"
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

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
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

struct SettingsCommand
{
    const char *id;
    QString title;
    const QMetaObject &pageType;
};

const SettingsCommand settingsCommands[] = {
    {"sync_video_paths", QStringLiteral("同步本地视频路径"), VideoSettingsPage::staticMetaObject},
    {"start_collector", QStringLiteral("启动信息服务器（信息补充器）"), CrawlerSettingsPage::staticMetaObject},
    {"start_llama", QStringLiteral("启动 llama.cpp（llama-server）"), TranslationSettingsPage::staticMetaObject},
    {"vacuum_databases", QStringLiteral("数据库清理碎片"), DatabaseSettingsPage::staticMetaObject},
    {"check_images", QStringLiteral("图片数据一致性检查"), DatabaseSettingsPage::staticMetaObject},
    {"backup_public_snapshot", QStringLiteral("全量备份公共数据库"), DatabaseSettingsPage::staticMetaObject},
    {"restore_public_snapshot", QStringLiteral("全量还原公共数据库"), DatabaseSettingsPage::staticMetaObject},
    {"backup_public_database", QStringLiteral("精简备份公共数据库"), DatabaseSettingsPage::staticMetaObject},
    {"restore_public_database", QStringLiteral("精简还原公共数据库"), DatabaseSettingsPage::staticMetaObject},
    {"backup_private_database", QStringLiteral("备份私有数据库"), DatabaseSettingsPage::staticMetaObject},
    {"restore_private_database", QStringLiteral("还原私有数据库"), DatabaseSettingsPage::staticMetaObject},
    {"rebuild_private_links", QStringLiteral("重建私有库与公共库的关联"), DatabaseSettingsPage::staticMetaObject},
    {"test_webdav", QStringLiteral("WebDAV 测试连接"), DatabaseSettingsPage::staticMetaObject},
    {"upload_webdav_backup", QStringLiteral("WebDAV 上传最近一次本地备份"), DatabaseSettingsPage::staticMetaObject},
    {"browse_webdav_backups", QStringLiteral("WebDAV 浏览云端备份"), DatabaseSettingsPage::staticMetaObject},
    {"restore_webdav_backup", QStringLiteral("WebDAV 从云端下载并恢复"), DatabaseSettingsPage::staticMetaObject},
    {"check_update", QStringLiteral("检查更新"), AboutSettingsPage::staticMetaObject},
    {"local_update", QStringLiteral("使用本地安装包更新"), AboutSettingsPage::staticMetaObject},
};

QWidget *operationDialogParent(QWidget *page)
{
    auto *settings = qobject_cast<SettingsDialog *>(page->window());
    if (settings && settings->isHidden() && settings->parentWidget())
        return settings->parentWidget();
    return page;
}

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

QString executableFileFilter()
{
#ifdef Q_OS_WIN
    return QStringLiteral("可执行文件 (*.exe);;所有文件 (*)");
#else
    return QStringLiteral("可执行文件 (*)");
#endif
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

bool startUpdater(QWidget *parent, const QStringList &additionalArguments)
{
    QString error;
    if (UpdateService::launchUpdater(&error, additionalArguments)) return true;
    QMessageBox::critical(parent, QStringLiteral("更新失败"), error);
    return false;
}

void addRightAlignedField(QFormLayout *form, QLabel *label, QWidget *field)
{
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(field);
    form->addRow(label, row);
}

ToggleSwitch *addSwitchRow(QFormLayout *form, DesignLabel *label, ThemeService &themeService)
{
    auto *toggle = new ToggleSwitch(40, 24, &themeService, form->parentWidget());
    toggle->setAccessibleName(label->text());
    label->setBuddy(toggle);
    addRightAlignedField(form, label, toggle);
    return toggle;
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
    layout->setContentsMargins(0, 4, 12, 12);
    layout->setSpacing(20);

    const auto addPanel = [this, layout] {
        auto *panel = new QFrame(this);
        panel->setObjectName(QStringLiteral("AboutSoftwarePanel"));
        panel->setAttribute(Qt::WA_StyledBackground);
        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(16, 14, 16, 14);
        panelLayout->setSpacing(12);
        layout->addWidget(panel);
        return panelLayout;
    };
    const auto sectionTitle = [this](const QString &text) {
        auto *label = new DesignLabel(text, this);
        label->setObjectName(QStringLiteral("AboutSoftwareSectionTitle"));
        return label;
    };

    auto *overview = addPanel();
    auto *identity = new QHBoxLayout;
    identity->setSpacing(14);
    auto *logo = new QLabel(overview->parentWidget());
    logo->setObjectName(QStringLiteral("AboutSoftwareLogo"));
    logo->setFixedSize(64, 64);
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(QIcon(QStringLiteral(":/icons/logo.svg"))
                        .pixmap(logo->size(), logo->devicePixelRatioF()));
    identity->addWidget(logo, 0, Qt::AlignVCenter);
    auto *identityText = new QVBoxLayout;
    identityText->setSpacing(3);
    auto *name = new DesignLabel(QStringLiteral("Darkeye"), overview->parentWidget());
    name->setObjectName(QStringLiteral("AboutSoftwareName"));
    identityText->addWidget(name);
    auto *version = new DesignLabel(
        QStringLiteral("当前版本 %1").arg(QStringLiteral(DARKEYE_VERSION)),
        overview->parentWidget());
    identityText->addWidget(version);
    identity->addLayout(identityText, 1);
    overview->addLayout(identity);

    auto *updateActions = new QHBoxLayout;
    updateActions->setSpacing(8);
    auto *checkUpdate = new DesignButton(QStringLiteral("检查更新"), overview->parentWidget());
    checkUpdate->setVariant(QStringLiteral("primary"));
    checkUpdate->setProperty("settingsCommand", QStringLiteral("check_update"));
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
                QMessageBox::critical(operationDialogParent(this), result.title, result.message);
                return;
            }
            if (!result.updateAvailable) {
                QMessageBox::information(operationDialogParent(this), result.title, result.message);
                return;
            }

            const QString prompt = result.message
                + QStringLiteral("\n\n已检测到新版本。软件将退出以完成更新，是否立即更新？");
            if (QMessageBox::question(operationDialogParent(this), result.title, prompt,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::Yes) != QMessageBox::Yes) {
                QMessageBox::information(operationDialogParent(this), QStringLiteral("已取消更新"),
                                         QStringLiteral("你已取消更新。"));
                return;
            }
            if (!startUpdater(operationDialogParent(this), {})) return;
            QMessageBox::information(operationDialogParent(this), QStringLiteral("开始更新"),
                                     result.message + QStringLiteral("\n\n已启动更新程序，软件即将退出完成更新。"));
            QTimer::singleShot(200, qApp, &QCoreApplication::quit);
        });
        service->check(UpdateService::manifestUrl(),
                       QStringLiteral(DARKEYE_VERSION));
    });
    updateActions->addWidget(checkUpdate);

    auto *localUpdate = new DesignButton(QStringLiteral("使用本地安装包更新…"), overview->parentWidget());
    localUpdate->setProperty("settingsCommand", QStringLiteral("local_update"));
    localUpdate->setToolTip(
        QStringLiteral("选择从 GitHub Release 下载的 zip 或 tar.zst，由更新程序离线安装。"));
    connect(localUpdate, &QPushButton::clicked, this, [this] {
        const QString explanation = QStringLiteral(
            "本地更新不会校验版本号，请确认所选程序更新包适用于当前系统和架构。\n\n"
            "更新包中的程序文件将被覆盖，原有数据、便携配置及包外文件会保留。\n\n确定继续？");
        if (QMessageBox::question(operationDialogParent(this), QStringLiteral("覆盖安装说明"), explanation,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        const QString packagePath = QFileDialog::getOpenFileName(
            operationDialogParent(this), QStringLiteral("选择本地安装包"), {},
            QStringLiteral("安装包 (*.zip *.tar.zst);;ZIP 压缩包 (*.zip);;Zstandard (*.tar.zst);;所有文件 (*.*)"));
        if (packagePath.isEmpty()) return;
        if (!QFileInfo(packagePath).isFile()) {
            QMessageBox::critical(operationDialogParent(this), QStringLiteral("更新失败"), QStringLiteral("所选文件无效。"));
            return;
        }
        const QString prompt = QStringLiteral("将使用以下文件更新：\n%1\n\n软件将退出以完成更新，是否继续？")
                                   .arg(QDir::toNativeSeparators(packagePath));
        if (QMessageBox::question(operationDialogParent(this), QStringLiteral("使用本地安装包更新"), prompt,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            QMessageBox::information(operationDialogParent(this), QStringLiteral("已取消更新"), QStringLiteral("你已取消更新。"));
            return;
        }
        if (!startUpdater(operationDialogParent(this), {QStringLiteral("--local-package"), packagePath})) return;
        QMessageBox::information(operationDialogParent(this), QStringLiteral("开始更新"),
                                 QStringLiteral("已启动更新程序，软件即将退出完成更新。"));
        QTimer::singleShot(200, qApp, &QCoreApplication::quit);
    });

    updateActions->addWidget(localUpdate);
    updateActions->addStretch();
    overview->addLayout(updateActions);

    auto *updateOptions = new QFormLayout;
    overview->addLayout(updateOptions);
    const QString settingsFile = m_runtimePaths.settingsFile();
    auto *automaticUpdate =
        addSwitchRow(updateOptions,
                     new DesignLabel(QStringLiteral("自动检查更新并提醒（每周一次）"),
                                     overview->parentWidget()),
                     m_themeService);
    automaticUpdate->setChecked(settings::app(settingsFile).update.checkAndNotify);
    connect(automaticUpdate, &ToggleSwitch::toggled, this,
            [settingsFile](bool enabled) {
        AppSettings saved = settings::app(settingsFile);
        saved.update.checkAndNotify = enabled;
        settings::saveApp(saved, settingsFile);
    });
    auto *directoryPanel = addPanel();
    directoryPanel->addWidget(sectionTitle(QStringLiteral("运行目录")));
    auto *runtimePaths = new QFormLayout;
    runtimePaths->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    runtimePaths->setRowWrapPolicy(QFormLayout::WrapLongRows);
    runtimePaths->setHorizontalSpacing(16);
    runtimePaths->setVerticalSpacing(8);
    directoryPanel->addLayout(runtimePaths);
    const auto addRuntimePath = [runtimePaths](const QString &title, const QString &path)
    {
        auto *parent = runtimePaths->parentWidget();
        auto *value = new DesignLabel(QDir::toNativeSeparators(QDir(path).absolutePath()), parent);
        value->setTextFormat(Qt::PlainText);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        value->setWordWrap(true);
        runtimePaths->addRow(new DesignLabel(title, parent), value);
    };
    addRuntimePath(QStringLiteral("数据目录（data-dir）"), m_runtimePaths.dataDirectory());
    addRuntimePath(QStringLiteral("配置目录（config）"), m_runtimePaths.configDirectory());
    addRuntimePath(QStringLiteral("知识库目录（avwiki）"), m_runtimePaths.wikiDirectory());
    auto *resources = addPanel();
    auto *resourceActions = new QHBoxLayout;
    resourceActions->setSpacing(8);
    resourceActions->addWidget(sectionTitle(QStringLiteral("项目与支持")), 1);
    const auto addExternalButton = [this, resources, resourceActions](const QString &text, const QString &url)
    {
        auto *button = new DesignButton(text, resources->parentWidget());
        connect(button, &QPushButton::clicked, this,
                [url] { QDesktopServices::openUrl(QUrl(url)); });
        resourceActions->addWidget(button);
    };
    addExternalButton(QStringLiteral("意见反馈"),
                      QStringLiteral("https://github.com/gty5678/darkeye/issues"));
    addExternalButton(QStringLiteral("版本记录"),
                      utils::changelogUrl().toString());
    resources->addLayout(resourceActions);

    auto *links = new QGridLayout;
    links->setHorizontalSpacing(12);
    links->setVerticalSpacing(8);
    links->setColumnStretch(0, 1);
    links->setColumnStretch(1, 1);
    const auto addLinks = [this, resources, links](int column, const QString &heading,
                                                  const QList<std::tuple<QString, QString, QString>> &entries)
    {
        auto *parent = resources->parentWidget();
        links->addWidget(new DesignLabel(heading, parent), 0, column);
        int row = 1;
        for (const auto &[title, description, url] : entries) {
            auto *card = new TokenLinkCard(title, description, url, &m_themeService, parent);
            card->setMinimumWidth(0);
            card->setMaximumWidth(QWIDGETSIZE_MAX);
            card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            links->addWidget(card, row++, column);
        }
    };
    const QList<std::tuple<QString, QString, QString>> projects = {
        {QStringLiteral("GitHub"), QStringLiteral("源代码仓库与问题反馈"),
         utils::projectRepositoryUrl().toString()},
        {QStringLiteral("Discord"), QStringLiteral("社区讨论与支持频道"),
         QStringLiteral("https://discord.gg/N7wJVNVA")},
        {QStringLiteral("官网"), QStringLiteral("产品介绍与主页"),
         QStringLiteral("https://gty5678.github.io/darkeye-webpage/")},
        {QStringLiteral("Mosaic 桥接器"), QStringLiteral("信息获取服务与桥接器"),
         QStringLiteral("https://github.com/gty5678/mosaic.git")},
    };
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
    addLinks(0, QStringLiteral("项目链接"), projects);
    addLinks(1, QStringLiteral("参考项目"), references);
    resources->addLayout(links);
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
    match->setProperty("settingsCommand", QStringLiteral("sync_video_paths"));
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
    QMessageBox::warning(operationDialogParent(this), QStringLiteral("无法提取番号"),
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
        QMessageBox::information(operationDialogParent(this), QStringLiteral("提示"),
                                 QStringLiteral("请先在设置的「视频」页配置至少一个视频文件夹路径"));
        return;
    }
    const VideoLibraryScanResult result =
        VideoLibraryService(m_publicDatabase).synchronizeVideoUrls(paths);
    if (!result.succeeded)
    {
        QMessageBox::critical(operationDialogParent(this), QStringLiteral("同步失败"), result.errorMessage);
        return;
    }
    showFilesWithoutSerial(result.filesWithoutSerial);
    if (result.updatedWorks > 0) emit worksChanged();
    QMessageBox::information(
        operationDialogParent(this), QStringLiteral("完成"),
        QStringLiteral("共扫描 %1 个视频文件；无法提取番号 %2 个；"
                       "番号在库中无匹配 %3 个；已更新/清理 %4 条作品的 video_url。")
            .arg(result.scannedFiles).arg(result.filesWithoutSerial.size())
            .arg(result.unmatchedSerials).arg(result.updatedWorks));
}

ShortcutSettingsPage::ShortcutSettingsPage(const QString &shortcutsFile, const QActionGroup &actions,
                                           QWidget *parent)
    : LazyWidget(parent), m_shortcutsFile(shortcutsFile), m_actions(actions)
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

    auto *layout = new QVBoxLayout(this);
    for (auto *action : m_actions.actions())
    {
        auto *row = new QWidget(this);
        row->setProperty("designScrollMenuContent", true);
        row->setObjectName(QStringLiteral("ShortcutSettingRow_%1").arg(action->objectName()));
        auto *rowLayout = new QHBoxLayout(row);
        auto *label = new DesignLabel(action->text(), row);
        label->setMinimumWidth(154);
        auto *editor = new TokenKeySequenceEdit(row);
        editor->setFixedWidth(150);
        editor->setKeySequence(action->shortcut());
        auto *reset = new DesignButton(QStringLiteral("恢复"), row);
        rowLayout->addWidget(label);
        rowLayout->addStretch();
        rowLayout->addWidget(editor);
        rowLayout->addWidget(reset);
        layout->addWidget(row);
        connect(editor, &QKeySequenceEdit::editingFinished, this,
                [this, action, editor] {
                    const auto shortcut = editor->keySequence();
                    if (shortcut == action->shortcut()) return;
                    if (saveShortcut(action->objectName(), shortcut.toString(QKeySequence::PortableText)))
                        action->setShortcut(shortcut);
                    else
                        editor->setKeySequence(action->shortcut());
                });
        connect(reset, &QPushButton::clicked, this,
                [this, action, editor] {
                    if (saveShortcut(action->objectName(), QJsonValue::Undefined)) {
                        action->setShortcut(action->data().value<QKeySequence>());
                        editor->setKeySequence(action->shortcut());
                    }
                });
    }
    layout->addStretch();
    layout->addWidget(
        new DesignLabel(QStringLiteral("<small>配置保存位置：%1</small>").arg(m_shortcutsFile.toHtmlEscaped()), this));
}

bool ShortcutSettingsPage::saveShortcut(const QString &actionId, const QJsonValue &value)
{
    auto shortcuts = m_userShortcuts;
    shortcuts.insert(actionId, value);
    const auto contents = QJsonDocument(shortcuts).toJson(QJsonDocument::Indented);
    QSaveFile file(m_shortcutsFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("快捷键保存失败"), file.errorString());
        return false;
    }
    m_userShortcuts = std::move(shortcuts);
    return true;
}

CommonSettingsPage::CommonSettingsPage(ThemeService &themeService,
                                       QWidget *parent, QString settingsFile)
    : LazyWidget(parent), m_themeService(themeService), m_settingsFile(std::move(settingsFile))
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
    m_colorPicker = new ColorPicker(QColor(initialPrimary), false, ColorPicker::Shape::Octagon,
                                    m_primaryColorRow);
    primaryLayout->addStretch();
    primaryLayout->addWidget(m_colorPicker);

    m_themeSelector = new DesignComboBox(this);
    m_themeSelector->setAccessibleName(QStringLiteral("主题"));

    layout->addRow(new DesignLabel(QStringLiteral("主色"), this), m_primaryColorRow);
    layout->addRow(new DesignLabel(QStringLiteral("主题"), this), m_themeSelector);

    const auto appSettings = settings::app(m_settingsFile);
    auto *navigationBar =
        addSwitchRow(layout, new DesignLabel(QStringLiteral("显示左侧页面导航栏"), this),
                     m_themeService);
    navigationBar->setObjectName(QStringLiteral("navigationBarVisible"));
    navigationBar->setChecked(appSettings.navigationBarVisible);
    connect(navigationBar, &ToggleSwitch::toggled, this, [this](bool visible) {
        auto appSettings = settings::app(m_settingsFile);
        appSettings.navigationBarVisible = visible;
        settings::saveApp(appSettings, m_settingsFile);
        emit navigationBarVisibilityChanged(visible);
    });

    const auto addSidebarToggleSetting = [&](const QString &label, const QString &name,
                                             bool AppSettings::*member) {
        auto *toggle = addSwitchRow(layout, new DesignLabel(label, this), m_themeService);
        toggle->setObjectName(name);
        toggle->setChecked(appSettings.*member);
        connect(toggle, &ToggleSwitch::toggled, this, [this, member](bool visible) {
            auto appSettings = settings::app(m_settingsFile);
            appSettings.*member = visible;
            settings::saveApp(appSettings, m_settingsFile);
            emit sidebarToggleVisibilityChanged(appSettings.leftSidebarToggleVisible,
                                                appSettings.rightSidebarToggleVisible);
        });
    };
    addSidebarToggleSetting(QStringLiteral("显示左边栏展开／收起按钮"),
                            QStringLiteral("leftSidebarToggleVisible"), &AppSettings::leftSidebarToggleVisible);
    addSidebarToggleSetting(QStringLiteral("显示右边栏展开／收起按钮"),
                            QStringLiteral("rightSidebarToggleVisible"), &AppSettings::rightSidebarToggleVisible);

    auto *autoCollapse =
        addSwitchRow(layout, new DesignLabel(QStringLiteral("焦点离开边栏时自动收起"), this),
                     m_themeService);
    autoCollapse->setObjectName(QStringLiteral("sidebarAutoCollapse"));
    autoCollapse->setChecked(appSettings.sidebarAutoCollapse);
    connect(autoCollapse, &ToggleSwitch::toggled, this, [this](bool enabled) {
        auto appSettings = settings::app(m_settingsFile);
        appSettings.sidebarAutoCollapse = enabled;
        settings::saveApp(appSettings, m_settingsFile);
        emit sidebarAutoCollapseChanged(enabled);
    });

    auto *greenCover =
        addSwitchRow(layout, new DesignLabel(QStringLiteral("初始过滤器添加绿色封面"), this),
                     m_themeService);
    greenCover->setObjectName(QStringLiteral("workFilterAddGreenCover"));
    greenCover->setToolTip(QStringLiteral("下次初始化作品筛选面板时生效"));
    greenCover->setChecked(appSettings.workFilterAddGreenCover);
    connect(greenCover, &ToggleSwitch::toggled, this, [this](bool enabled) {
        auto appSettings = settings::app(m_settingsFile);
        appSettings.workFilterAddGreenCover = enabled;
        settings::saveApp(appSettings, m_settingsFile);
    });

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
    if (QColor(color) == QColor(m_themeService.currentTokens().primary)) return;
    if (!m_themeService.setTheme(m_themeService.current(), color)) return;
    AppSettings appSettings = settings::app();
    appSettings.customPrimary = m_themeService.customPrimary();
    settings::saveApp(appSettings);
}

CrawlerSettingsPage::CrawlerSettingsPage(ThemeService &themeService, QWidget *parent)
    : LazyWidget(parent), m_themeService(themeService)
{
}

void CrawlerSettingsPage::lazyLoad()
{
    const CrawlerSettings values = settings::crawler();
    auto *layout = new QVBoxLayout(this);
    const auto wrappingLabel = [this](const QString &text) {
        auto *label = new DesignLabel(text, this);
        label->setWordWrap(true);
        return label;
    };
    layout->addWidget(wrappingLabel(QStringLiteral("<h3>信息补充器相关设置</h3>")));
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

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
                 wrappingLabel(QStringLiteral("作品/女优为完整前缀，程序会追加 /{serial} 或 /{name}；"
                                              "图片下载、热门女优请填写完整地址。")));

    m_collectorExecutable = new DesignLineEdit(this);
    m_collectorExecutable->setText(values.collectorExecutable);
    m_collectorExecutable->setPlaceholderText(QStringLiteral("可选：信息补充器的可执行文件"));
    m_collectorExecutable->setClearButtonEnabled(true);
    auto *collectorRow = new QHBoxLayout;
    collectorRow->addWidget(m_collectorExecutable, 1);
    auto *browse = new DesignButton(QStringLiteral("浏览…"), this);
    collectorRow->addWidget(browse);
    form->addRow(wrappingLabel(QStringLiteral("信息补充器可执行文件")), collectorRow);
    m_autoStartCollector = new ToggleSwitch(40, 24, &m_themeService, this);
    m_autoStartCollector->setChecked(values.autoStartCollector);
    addRightAlignedField(form, wrappingLabel(QStringLiteral("打开软件自动启动信息补充器")),
                         m_autoStartCollector);
    auto *controls = new QWidget(this);
    controls->setProperty("designScrollMenuContent", true);
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    auto *start = new DesignButton(QStringLiteral("启动"), controls);
    auto *test = new DesignButton(QStringLiteral("测试"), controls);
    start->setProperty("settingsCommand", QStringLiteral("start_collector"));
    start->setToolTip(QStringLiteral("独立启动信息补充器；程序退出时不会结束该进程。"));
    test->setToolTip(QStringLiteral("请求采集器的 /api/v1/exist 接口验证连通性。"));
    controlsLayout->addWidget(start);
    controlsLayout->addWidget(test);
    controlsLayout->addStretch();
    form->addRow(new DesignLabel(QStringLiteral("信息服务器控制"), this), controls);
    m_collectorStatus = wrappingLabel(QStringLiteral("状态：未检测"));
    m_collectorStatus->setTextFormat(Qt::PlainText);
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
    if (!m_collectorStatus) return;

    // Network errors can include URLs or hostnames without any spaces.
    static const QRegularExpression longToken(QStringLiteral("([!-~]{20})(?=[!-~])"));
    QString displayStatus = status;
    displayStatus.replace(longToken, QStringLiteral("\\1\u200B"));
    m_collectorStatus->setText(QStringLiteral("状态：") + displayStatus);
}

void CrawlerSettingsPage::startCollector()
{
    save();
    const QString executable = m_collectorExecutable->text().trimmed();
    if (executable.isEmpty()) {
        QMessageBox::information(operationDialogParent(this), QStringLiteral("提示"), QStringLiteral("请先选择信息补充器可执行文件。"));
        return;
    }
    if (!QFileInfo::exists(executable) || !QProcess::startDetached(executable, {}, QFileInfo(executable).absolutePath())) {
        setCollectorStatus(QStringLiteral("启动失败"));
        QMessageBox::warning(operationDialogParent(this), QStringLiteral("无法启动"), QStringLiteral("无法启动信息补充器：%1").arg(executable));
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

TranslationSettingsPage::TranslationSettingsPage(ThemeService &themeService, QWidget *parent)
    : LazyWidget(parent), m_themeService(themeService)
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
                 utils::documentationUrl(QStringLiteral("usage/#llamacpp")).toString());
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
    m_mlock = new ToggleSwitch(40, 24, &m_themeService, this);
    m_autoSync = new ToggleSwitch(40, 24, &m_themeService, this);
    m_autoStart = new ToggleSwitch(40, 24, &m_themeService, this);
    m_mlock->setChecked(values.llama.mlock);
    m_autoSync->setChecked(values.llama.autoSyncTranslation);
    m_autoStart->setChecked(values.llama.autoStart);
    addRightAlignedField(llamaForm, new DesignLabel(QStringLiteral("mlock"), this), m_mlock);
    addRightAlignedField(llamaForm, new DesignLabel(QStringLiteral("自动回填翻译配置"), this), m_autoSync);
    addRightAlignedField(llamaForm, new DesignLabel(QStringLiteral("打开软件自动启动"), this), m_autoStart);
    m_commandPreview = new DesignPlainTextEdit(this);
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setFixedHeight(72);
    llamaForm->addRow(new DesignLabel(QStringLiteral("命令预览"), this), m_commandPreview);
    auto *controls = new QWidget(this);
    controls->setProperty("designScrollMenuContent", true);
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    m_startLlamaButton = new DesignButton(QStringLiteral("启动 llama-server"), controls);
    m_startLlamaButton->setProperty("settingsCommand", QStringLiteral("start_llama"));
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
        QMessageBox::information(operationDialogParent(this), QStringLiteral("提示"), error);
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
                                           settings::Paths paths, ThemeService &themeService,
                                           QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_paths(std::move(paths)),
      m_themeService(themeService)
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
    const auto addButton = [this, layout](const QString &id, const QString &text,
                                          const QString &toolTip, auto callback) {
        auto *button = new DesignButton(text, this);
        button->setProperty("settingsCommand", id);
        button->setToolTip(toolTip);
        connect(button, &QPushButton::clicked, this, callback);
        layout->addWidget(button);
    };
    addButton(QStringLiteral("vacuum_databases"), QStringLiteral("数据库清理碎片"), QStringLiteral("先备份公共库与私库，再执行 VACUUM。"), &DatabaseSettingsPage::vacuumDatabases);
    addButton(QStringLiteral("check_images"), QStringLiteral("图片数据一致性检查"), QStringLiteral("检查数据库图片记录与本地文件夹；不会删除文件。"), &DatabaseSettingsPage::checkImages);
    addButton(QStringLiteral("backup_public_snapshot"), QStringLiteral("全量备份公共数据库"), QStringLiteral("备份公共数据库和封面、剧照、人物图片。"), &DatabaseSettingsPage::createPublicSnapshot);
    addButton(QStringLiteral("restore_public_snapshot"), QStringLiteral("全量还原公共数据库"), QStringLiteral("从 meta.json 恢复公共数据库及图片快照。"), &DatabaseSettingsPage::restorePublicSnapshot);
    addButton(QStringLiteral("backup_public_database"), QStringLiteral("精简备份公共数据库"), QStringLiteral("仅备份公共数据库 .db 文件。"), [this] { createSimpleBackup(false); });
    addButton(QStringLiteral("restore_public_database"), QStringLiteral("精简还原公共数据库"), QStringLiteral("从 .db 文件恢复公共数据库。"), [this] { restoreSimpleBackup(false); });
    addButton(QStringLiteral("backup_private_database"), QStringLiteral("备份私有数据库"), QStringLiteral("备份私有数据库 .db 文件。"), [this] { createSimpleBackup(true); });
    addButton(QStringLiteral("restore_private_database"), QStringLiteral("还原私有数据库"), QStringLiteral("从 .db 文件恢复私有数据库。"), [this] { restoreSimpleBackup(true); });
    addButton(QStringLiteral("rebuild_private_links"), QStringLiteral("重建私有库与公共库的关联"), QStringLiteral("公共库替换后，按番号和日文名重新建立私库关联。"), &DatabaseSettingsPage::rebuildPrivateLinks);
    layout->addWidget(new DesignLabel(QStringLiteral("<h3>WebDAV 云备份</h3>"), this));
    const CrawlerSettings crawler = settings::crawler(m_paths.settingsFile());
    auto *form = new QFormLayout;
    m_webDavEnabled = new ToggleSwitch(40, 24, &m_themeService, this); m_webDavEnabled->setChecked(crawler.webDav.enabled);
    m_webDavProfile = new DesignLineEdit(this);
    m_webDavProfile->setText(crawler.webDav.profileName);
    m_webDavBaseUrl = new DesignLineEdit(this);
    m_webDavBaseUrl->setText(crawler.webDav.baseUrl.toString());
    m_webDavRemoteRoot = new DesignLineEdit(this);
    m_webDavRemoteRoot->setText(crawler.webDav.remoteRoot);
    m_webDavTimeout = new TokenSpinBox(this); m_webDavTimeout->setRange(3, 300); m_webDavTimeout->setValue(crawler.webDav.timeoutSeconds);
    m_webDavAutoUpload = new ToggleSwitch(40, 24, &m_themeService, this); m_webDavAutoUpload->setChecked(crawler.webDav.autoUploadOnBackup);
    m_webDavCredentialStatus = new DesignLabel(QString{}, this);
    addRightAlignedField(form, new DesignLabel(QStringLiteral("启用 WebDAV"), this), m_webDavEnabled);
    form->addRow(new DesignLabel(QStringLiteral("Profile"), this), m_webDavProfile);
    form->addRow(new DesignLabel(QStringLiteral("Base URL"), this), m_webDavBaseUrl);
    form->addRow(new DesignLabel(QStringLiteral("Remote Root"), this), m_webDavRemoteRoot);
    form->addRow(new DesignLabel(QStringLiteral("超时(秒)"), this), m_webDavTimeout);
    addRightAlignedField(form, new DesignLabel(QStringLiteral("备份后自动上传"), this), m_webDavAutoUpload);
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
    test->setProperty("settingsCommand", QStringLiteral("test_webdav"));
    auto *upload = new DesignButton(QStringLiteral("上传最近一次本地备份"), operations);
    upload->setProperty("settingsCommand", QStringLiteral("upload_webdav_backup"));
    auto *list = new DesignButton(QStringLiteral("浏览云端备份"), operations);
    list->setProperty("settingsCommand", QStringLiteral("browse_webdav_backups"));
    auto *restore = new DesignButton(QStringLiteral("从云端下载并恢复"), operations);
    restore->setProperty("settingsCommand", QStringLiteral("restore_webdav_backup"));
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
    auto *parent = operationDialogParent(const_cast<DatabaseSettingsPage *>(this));
    if (result.succeeded) QMessageBox::information(parent, title, message);
    else QMessageBox::critical(parent, title, message);
}

void DatabaseSettingsPage::createPublicSnapshot()
{
    const QString directory = QFileDialog::getExistingDirectory(operationDialogParent(this), QStringLiteral("选择完整快照保存位置"), m_paths.publicBackupDirectory());
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
    const QString metaPath = QFileDialog::getOpenFileName(operationDialogParent(this), QStringLiteral("选择快照 meta.json"), m_paths.publicBackupDirectory(), QStringLiteral("JSON 文件 (meta.json)"));
    if (metaPath.isEmpty()) return;
    if (QMessageBox::warning(operationDialogParent(this), QStringLiteral("确认恢复"), QStringLiteral("这会覆盖公共数据库中的数据，并合并恢复快照图片。是否继续？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    showResult(QStringLiteral("完整恢复"), DatabaseMaintenanceService::restorePublicSnapshot(m_publicDatabase, metaPath, m_paths.workCoverDirectory(), m_paths.fanartDirectory(), m_paths.actressImageDirectory(), m_paths.actorImageDirectory()));
}

void DatabaseSettingsPage::createSimpleBackup(bool privateDatabase)
{
    const QString initialDirectory = privateDatabase ? m_paths.privateBackupDirectory() : m_paths.publicBackupDirectory();
    const QString directory = QFileDialog::getExistingDirectory(operationDialogParent(this), QStringLiteral("选择备份保存位置"), initialDirectory);
    if (directory.isEmpty()) return;
    const auto result = WebDavBackupService::uploadDatabaseBackup(
        privateDatabase ? m_privateDatabase : m_publicDatabase, directory,
        privateDatabase ? QStringLiteral("darkeye-private") : QStringLiteral("darkeye-public"),
        settings::crawler(m_paths.settingsFile()).webDav);
    if (result.succeeded) QMessageBox::information(operationDialogParent(this), QStringLiteral("数据库备份"), result.message);
    else QMessageBox::critical(operationDialogParent(this), QStringLiteral("数据库备份失败"), result.message);
}

void DatabaseSettingsPage::restoreSimpleBackup(bool privateDatabase)
{
    const QString initialDirectory = privateDatabase ? m_paths.privateBackupDirectory() : m_paths.publicBackupDirectory();
    const QString backupPath = QFileDialog::getOpenFileName(operationDialogParent(this), QStringLiteral("选择数据库备份"), initialDirectory, QStringLiteral("SQLite 数据库 (*.db)"));
    if (backupPath.isEmpty()) return;
    if (QMessageBox::warning(operationDialogParent(this), QStringLiteral("确认恢复"), QStringLiteral("这会覆盖当前数据库中的数据。是否继续？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    showResult(QStringLiteral("数据库恢复"), DatabaseMaintenanceService::restoreBackup(privateDatabase ? m_privateDatabase : m_publicDatabase, backupPath));
}

void DatabaseSettingsPage::vacuumDatabases()
{
    if (QMessageBox::question(operationDialogParent(this), QStringLiteral("确认清理"), QStringLiteral("将先创建本地备份，再整理公共库和私库碎片。是否继续？")) != QMessageBox::Yes) return;
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
    if (QMessageBox::warning(operationDialogParent(this), QStringLiteral("确认重建"), QStringLiteral("公共库变更后才需要此操作；它可能会为缺失的番号或女优创建空记录。是否继续？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
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
    if (result.succeeded) QMessageBox::information(operationDialogParent(this), QStringLiteral("连接成功"), result.message);
    else QMessageBox::warning(operationDialogParent(this), QStringLiteral("连接失败"), result.message);
}

void DatabaseSettingsPage::uploadLatestBackup()
{
    saveWebDavSettings();
    bool accepted = false;
    const auto scope = QInputDialog::getItem(operationDialogParent(this), QStringLiteral("选择上传对象"), QStringLiteral("请选择要上传的备份："), {QStringLiteral("public"), QStringLiteral("private")}, 0, false, &accepted);
    if (!accepted || scope.isEmpty()) return;
    const bool privateDatabase = scope == QStringLiteral("private");
    const auto backup = DatabaseMaintenanceService::createBackup(privateDatabase ? m_privateDatabase : m_publicDatabase,
                                                                   privateDatabase ? m_paths.privateBackupDirectory() : m_paths.publicBackupDirectory(),
                                                                   privateDatabase ? QStringLiteral("darkeye-private") : QStringLiteral("darkeye-public"));
    if (!backup.succeeded) { showResult(QStringLiteral("上传失败"), backup); return; }
    const auto result = WebDavBackupService::uploadFile(backup.outputPath, settings::crawler(m_paths.settingsFile()).webDav);
    if (result.succeeded) QMessageBox::information(operationDialogParent(this), QStringLiteral("上传成功"), result.message);
    else QMessageBox::warning(operationDialogParent(this), QStringLiteral("上传失败"), result.message);
}

void DatabaseSettingsPage::listWebDavBackups()
{
    saveWebDavSettings(); QStringList files;
    const auto result = WebDavBackupService::listBackups(settings::crawler(m_paths.settingsFile()).webDav, &files);
    if (!result.succeeded) { QMessageBox::warning(operationDialogParent(this), QStringLiteral("列举失败"), result.message); return; }
    QMessageBox::information(operationDialogParent(this), QStringLiteral("云端备份列表"), files.isEmpty() ? QStringLiteral("云端暂无备份文件。") : result.message + QStringLiteral("\n\n") + files.join(QLatin1Char('\n')));
}

void DatabaseSettingsPage::restoreWebDavBackup()
{
    saveWebDavSettings(); QStringList files;
    const auto settings = darkeye::settings::crawler(m_paths.settingsFile()).webDav;
    const auto listed = WebDavBackupService::listBackups(settings, &files);
    if (!listed.succeeded) { QMessageBox::warning(operationDialogParent(this), QStringLiteral("获取列表失败"), listed.message); return; }
    bool accepted = false;
    const QString remotePath = QInputDialog::getItem(operationDialogParent(this), QStringLiteral("选择云端备份"), QStringLiteral("请选择要恢复的备份："), files, 0, false, &accepted);
    if (!accepted || remotePath.isEmpty()) return;
    if (QMessageBox::warning(operationDialogParent(this), QStringLiteral("确认恢复"), QStringLiteral("是否从云端下载并覆盖现有公共数据库？操作不可撤销！"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const auto result = WebDavBackupService::restoreDatabaseBackup(m_publicDatabase, remotePath, QDir(m_paths.dataDirectory()).filePath(QStringLiteral("temp/webdav_restore")), settings);
    if (result.succeeded) QMessageBox::information(operationDialogParent(this), QStringLiteral("恢复成功"), result.message);
    else QMessageBox::critical(operationDialogParent(this), QStringLiteral("恢复失败"), result.message);
}

SettingsDialog::SettingsDialog(ThemeService &themeService, const QActionGroup &shortcutActions,
                               QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                               settings::Paths paths, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint), m_themeService(themeService)
{
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(QStringLiteral("设置"));
    setAttribute(Qt::WA_TranslucentBackground);
    connect(&m_themeService, &ThemeService::themeChanged, this, [this] { update(); });
    const QSize screenSize = (parent ? parent->screen() : screen())->availableGeometry().size();
    setFixedWidth(qMin(920, screenSize.width() - 32));
    resize(width(), qMin(720, screenSize.height() - 48));

    m_commonPage = new CommonSettingsPage(themeService, this, paths.settingsFile());
    connect(m_commonPage, &CommonSettingsPage::navigationBarVisibilityChanged,
            this, &SettingsDialog::navigationBarVisibilityChanged);
    connect(m_commonPage, &CommonSettingsPage::sidebarAutoCollapseChanged,
            this, &SettingsDialog::sidebarAutoCollapseChanged);
    connect(m_commonPage, &CommonSettingsPage::sidebarToggleVisibilityChanged,
            this, &SettingsDialog::sidebarToggleVisibilityChanged);
    auto *videoPage = new VideoSettingsPage(publicDatabase, this);
    auto *nfoPage = new NfoSettingsPage(publicDatabase, paths, this);
    auto *crawlerPage = new CrawlerSettingsPage(themeService, this);
    struct Section {
        QString title;
        LazyWidget *page;
        QStringList searchLabels;
    };
    QStringList shortcutLabels;
    for (const auto *action : shortcutActions.actions())
        shortcutLabels.append(action->text());

    const QList<Section> sections = {
        {QStringLiteral("常规"), m_commonPage, {QStringLiteral("主题"), QStringLiteral("主色"),
         QStringLiteral("显示左侧页面导航栏"), QStringLiteral("显示左边栏展开／收起按钮"),
         QStringLiteral("显示右边栏展开／收起按钮"), QStringLiteral("焦点离开边栏时自动收起"), QStringLiteral("初始过滤器添加绿色封面")}},
        {QStringLiteral("视频"), videoPage,
         QStringLiteral("本地播放器（可选）：|视频文件夹路径管理：|扫描本地视频提取番号并录入数据库|同步作品本地视频路径").split(u'|')},
        {QStringLiteral("NFO"), nfoPage,
         QStringLiteral("从视频路径扫描并导入 Jvedio NFO|从文件夹导入 Jvedio NFO|从 Jvedio NFO 导入作品|"
                        "从视频路径扫描并导入 MDCZ NFO|从文件夹导入 MDCZ NFO|从 MDCZ NFO 导入作品").split(u'|')},
        {QStringLiteral("信息补充器"), crawlerPage,
         QStringLiteral("作品 API 前缀|女优 API 前缀|图片下载 API|热门女优 API|信息补充器可执行文件|"
                        "打开软件自动启动信息补充器|信息服务器控制|运行状态").split(u'|')},
        {QStringLiteral("翻译"), new TranslationSettingsPage(themeService, this),
         QStringLiteral("翻译引擎|模型|Base URL|API Key|超时（秒）|重试次数|失败回退|测试输入|测试输出|"
                        "测试翻译|llama-server|GGUF 模型|监听地址|运行模式|参数预设|上下文大小|GPU layers|"
                        "threads|threads-batch|batch-size|ubatch-size|mlock|自动回填翻译配置|打开软件自动启动|"
                        "命令预览|运行状态|日志输出").split(u'|')},
        {QStringLiteral("数据库"), new DatabaseSettingsPage(publicDatabase, privateDatabase, paths, themeService, this),
         QStringLiteral("数据库清理碎片|图片数据一致性检查|全量备份公共数据库|全量还原公共数据库|精简备份公共数据库|"
                        "精简还原公共数据库|备份私有数据库|还原私有数据库|重建私有库与公共库的关联|启用 WebDAV|"
                        "Profile|Base URL|Remote Root|超时(秒)|备份后自动上传|凭据管理|云端操作").split(u'|')},
        {QStringLiteral("快捷键"), new ShortcutSettingsPage(paths.shortcutsFile(), shortcutActions, this), shortcutLabels},
        {QStringLiteral("关于软件"), new AboutSettingsPage(themeService, this, paths),
         QStringLiteral("检查更新|使用本地安装包更新…|自动检查更新并提醒（每周一次）|意见反馈|"
                        "版本记录|数据目录（data-dir）|配置目录（config）|知识库目录（avwiki）|项目链接|参考项目").split(u'|')},
    };

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(20);

    auto *sidebar = new QWidget(this);
    sidebar->setFixedWidth(176);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    auto *search = new DesignLineEdit(sidebar);
    search->setAccessibleName(QStringLiteral("搜索设置"));
    search->setPlaceholderText(QStringLiteral("搜索设置"));
    search->setClearButtonEnabled(true);
    sidebarLayout->addWidget(search);

    auto *menu = new QListWidget(sidebar);
    menu->setObjectName(QStringLiteral("DesignListWidget"));
    menu->setAccessibleName(QStringLiteral("设置分类与搜索结果"));
    menu->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarLayout->addWidget(menu, 1);
    auto *empty = new DesignLabel(QStringLiteral("未找到匹配的设置"), sidebar);
    empty->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(empty, 1);
    layout->addWidget(sidebar);

    auto *content = new QVBoxLayout;
    auto *title = new DesignLabel(sections.first().title, this);
    title->setObjectName(QStringLiteral("DesignModalTitle"));
    auto *closeButton = new IconButton(QStringLiteral("x"), QStringLiteral("关闭设置"),
                                       &themeService, this);
    closeButton->setButtonPixelSize(28);
    closeButton->setIconPixelSize(20);
    closeButton->setAutoDefault(false);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    auto *heading = new QHBoxLayout;
    heading->addWidget(title, 1);
    heading->addWidget(closeButton, 0, Qt::AlignTop | Qt::AlignRight);
    content->addLayout(heading);
    auto *stack = new QStackedWidget(this);
    for (const auto &section : sections) {
        auto *area = new QScrollArea(stack);
        area->setObjectName(QStringLiteral("DesignScrollMenuArea"));
        area->viewport()->setObjectName(QStringLiteral("DesignScrollMenuViewport"));
        area->setFrameShape(QFrame::NoFrame);
        area->setWidgetResizable(true);
        if (section.page == crawlerPage)
            area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget;
        body->setProperty("designScrollMenuContent", true);
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        bodyLayout->setSizeConstraint(QLayout::SetMinimumSize);
        bodyLayout->addWidget(section.page);
        bodyLayout->addStretch();
        section.page->setProperty("designScrollMenuContent", true);
        area->setWidget(body);
        stack->addWidget(area);
    }
    content->addWidget(stack, 1);
    layout->addLayout(content, 1);

    const auto populate = [menu, empty, stack, sections](const QString &text) {
        menu->clear();
        const auto query = text.simplified();
        const auto words = query.split(u' ');
        for (int index = 0; index < sections.size(); ++index) {
            const auto &section = sections.at(index);
            const auto addItem = [menu, index](const QString &caption, const QString &anchor = {}) {
                auto *item = new QListWidgetItem(caption, menu);
                item->setData(Qt::UserRole, index);
                item->setData(Qt::UserRole + 1, anchor);
                item->setToolTip(caption);
                item->setSizeHint(QSize(0, 34));
            };
            if (query.isEmpty() || section.title.contains(query, Qt::CaseInsensitive))
                addItem(section.title);
            if (query.isEmpty()) continue;
            for (const auto &label : section.searchLabels) {
                const auto searchable = section.title + u' ' + label;
                if (std::ranges::all_of(words, [&searchable](const QString &word) {
                        return searchable.contains(word, Qt::CaseInsensitive);
                    }))
                    addItem(label + QStringLiteral(" · ") + section.title, label);
            }
        }
        menu->setVisible(menu->count() > 0);
        empty->setVisible(menu->count() == 0);
        if (menu->count() > 0)
            menu->setCurrentRow(query.isEmpty() ? stack->currentIndex() : 0);
    };
    const auto openSection = [stack, title, sections](int index) {
        const auto &section = sections.at(index);
        section.page->initialize();
        auto *area = static_cast<QScrollArea *>(stack->widget(index));
        area->widget()->layout()->activate();
        stack->setCurrentIndex(index);
        title->setText(section.title);
        return area;
    };
    const auto openItem = [sections, openSection](const QListWidgetItem *item) {
        if (!item) return;
        const int index = item->data(Qt::UserRole).toInt();
        auto *area = openSection(index);
        const auto anchor = item->data(Qt::UserRole + 1).toString();
        if (anchor.isEmpty()) return;

        for (auto *widget : sections.at(index).page->findChildren<QWidget *>()) {
            const auto *label = qobject_cast<QLabel *>(widget);
            const auto *button = qobject_cast<QAbstractButton *>(widget);
            if ((label && label->text() == anchor) || (button && button->text() == anchor)) {
                QTimer::singleShot(0, area, [area, target = QPointer<QWidget>(widget)] {
                    if (target) area->ensureWidgetVisible(target, 12, 24);
                });
                break;
            }
        }
    };
    connect(search, &QLineEdit::textChanged, this, populate);
    connect(search, &QLineEdit::returnPressed, this, [menu, openItem] { openItem(menu->currentItem()); });
    for (const auto &[key, step] : {std::pair{Qt::Key_Up, -1}, std::pair{Qt::Key_Down, 1}}) {
        auto *shortcut = new QShortcut(QKeySequence(key), search);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, menu, [menu, step] {
            if (menu->count() > 0)
                menu->setCurrentRow(qBound(0, menu->currentRow() + step, menu->count() - 1));
        });
    }
    connect(menu, &QListWidget::itemClicked, this, openItem);
    connect(menu, &QListWidget::itemActivated, this, openItem);
    m_runWorkspaceCommand = [sections](const QString &id) {
        const auto command = std::ranges::find_if(settingsCommands, [&id](const SettingsCommand &entry) {
            return id == QLatin1StringView(entry.id);
        });
        if (command == std::end(settingsCommands)) return;

        for (const auto &section : sections) {
            auto *page = section.page;
            if (!command->pageType.cast(page)) continue;

            page->initialize();
            for (auto *button : page->findChildren<QAbstractButton *>()) {
                if (button->property("settingsCommand").toString() != id) continue;
                QTimer::singleShot(0, button, &QAbstractButton::click);
                return;
            }
            return;
        }
    };
    populate({});
    search->setFocus();

    connect(videoPage, &VideoSettingsPage::worksChanged, this, &SettingsDialog::worksChanged);
    connect(videoPage, &VideoSettingsPage::quickWorkRequested, this, &SettingsDialog::quickWorkRequested);
    connect(nfoPage, &NfoSettingsPage::referencesChanged, this, &SettingsDialog::referencesChanged);
    connect(nfoPage, &NfoSettingsPage::tagsChanged, this, &SettingsDialog::tagsChanged);
    connect(nfoPage, &NfoSettingsPage::actressesChanged, this, &SettingsDialog::actressesChanged);
    connect(nfoPage, &NfoSettingsPage::actorsChanged, this, &SettingsDialog::actorsChanged);
    connect(nfoPage, &NfoSettingsPage::worksChanged, this, &SettingsDialog::worksChanged);
}

QMap<QString, QString> SettingsDialog::workspaceCommands()
{
    QMap<QString, QString> commands;
    for (const auto &command : settingsCommands) {
        const QString title = &command.pageType == &DatabaseSettingsPage::staticMetaObject
            ? QStringLiteral("数据库运维 · ") + command.title : command.title;
        commands.insert(QString(workspaceCommandPrefix) + QLatin1StringView(command.id), title);
    }
    return commands;
}

void SettingsDialog::runWorkspaceCommand(const QString &id)
{
    m_runWorkspaceCommand(id);
}

void SettingsDialog::paintEvent(QPaintEvent *)
{
    const auto tokens = m_themeService.currentTokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(tokens.background));
    painter.setPen(QPen(QColor(tokens.border), 1));
    painter.drawPath(chamferPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5)));
}

QComboBox *SettingsDialog::themeSelector() const
{
    return m_commonPage->themeSelector();
}

} // namespace darkeye
