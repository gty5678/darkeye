#include "settings/Paths.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/SqliteConnection.h"
#include "MainWindow.h"
#include "ui/pages/PersonPage.h"
#include "ui/pages/WorkPage.h"
#include "ui/components/WorkFilter.h"
#include "darkeye_ui/components/ColorWheel.h"
#include "ui/components/WorkspaceSearch.h"
#include "ui/pages/AddWorkTabPage3.h"
#include "ui/pages/TagManagementWidget.h"
#include "ui/components/FanartStripWidget.h"
#include "ui/components/WorkCard.h"
#include "ui/pages/ModifyActressPage.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSqlQuery>
#include <QTest>
#include <QTimer>

namespace
{
template <typename T>
T *findWorkControl(QObject *root, const QString &controlId)
{
    if (root == nullptr) return nullptr;
    for (T *child : root->findChildren<T *>())
        if (child->property("workControlId").toString() == controlId)
            return child;
    return nullptr;
}
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    if (application.arguments().size() != 3)
        return 2;
    const QString dataDirectory = QDir::cleanPath(application.arguments().at(1));
    const QString outputDirectory = QDir::cleanPath(application.arguments().at(2));
    QDir().mkpath(outputDirectory);

    const QString applicationDirectory = QFileInfo(dataDirectory).absoluteDir().absolutePath();
    const darkeye::settings::Paths paths(applicationDirectory, dataDirectory);
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    if (!publicConnection.open(paths.publicDatabase(), true, &errorMessage) ||
        !privateConnection.open(paths.privateDatabase(), true, &errorMessage))
    {
        return 3;
    }
    darkeye::ThemeService themes(application);
    darkeye::MainWindow window(themes, publicConnection.database(),
                               privateConnection.database(), paths);
    window.resize(1340, 800);
    window.show();
    QApplication::processEvents();

    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    if (!search)
        return 4;
    emit search->openRequested(QStringLiteral("mutiwork"));
    auto *workPage = window.findChild<darkeye::WorkPage *>(QStringLiteral("WorkPage"));
    if (!workPage)
        return 4;
    auto *workFilter = window.findChild<darkeye::WorkFilter *>();
    auto *workCount = findWorkControl<QLabel>(workFilter, QStringLiteral("WorkCountLabel"));
    if (workCount == nullptr || workCount->text() == QStringLiteral("过滤总数:0") ||
        workPage->findChildren<darkeye::WorkCard *>().isEmpty())
    {
        return 28;
    }
    // Python 封面卡片同样异步解码；视觉基线必须等首屏图片稳定后再截取。
    QTest::qWait(2500);
    if (!window.grab().save(QDir(outputDirectory).filePath(QStringLiteral("work-list.png"))))
    {
        return 5;
    }
    if (!workPage->grab().save(
            QDir(outputDirectory).filePath(QStringLiteral("work-list-page.png"))))
    {
        return 9;
    }
    emit search->openRequested(QStringLiteral("work_filters"));
    auto *tagPanel = workFilter->findChild<QWidget *>(QStringLiteral("WorkTagPanel"));
    if (tagPanel == nullptr || !tagPanel->isVisible())
        return 29;
    QTest::qWait(150);
    if (!window.grab().save(
            QDir(outputDirectory).filePath(QStringLiteral("work-tag-selector.png"))))
    {
        return 30;
    }
    emit search->openRequested(QStringLiteral("mutiwork"));

