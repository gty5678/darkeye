#include "ui/layouts/myads/WorkspaceWidget.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolBar>
#include <QTimer>

using namespace darkeye::myads;

namespace {

QWidget *panel(const QString &title, const QString &color)
{
    auto *label = new QLabel(title);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral(
        "QLabel { background:%1; color:white; font-size:24px; "
        "font-weight:600; padding:24px; }").arg(color));
    return label;
}

ContentConfig demoContent(const QString &id, const QString &title,
                          const QString &color, bool closeable = true)
{
    ContentConfig config(id);
    config.setWindowTitle(title).setWidget(panel(title, color)).setCloseable(closeable);
    return config;
}

QString layoutPath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("myads-demo-layout.json"));
}

void buildDefault(WorkspaceWidget *workspace)
{
    PaneWidget *listPane = workspace->rootPane();
    PaneWidget *detailPane = workspace->split(listPane, Placement::Right);
    PaneWidget *notesPane = workspace->split(listPane, Placement::Bottom);
    workspace->fillPane(listPane, demoContent(QStringLiteral("movie_list"),
        QStringLiteral("影片列表（拖动这个标签试试）"), QStringLiteral("#2457a6"), false));
    workspace->fillPane(listPane, demoContent(QStringLiteral("search"),
        QStringLiteral("搜索结果"), QStringLiteral("#5f3a9e")));
    workspace->fillPane(detailPane, demoContent(QStringLiteral("movie_detail"),
        QStringLiteral("影片详情"), QStringLiteral("#19766f")));
    workspace->fillPane(notesPane, demoContent(QStringLiteral("notes"),
        QStringLiteral("笔记"), QStringLiteral("#9a5a20")));
    workspace->setActivePane(listPane);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Darkeye MyADS Demo"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption smokeOption(QStringLiteral("smoke-test"),
        QStringLiteral("无界面执行拆分、移动和布局恢复验证"));
    QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
        QStringLiteral("保存界面截图后退出"), QStringLiteral("file"));
    QCommandLineOption previewCenterOption(QStringLiteral("preview-center"),
        QStringLiteral("截图时显示中央停靠预览"));
    parser.addOption(smokeOption);
    parser.addOption(snapshotOption);
    parser.addOption(previewCenterOption);
    parser.process(application);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Darkeye C++ · MyADS 高级停靠区测试"));
    window.resize(1180, 760);
    auto *workspace = new WorkspaceWidget(&window, DockTheme::dark());
    window.setCentralWidget(workspace);
    buildDefault(workspace);

    auto *toolbar = window.addToolBar(QStringLiteral("MyADS 测试"));
    toolbar->setMovable(false);
    auto *targetLabel = new QLabel(toolbar);
    targetLabel->setText(QStringLiteral("当前拆分目标：%1  ")
        .arg(workspace->activePane() ? workspace->activePane()->paneId() : QStringLiteral("无")));
    toolbar->addWidget(targetLabel);
    auto *addButton = toolbar->addAction(QStringLiteral("添加标签"));
    auto *splitRightButton = toolbar->addAction(QStringLiteral("向右拆分"));
    auto *splitBottomButton = toolbar->addAction(QStringLiteral("向下拆分"));
    toolbar->addSeparator();
    auto *saveButton = toolbar->addAction(QStringLiteral("保存布局"));
    auto *loadButton = toolbar->addAction(QStringLiteral("恢复布局"));
    auto *resetButton = toolbar->addAction(QStringLiteral("恢复默认"));
    auto *themeButton = toolbar->addAction(QStringLiteral("切换明暗"));

    int added = 0;
    QObject::connect(workspace, &WorkspaceWidget::activePaneChanged, targetLabel,
        [targetLabel](const QString &paneId) {
            targetLabel->setText(QStringLiteral("当前拆分目标：%1  ").arg(paneId));
        });
    QObject::connect(addButton, &QAction::triggered, workspace, [workspace, &added] {
        ++added;
        workspace->fillPane(workspace->activePane(), demoContent(
            QStringLiteral("extra_%1").arg(added), QStringLiteral("新标签 %1").arg(added),
            added % 2 ? QStringLiteral("#a43d63") : QStringLiteral("#397847")));
    });
    QObject::connect(splitRightButton, &QAction::triggered, workspace, [workspace] {
        workspace->split(workspace->activePane(), Placement::Right);
    });
    QObject::connect(splitBottomButton, &QAction::triggered, workspace, [workspace] {
        workspace->split(workspace->activePane(), Placement::Bottom);
    });
    QObject::connect(saveButton, &QAction::triggered, workspace, [workspace, &window] {
        QString error;
        const bool saved = workspace->saveLayout(layoutPath(),
            [](const PaneWidget *pane, const QString &id) {
                return QJsonObject{{QStringLiteral("content_id"), id},
                                   {QStringLiteral("title"), pane->contentTitle(id)},
                                   {QStringLiteral("closeable"), pane->isContentCloseable(id)}};
            }, {}, &error);
        QMessageBox::information(&window, QStringLiteral("保存布局"), saved
            ? QStringLiteral("已保存到：\n%1").arg(QDir::toNativeSeparators(layoutPath()))
            : error);
    });
    QObject::connect(loadButton, &QAction::triggered, workspace, [workspace, &window] {
        QString error;
        const bool loaded = workspace->loadLayout(layoutPath(),
            [](const QJsonObject &value) -> std::optional<ContentConfig> {
                const QString id = value.value(QStringLiteral("content_id")).toString();
                const QString title = value.value(QStringLiteral("title")).toString(id);
                return demoContent(id, title, QStringLiteral("#31627d"),
                    value.value(QStringLiteral("closeable")).toBool(true));
            }, &error);
        if (!loaded) QMessageBox::warning(&window, QStringLiteral("恢复失败"), error);
    });
    QObject::connect(resetButton, &QAction::triggered, workspace, [workspace] {
        workspace->resetToSingleEmptyPane();
        buildDefault(workspace);
    });
    bool dark = true;
    QObject::connect(themeButton, &QAction::triggered, workspace, [workspace, &dark] {
        dark = !dark;
        workspace->applyTheme(dark ? DockTheme::dark() : DockTheme());
    });

    if (parser.isSet(smokeOption)) {
        QString error;
        const QString path = QDir::temp().filePath(QStringLiteral("darkeye-myads-smoke.json"));
        if (!workspace->moveContent(QStringLiteral("pane_1"), QStringLiteral("search"),
                                    QStringLiteral("pane_2"), DropZone::Center) ||
            !workspace->saveLayout(path, [](const PaneWidget *pane, const QString &id) {
                return QJsonObject{{QStringLiteral("content_id"), id},
                                   {QStringLiteral("title"), pane->contentTitle(id)}};
            }, {}, &error) ||
            !workspace->loadLayout(path, [](const QJsonObject &value)
                    -> std::optional<ContentConfig> {
                const QString id = value.value(QStringLiteral("content_id")).toString();
                return demoContent(id, value.value(QStringLiteral("title")).toString(),
                                   QStringLiteral("#31627d"));
            }, &error)) {
            qCritical("MyADS smoke test failed: %s", qPrintable(error));
            return 1;
        }
        return workspace->findPaneByContentId(QStringLiteral("search")) ? 0 : 2;
    }

    window.show();
    if (parser.isSet(snapshotOption)) {
        const QString snapshotPath = parser.value(snapshotOption);
        const bool previewCenter = parser.isSet(previewCenterOption);
        QTimer::singleShot(250, &application,
                          [&application, &window, workspace, snapshotPath, previewCenter] {
            if (previewCenter)
                workspace->showDropPreview(workspace->rootPane(), DropZone::Center);
            QApplication::processEvents();
            QDir().mkpath(QFileInfo(snapshotPath).absolutePath());
            if (!window.grab().save(snapshotPath)) application.exit(3);
            else application.quit();
        });
    }
    return application.exec();
}