    const auto filtered = workPage->filteredWorks();
    if (filtered.isEmpty()) return 6;
    const auto workId = filtered.first().id;
    emit workPage->detailRequested(workId);
    QTest::qWait(250);
    if (!window.grab().save(QDir(outputDirectory).filePath(QStringLiteral("work-detail.png"))))
    {
        return 7;
    }
    emit search->openRequested(QStringLiteral("actress"));
    auto *actressPage = window.findChild<darkeye::PersonPage *>(QStringLiteral("ActressPage"));
    if (actressPage == nullptr)
        return 10;
    QTest::qWait(1500);
    if (!window.grab().save(QDir(outputDirectory).filePath(QStringLiteral("actress-list.png"))))
    {
        return 11;
    }
    QSqlQuery actressQuery(publicConnection.database());
    if (!actressQuery.exec(
            QStringLiteral("SELECT actress_id FROM actress ORDER BY actress_id DESC LIMIT 1")) ||
        !actressQuery.next())
    {
        return 12;
    }
    emit actressPage->detailRequested(darkeye::PersonKind::Actress,
                                      actressQuery.value(0).toLongLong());
    QTest::qWait(500);
    if (!window.grab().save(QDir(outputDirectory).filePath(QStringLiteral("person-detail.png"))))
    {
        return 13;
    }
    emit actressPage->editRequested(darkeye::PersonKind::Actress,
                                    actressQuery.value(0).toLongLong());
    QTest::qWait(500);
    auto *personEditor =
        window.findChild<darkeye::ModifyActressPage *>(QStringLiteral("ModifyActressPage"));
    if (personEditor == nullptr ||
        !personEditor->grab().save(
            QDir(outputDirectory).filePath(QStringLiteral("person-editor.png"))))
    {
        return 14;
    }
    personEditor->close();
    const auto capturePage = [&](const QString &route, const QString &filename) {
        emit search->openRequested(route);
        QTest::qWait(250);
        return window.grab().save(QDir(outputDirectory).filePath(filename));
    };
    if (!capturePage(QStringLiteral("manage_tags"), QStringLiteral("tag-management.png")))
        return 15;
    auto *tags = window.findChild<darkeye::TagManagementWidget *>();
    if (!tags)
        return 16;
    bool capturedTagTypes = false;
    for (auto *button : tags->findChildren<QPushButton *>())
    {
        if (button->text() != QStringLiteral("修改标签类型"))
            continue;
        QTimer::singleShot(100, &window, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            {
                capturedTagTypes = dialog->grab().save(QDir(outputDirectory).filePath(
                    QStringLiteral("tag-type-management.png")));
                dialog->reject();
            }
        });
        button->click();
        break;
    }
    if (!capturedTagTypes)
        return 16;
    if (!capturePage(QStringLiteral("manage_makers"), QStringLiteral("maker-prefix-management.png")))
        return 17;
    if (!capturePage(QStringLiteral("manage_labels"), QStringLiteral("reference-management.png")))
        return 18;
    if (!capturePage(QStringLiteral("manage_deleted"), QStringLiteral("work-soft-delete.png")))
        return 20;
    if (!capturePage(QStringLiteral("manage_trash"), QStringLiteral("work-recycle-bin.png")))
        return 21;
    if (!capturePage(QStringLiteral("work_edit/%1").arg(workId), QStringLiteral("work-editor.png")))
        return 22;
    auto *editor = window.findChild<darkeye::AddWorkTabPage3 *>();
    if (editor == nullptr)
        return 23;
    auto *fanartStrip = findWorkControl<darkeye::FanartStripWidget>(
        editor, QStringLiteral("WorkFanartStrip"));
    if (fanartStrip == nullptr)
        return 24;
    fanartStrip->setEntries({{QStringLiteral("https://invalid.example/fanart-1.jpg"),
                              QStringLiteral("DEMO-001-1.jpg"),
                              {}},
                             {QStringLiteral("https://invalid.example/pending.jpg"), {}, {}}});
    fanartStrip->setCanAdd(true);
    QTest::qWait(100);
    if (!window.grab().save(QDir(outputDirectory).filePath(QStringLiteral("work-editor-fanart.png"))))
    {
        return 25;
    }
    if (!capturePage(QStringLiteral("library_overview"), QStringLiteral("library-overview.png")) ||
        !capturePage(QStringLiteral("top_actresses"), QStringLiteral("top-actresses.png")) ||
        !capturePage(QStringLiteral("record_heatmap"), QStringLiteral("record-heatmap.png")))
        return 26;
    darkeye::ColorWheelSimple colorWheel;
    colorWheel.setInitialColor(QStringLiteral("#336699"));
    colorWheel.show();
    QTest::qWait(100);
    if (!colorWheel.grab().save(
            QDir(outputDirectory).filePath(QStringLiteral("component-color-wheel.png"))))
    {
        return 8;
    }
    return 0;
}
