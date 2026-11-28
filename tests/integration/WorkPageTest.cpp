#include "QtTestEntry.h"

#include "database/repositories/PrivateRepository.h"
#include "ui/pages/WorkPage.h"
#include "settings/Paths.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/CompleterLineEdit.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/MakerSelector.h"
#include "darkeye_ui/components/LazyScrollArea.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/WorkRepository.h"
#include "crawler/CrawlerScheduler.h"
#include "ui/components/FanartStripWidget.h"
#include "ui/components/AsyncImageLabel.h"
#include "ui/components/ClickableLabel.h"
#include "ui/components/ImageDropWidget.h"
#include "ui/components/WorkCard.h"
#include "ui/pages/AddWorkTabPage3.h"
#include "ui/pages/TagManagementWidget.h"
#include "ui/components/WorkTagSelector.h"
#include "ui/components/PersonTransferSelector.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsSceneHoverEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>
#include <algorithm>

namespace
{
class ContextMenuSpy final : public QWidget
{
public:
    int contextMenuEvents = 0;

protected:
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        ++contextMenuEvents;
        event->accept();
    }
};

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

class WorkPageTest final : public QObject
{
    Q_OBJECT

  private slots:
    void matchesPythonControlAndRoutingContract();
    void searchableSelectorsAndClearFilters();
    void loadsOneDatabasePageAtATime();
    void filtersByPrivateScope();
    void filtersByLocalVideoScope();
    void persistsViewPreferences();
    void groupsAndSelectsTags();
    void tagSelectorMatchesPythonGeometryAndNavigation();
    void tagSelectionInsetsMatchAvailableInEditorAndManagement();
    void tagSelectorPageWidthsAndSearchStyle();
    void importsCoverAndRollsBackOnDatabaseFailure();
    void loadingCrLfStoriesDoesNotMarkWorkModified();
    void personSelectionChangesRenderWarningFrame();
    void tagSelectionChangesRenderWarningFrame();
    void fanartChangesRenderWarningFrame();
    void downloadedCoverEnablesModifyAction();
    void identicalCoverDoesNotEnableModifyAction();
    void droppingOriginalAfterSaveDoesNotEnableModifyAction_data();
    void droppingOriginalAfterSaveDoesNotEnableModifyAction();
    void addWorkBasicInfoSerialMatchesPythonBehavior();
    void editorCompletesExistingDirectorsAndRefreshes();
    void addWorkBasicInfoKeepsRowsHorizontalWhenNarrow();
    void editorAutomaticallySavesLayoutWithoutSettings();
    void normalWorkCardTitleFitsTwoLines();
    void workCardCopiesSerialAndActivatesOnlyCover();
    void matchesPythonWorkListSqlContract();
    void loadsConfiguredRealDatabaseWhenAvailable();
};

void WorkPageTest::loadsConfiguredRealDatabaseWhenAvailable()
{
    const QString dataDirectory = qEnvironmentVariable("DARKEYE_REAL_DATA_DIR").trimmed();
    if (dataDirectory.isEmpty())
        QSKIP("Set DARKEYE_REAL_DATA_DIR to run the real-data WorkPage integration "
              "check");

    const QString applicationDirectory = QFileInfo(dataDirectory).absoluteDir().absolutePath();
    const darkeye::settings::Paths paths(applicationDirectory, dataDirectory);
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(paths.publicDatabase(), true, &errorMessage), qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::WorkPage page(connection.database(), themes, paths.workCoverDirectory());
    page.initialize();

    auto *count = findWorkControl<QLabel>(&page, QStringLiteral("WorkCountLabel"));
    auto *cardsWidget = page.findChild<QWidget *>(QStringLiteral("DesignLazyScrollArea"));
    QVERIFY(count != nullptr);
    QVERIFY(cardsWidget != nullptr);
    auto *cards = static_cast<darkeye::LazyScrollArea *>(cardsWidget);
    QTRY_VERIFY(cards->itemCount() > 0);
    QVERIFY2(count->text() != QStringLiteral("过滤总数:0"), qPrintable(count->text()));
    QVERIFY(cards->itemCount() <= cards->pageSize());
}

void WorkPageTest::matchesPythonWorkListSqlContract()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &errorMessage));
    QSqlQuery seed(connection.database());
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO maker(cn_name) VALUES('片商甲')")));
    const qint64 makerId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO maker(cn_name) VALUES('片商乙')")));
    const qint64 otherMakerId = seed.lastInsertId().toLongLong();

    darkeye::WorkRepository repository(connection.database());
    darkeye::Work highlighted;
    highlighted.serialNumber = QStringLiteral("SQL-001");
    highlighted.chineseTitle = QStringLiteral("中文标题");
    highlighted.japaneseTitle = QStringLiteral("日文标题");
    highlighted.releaseDate = QStringLiteral("2025-01-02");
    highlighted.makerId = makerId;
    QVERIFY(repository.insertComplete(highlighted, {}, {}, {1, 2}, &errorMessage).has_value());
    darkeye::Work noDate;
    noDate.serialNumber = QStringLiteral("SQL-002");
    noDate.chineseTitle = QStringLiteral("无日期");
    noDate.makerId = makerId;
    QVERIFY(repository.insertComplete(noDate, {}, {}, {}, &errorMessage).has_value());
    darkeye::Work other;
    other.serialNumber = QStringLiteral("SQL-003");
    other.chineseTitle = QStringLiteral("其他片商");
    other.releaseDate = QStringLiteral("2024-01-02");
    other.makerId = otherMakerId;
    QVERIFY(repository.insertComplete(other, {}, {}, {}, &errorMessage).has_value());

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    darkeye::WorkPage page(connection.database(), themes);
    page.initialize();
    auto *tags = page.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->clearSelection();
    page.refresh();
    auto *maker = findWorkControl<QComboBox>(&page, QStringLiteral("WorkMakerFilter"));
    auto *sort = findWorkControl<QComboBox>(&page, QStringLiteral("WorkSortSelector"));
    auto *count = findWorkControl<QLabel>(&page, QStringLiteral("WorkCountLabel"));
    QVERIFY(maker != nullptr);
    QVERIFY(sort != nullptr);
    QVERIFY(count != nullptr);
    QTRY_COMPARE(count->text(), QStringLiteral("过滤总数:4"));
    QTRY_COMPARE(page.findChildren<darkeye::WorkCard *>().size(), 4);

    page.focusSearch();
    QTRY_VERIFY_WITH_TIMEOUT(maker->findData(makerId) > 0, 2000);
    const int makerIndex = maker->findData(makerId);
    maker->setCurrentIndex(makerIndex);
    QTRY_COMPARE(count->text(), QStringLiteral("过滤总数:3"));
    QTRY_COMPARE(page.findChildren<darkeye::WorkCard *>().size(), 3);

    sort->setCurrentText(QStringLiteral("发布时间逆序"));
    QTRY_COMPARE(count->text(), QStringLiteral("过滤总数:2"));
    QTRY_COMPARE(page.findChildren<darkeye::WorkCard *>().size(), 2);
    for (auto *card : page.findChildren<darkeye::WorkCard *>())
    {
        const auto *serial = card->findChild<darkeye::ClickableLabel *>(
            QStringLiteral("WorkCardSerialNumber"));
        QVERIFY(serial != nullptr);
        QCOMPARE(serial->text(), QStringLiteral("SQL-001"));
    }
}

void WorkPageTest::searchableSelectorsAndClearFilters()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    QSqlQuery seed(connection.database());
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO maker(maker_id,cn_name,jp_name) VALUES(900001,'片商甲','Maker A')")));
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO label(label_id,cn_name,jp_name) VALUES(900001,'厂牌甲','Label A')")));
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO series(series_id,cn_name,jp_name) VALUES(900001,'系列甲','Series A')")));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work target;
    target.serialNumber = QStringLiteral("SELECT-001");
    target.makerId = 900001;
    target.labelId = 900001;
    target.seriesId = 900001;
    QVERIFY(repository.insertComplete(target, {}, {}, {}, &error).has_value());
    darkeye::Work other;
    other.serialNumber = QStringLiteral("SELECT-002");
    QVERIFY(repository.insertComplete(other, {}, {}, {}, &error).has_value());
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::WorkPage page(connection.database(), themes);
    page.initialize();
    auto *count = findWorkControl<QLabel>(&page, QStringLiteral("WorkCountLabel"));
    auto *clear = findWorkControl<QPushButton>(&page, QStringLiteral("WorkClearButton"));
    QVERIFY(count != nullptr);
    QVERIFY(clear != nullptr);
    clear->click();
    QTRY_VERIFY(!count->text().isEmpty());
    const QString unfilteredCount = count->text();
    page.focusSearch();
    for (const QString &control : {QStringLiteral("WorkMakerFilter"),
                                  QStringLiteral("WorkLabelFilter"),
                                  QStringLiteral("WorkSeriesFilter")})
    {
        auto *selector = findWorkControl<darkeye::MakerSelector>(&page, control);
        QVERIFY(selector != nullptr);
        QTRY_VERIFY(selector->findData(900001) > 0);
        QVERIFY(selector->isEditable());
        QVERIFY(selector->completer() != nullptr);
        selector->setCurrentIndex(selector->findData(900001));
        QTRY_COMPARE(count->text(), QStringLiteral("过滤总数:1"));
        QCOMPARE(selector->maker(), std::optional<qint64>(900001));
        page.refreshFromWorkspace(false, true);
        QCOMPARE(selector->maker(), std::optional<qint64>(900001));
        clear->click();
        QCOMPARE(selector->currentIndex(), 0);
        QVERIFY(selector->currentText().isEmpty());
        QVERIFY(!selector->maker().has_value());
        QTRY_COMPARE(count->text(), unfilteredCount);
        selector->setCurrentIndex(selector->findData(900001));
        QTRY_COMPARE(count->text(), QStringLiteral("过滤总数:1"));
        selector->setEditText(QStringLiteral("不存在的筛选项"));
        QTRY_COMPARE(count->text(), unfilteredCount);
        QVERIFY(!selector->maker().has_value());
        clear->click();
        QCOMPARE(selector->currentIndex(), 0);
        QVERIFY(selector->currentText().isEmpty());
    }
}

void WorkPageTest::matchesPythonControlAndRoutingContract()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    darkeye::WorkRepository repository(connection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("PAGE-001");
    work.chineseTitle = QStringLiteral("原始标题");
    const auto workId = repository.insertComplete(work, {}, {}, {}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    darkeye::WorkPage page(connection.database(), themeService);
    page.initialize();
    auto *tags = page.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->clearSelection();
    page.refresh();
    auto *sortSelector = findWorkControl<QComboBox>(&page, QStringLiteral("WorkSortSelector"));
    auto *tagList = page.findChild<QGraphicsView *>(QStringLiteral("WorkTagList"));
    auto *notesFilter = findWorkControl<QLineEdit>(&page, QStringLiteral("WorkNotesFilter"));
    auto *coverButton = findWorkControl<darkeye::IconButton>(
        &page, QStringLiteral("WorkCoverViewButton"));
    auto *refreshButton = findWorkControl<QPushButton>(
        &page, QStringLiteral("WorkRefreshButton"));
    auto *scopeSelector = findWorkControl<QComboBox>(
        &page, QStringLiteral("WorkScopeSelector"));
    auto *countLabel = findWorkControl<QLabel>(&page, QStringLiteral("WorkCountLabel"));
    QVERIFY(sortSelector != nullptr);
    QVERIFY(tagList != nullptr);
    QCOMPARE(tagList->minimumWidth(), tagList->maximumWidth());
    QVERIFY(tagList->viewport()->width() >= 60); // Two columns and the card margins.
    QVERIFY(notesFilter != nullptr);
    QVERIFY(coverButton != nullptr);
    QVERIFY(refreshButton != nullptr);
    QVERIFY(scopeSelector != nullptr);
    QVERIFY(countLabel != nullptr);
    QCOMPARE(notesFilter->objectName(), QStringLiteral("DesignInput"));
    QCOMPARE(sortSelector->objectName(), QStringLiteral("DesignComboBox"));
    QCOMPARE(scopeSelector->objectName(), QStringLiteral("DesignComboBox"));
    QCOMPARE(countLabel->objectName(), QStringLiteral("DesignLabel"));
    QCOMPARE(coverButton->objectName(), QStringLiteral("WorkCoverViewToggle"));
    QCOMPARE(refreshButton->objectName(), QStringLiteral("DesignRotateButton"));
    QCOMPARE(sortSelector->count(), 13);
    QCOMPARE(sortSelector->currentText(), QStringLiteral("添加逆序"));
    QTRY_COMPARE(page.findChildren<darkeye::WorkCard *>().size(), 1);

    auto *card = page.findChild<darkeye::WorkCard *>();
    auto *cover = card ? card->findChild<darkeye::AsyncImageLabel *>(
                             QStringLiteral("WorkCardCover"))
                       : nullptr;
    QVERIFY(card != nullptr);
    QVERIFY(cover != nullptr);
    QSignalSpy detailRequested(&page, &darkeye::WorkPage::detailRequested);
    QSignalSpy editRequested(&page, &darkeye::WorkPage::editRequested);
    QTest::mouseClick(cover, Qt::LeftButton);
    QTest::mouseClick(cover, Qt::RightButton);
    QCoreApplication::processEvents();
    QCOMPARE(detailRequested.count(), 1);
    QCOMPARE(detailRequested.first().first().toLongLong(), *workId);
    QCOMPARE(editRequested.count(), 1);
    QCOMPARE(editRequested.first().first().toLongLong(), *workId);
}

void WorkPageTest::loadsOneDatabasePageAtATime()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    darkeye::WorkRepository repository(connection.database());
    for (int index = 1; index <= 75; ++index)
    {
        darkeye::Work work;
        work.serialNumber = QStringLiteral("PAGE-%1").arg(index, 3, 10, QLatin1Char('0'));
        QVERIFY(repository.insertComplete(work, {}, {}, {}, &errorMessage).has_value());
    }

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    darkeye::WorkPage page(connection.database(), themeService);
    QVERIFY(!page.isInitialized());
    page.resize(500, 360);
    page.show();
    page.initialize();
    auto *tags = page.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->clearSelection();
    page.refresh();
    QVERIFY(page.isInitialized());
    auto *scrollArea = page.findChild<QScrollArea *>(QStringLiteral("DesignLazyScrollArea"));
    auto *lazyArea = dynamic_cast<darkeye::LazyScrollArea *>(scrollArea);
    QVERIFY(lazyArea != nullptr);
    QVERIFY(lazyArea->loading());
    QCOMPARE(lazyArea->itemCount(), 0);
    QTRY_COMPARE(lazyArea->itemCount(), 70);
    QCOMPARE(page.findChildren<darkeye::WorkCard *>().size(), 70);
    auto *scrollBar = lazyArea->verticalScrollBar();
    QTRY_VERIFY(scrollBar->maximum() > 0);
    const int firstPageMaximum = scrollBar->maximum();

    QVERIFY(lazyArea->loadNextPage());
    QTRY_COMPARE(lazyArea->itemCount(), 75);
    QCOMPARE(page.findChildren<darkeye::WorkCard *>().size(), 75);
    QVERIFY(lazyArea->reachedEnd());

    QTRY_VERIFY(scrollBar->maximum() > firstPageMaximum);
    const int position = scrollBar->maximum();
    scrollBar->setValue(position);
    darkeye::Work newWork;
    newWork.serialNumber = QStringLiteral("PAGE-076");
    QVERIFY(repository.insertComplete(newWork, {}, {}, {}, &errorMessage).has_value());
    page.refreshFromWorkspace(false, false);
    QTRY_COMPARE(lazyArea->itemCount(), 76);
    QTRY_COMPARE(scrollBar->value(), position);

    auto *serialFilter = findWorkControl<QLineEdit>(&page, QStringLiteral("WorkSerialFilter"));
    QVERIFY(serialFilter);
    serialFilter->setText(QStringLiteral("PAGE-07"));
    QTRY_COMPARE(lazyArea->itemCount(), 7);
    QVERIFY(scrollBar->maximum() > 0);
    QCOMPARE(scrollBar->value(), 0);
}

void WorkPageTest::filtersByPrivateScope()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(QDir(temporaryDirectory.path()).filePath("private.db"), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(publicConnection, darkeye::DatabaseKind::Public,
                                                            &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateConnection, darkeye::DatabaseKind::Private,
                                                            &errorMessage));

    darkeye::WorkRepository works(publicConnection.database());
    darkeye::Work first;
    first.serialNumber = QStringLiteral("SCOPE-001");
    const auto firstId = works.insertComplete(first, {}, {}, {}, &errorMessage);
    darkeye::Work second;
    second.serialNumber = QStringLiteral("SCOPE-002");
    const auto secondId = works.insertComplete(second, {}, {}, {}, &errorMessage);
    QVERIFY(firstId.has_value());
    QVERIFY(secondId.has_value());
    darkeye::PrivateRepository privateData(privateConnection.database());
    QVERIFY(privateData.addFavoriteWork(*firstId, first.serialNumber, &errorMessage));
    QVERIFY(privateData.addFavoriteWork(*secondId, second.serialNumber, &errorMessage));
    QVERIFY(privateData.addMasturbationRecord(*firstId, first.serialNumber, QStringLiteral("2026-09-10"), QString(), 4,
                                              QString(), &errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    darkeye::WorkPage page(publicConnection.database(), themeService, {},
                           privateConnection.database());
    page.initialize();
    auto *tags = page.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->clearSelection();
    page.refresh();
    auto *scope = findWorkControl<QComboBox>(&page, QStringLiteral("WorkScopeSelector"));
    QVERIFY(scope != nullptr);
    QTRY_COMPARE(page.findChildren<darkeye::WorkCard *>().size(), 2);
    scope->setCurrentText(QStringLiteral("收藏未观看"));
    QTRY_COMPARE(page.findChildren<darkeye::WorkCard *>().size(), 1);
    auto *serial = page.findChild<darkeye::ClickableLabel *>(
        QStringLiteral("WorkCardSerialNumber"));
    QVERIFY(serial != nullptr);
    QCOMPARE(serial->text(), QStringLiteral("SCOPE-002"));
    scope->setCurrentText(QStringLiteral("已撸过"));
    QTRY_VERIFY([&page]
    {
        const auto *label = page.findChild<darkeye::ClickableLabel *>(
            QStringLiteral("WorkCardSerialNumber"));
        return label != nullptr && label->text() == QStringLiteral("SCOPE-001");
    }());
    serial = page.findChild<darkeye::ClickableLabel *>(QStringLiteral("WorkCardSerialNumber"));
    QVERIFY(serial != nullptr);
    QCOMPARE(serial->text(), QStringLiteral("SCOPE-001"));
}

void WorkPageTest::filtersByLocalVideoScope()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    darkeye::WorkRepository repository(connection.database());
    darkeye::Work withVideo;
    withVideo.serialNumber = QStringLiteral("PAGE-VIDEO-001");
    withVideo.videoUrl = QStringLiteral("C:/videos/PAGE-VIDEO-001.mp4");
    QVERIFY2(repository.insertComplete(withVideo, {}, {}, {}, &errorMessage).has_value(),
             qPrintable(errorMessage));
    darkeye::Work withoutVideo;
    withoutVideo.serialNumber = QStringLiteral("PAGE-VIDEO-002");
    QVERIFY2(repository.insertComplete(withoutVideo, {}, {}, {}, &errorMessage).has_value(),
             qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    darkeye::WorkPage page(connection.database(), themeService);
    page.initialize();
    auto *tags = page.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->clearSelection();
    page.refresh();
    auto *scope = findWorkControl<QComboBox>(&page, QStringLiteral("WorkScopeSelector"));
    QVERIFY(scope != nullptr);
    QCOMPARE(scope->findText(QStringLiteral("本地有视频")), 4);
    scope->setCurrentText(QStringLiteral("本地有视频"));
    QTRY_COMPARE(page.findChildren<darkeye::WorkCard *>().size(), 1);
    const auto *serial = page.findChild<darkeye::ClickableLabel *>(
        QStringLiteral("WorkCardSerialNumber"));
    QVERIFY(serial != nullptr);
    QCOMPARE(serial->text(), QStringLiteral("PAGE-VIDEO-001"));
}

void WorkPageTest::persistsViewPreferences()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &errorMessage));
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    const darkeye::AppSettings originalSettings = darkeye::settings::app();
    darkeye::AppSettings testSettings = originalSettings;
    testSettings.workTagSelectorVisible = false;
    testSettings.workLargeCoverView = false;
    darkeye::settings::saveApp(testSettings);

    {
        darkeye::WorkPage page(connection.database(), themeService);
        page.initialize();
        auto *tagPanel = page.findChild<QWidget *>(QStringLiteral("WorkTagPanel"));
        QVERIFY(!findWorkControl<darkeye::IconButton>(
            &page, QStringLiteral("WorkTagPanelButton")));
        auto *viewButton = findWorkControl<darkeye::IconButton>(
            &page, QStringLiteral("WorkCoverViewButton"));
        auto *lazyArea = dynamic_cast<darkeye::LazyScrollArea *>(
            page.findChild<QScrollArea *>(QStringLiteral("DesignLazyScrollArea")));
        QVERIFY(tagPanel != nullptr);
        QVERIFY(viewButton != nullptr);
        QVERIFY(lazyArea != nullptr);
        QVERIFY(!tagPanel->isHidden());
        QTest::mouseClick(viewButton, Qt::LeftButton);
        QCOMPARE(lazyArea->columnWidth(), 250);
    }

    darkeye::WorkPage restored(connection.database(), themeService);
    restored.initialize();
    auto *restoredPanel = restored.findChild<QWidget *>(QStringLiteral("WorkTagPanel"));
    auto *restoredArea = dynamic_cast<darkeye::LazyScrollArea *>(
        restored.findChild<QScrollArea *>(QStringLiteral("DesignLazyScrollArea")));
    QVERIFY(restoredPanel != nullptr);
    QVERIFY(restoredArea != nullptr);
    QVERIFY(!restoredPanel->isHidden());
    QCOMPARE(restoredArea->columnWidth(), 250);
    darkeye::settings::saveApp(originalSettings);
}

void WorkPageTest::groupsAndSelectsTags()
{
    const QList<darkeye::TagOption> tags{
        {1, QStringLiteral("标签甲"), QStringLiteral("类型一"), QStringLiteral("#336699"),
         QStringLiteral("说明甲"), {}, {QStringLiteral("旧标签甲")}},
        {2, QStringLiteral("标签乙"), QStringLiteral("类型二"), QStringLiteral("#663399"),
         QStringLiteral("说明乙")}};
    QWidget selectorHost;
    selectorHost.resize(800, 500);
    auto *selectorLayout = new QHBoxLayout(&selectorHost);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    darkeye::WorkTagSelector selector(tags, nullptr, &selectorHost);
    selectorLayout->addWidget(&selector, 0, Qt::AlignLeft);
    selectorLayout->addStretch();
    selectorHost.show();
    QTest::qWait(1);
    auto *tabs = selector.findChild<QTabWidget *>();
    const auto availableLists = selector.findChildren<QGraphicsView *>(QStringLiteral("WorkAvailableTagList"));
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(availableLists.size(), 2);
    auto *search = findWorkControl<QLineEdit>(&selector, QStringLiteral("WorkTagSearch"));
    auto *searchResult = findWorkControl<QLabel>(&selector, QStringLiteral("WorkTagSearchResult"));
    auto *searchPrevious = findWorkControl<QPushButton>(&selector, QStringLiteral("WorkTagSearchPrevious"));
    auto *searchNext = findWorkControl<QPushButton>(&selector, QStringLiteral("WorkTagSearchNext"));
    auto *clear = findWorkControl<QPushButton>(&selector, QStringLiteral("WorkTagClearButton"));
    auto *reload = findWorkControl<QPushButton>(&selector, QStringLiteral("WorkTagReloadButton"));
    auto *selectedView = selector.findChild<QGraphicsView *>(QStringLiteral("WorkTagList"));
    auto *availablePanel = selector.findChild<QWidget *>(QStringLiteral("WorkTagAvailablePanel"));
    QVERIFY(search != nullptr);
    QVERIFY(searchResult != nullptr);
    QVERIFY(searchPrevious != nullptr);
    QVERIFY(searchNext != nullptr);
    QVERIFY(clear != nullptr);
    QVERIFY(reload != nullptr);
    QVERIFY(selectedView != nullptr);
    QVERIFY(availablePanel != nullptr);
    QVERIFY(availablePanel->isVisible());
    QVERIFY(findWorkControl<QPushButton>(&selector, QStringLiteral("WorkTagExpandButton")) == nullptr);
    QCOMPARE(selectedView->geometry().top(), availablePanel->geometry().top());
    QCOMPARE(selectedView->geometry().left(), 2);
    QCOMPARE(availablePanel->geometry().left() - selectedView->geometry().right() - 1, 1);
    QCOMPARE(selector.rect().right() - availablePanel->geometry().right(), 2);
    QCOMPARE(selector.rect().bottom() - selectedView->geometry().bottom(), 2);
    const QWidget *toolbar = clear->parentWidget();
    QCOMPARE(toolbar->geometry().left(), selectedView->geometry().left());
    QCOMPARE(toolbar->geometry().right(), availablePanel->geometry().right());
    QCOMPARE(selectedView->geometry().top() - toolbar->geometry().bottom() - 1, 2);
    const QList<QWidget *> controls{clear, reload, search, searchPrevious, searchNext, searchResult};
    for (qsizetype i = 0; i < controls.size(); ++i)
    {
        const QWidget *control = controls.at(i);
        QCOMPARE(control->parentWidget(), toolbar);
        QVERIFY(qAbs(control->geometry().center().y() - search->geometry().center().y()) <= 1);
        if (i > 0)
            QCOMPARE(control->geometry().left() - controls.at(i - 1)->geometry().right() - 1, 4);
    }
    search->setText(QStringLiteral("标签"));
    QCOMPARE(searchResult->text(), QStringLiteral("结果 1/2"));
    searchNext->click();
    QCOMPARE(searchResult->text(), QStringLiteral("结果 2/2"));
    searchPrevious->click();
    QCOMPARE(searchResult->text(), QStringLiteral("结果 1/2"));
    searchNext->click();
    search->setText(QStringLiteral("旧标签"));
    QCOMPARE(searchResult->text(), QStringLiteral("结果 1/1"));
    search->setText(QStringLiteral("类型一"));
    QCOMPARE(searchResult->text(), QStringLiteral("无匹配结果"));
    QSignalSpy selectionChanged(&selector, &darkeye::WorkTagSelector::selectionChanged);
    QGraphicsObject *firstTag = nullptr;
    QGraphicsView *firstTagView = nullptr;
    for (auto *view : availableLists)
        for (auto *item : view->scene()->items())
            if (auto *object = item->toGraphicsObject();
                object && object->objectName() == QStringLiteral("WorkTag_1"))
            {
                firstTag = object;
                firstTagView = view;
            }
    QVERIFY(firstTag != nullptr);
    QVERIFY(firstTagView != nullptr);
    const QPoint clickPoint = firstTagView->mapFromScene(firstTag->sceneBoundingRect().center());
    QTest::mouseClick(firstTagView->viewport(), Qt::LeftButton, Qt::NoModifier, clickPoint);
    QCOMPARE(selector.selectedIds(), QList<qint64>{1});
    QCOMPARE(selectionChanged.count(), 1);
    QVERIFY(!firstTag->isVisible());
    const auto selectedItems = selectedView->scene()->items();
    QVERIFY(std::any_of(selectedItems.cbegin(), selectedItems.cend(), [](QGraphicsItem *item) {
        auto *object = item->toGraphicsObject();
        return object && object->objectName() == QStringLiteral("WorkTag_1");
    }));
    clear->click();
    QVERIFY(selector.selectedIds().isEmpty());
    QCOMPARE(selectionChanged.count(), 2);
    QVERIFY(firstTag->isVisible());

    selector.setLoader(
        [] { return QList<darkeye::TagOption>{{3, QStringLiteral("新标签"), QStringLiteral("新类型"), {}, {}}}; });
    reload->click();
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(tabs->tabText(0), QStringLiteral("新类型"));

    QList<darkeye::TagOption> categories;
    for (int id = 1; id <= 12; ++id)
        categories.append({id, QStringLiteral("标签"), QStringLiteral("分类%1").arg(id), {}});
    selector.setLoader([categories] { return categories; });
    reload->click();
    QCOMPARE(tabs->count(), 12);
    auto *availableView = qobject_cast<QGraphicsView *>(tabs->currentWidget());
    QVERIFY(availableView != nullptr);
    const int selectedHeight = selectedView->viewport()->height();
    const int availableHeight = availableView->viewport()->height();

    selectorHost.resize(800, 210);
    QTRY_COMPARE(selector.height(), 210);
    QVERIFY(selectedView->viewport()->height() < selectedHeight);
    QVERIFY(availableView->viewport()->height() < availableHeight);

    selectorHost.resize(800, 500);
    QTRY_COMPARE(selector.height(), 500);
    QCOMPARE(selectedView->viewport()->height(), selectedHeight);
    QCOMPARE(availableView->viewport()->height(), availableHeight);
}

void WorkPageTest::tagSelectorMatchesPythonGeometryAndNavigation()
{
    QList<darkeye::TagOption> tags{
        {1, QStringLiteral("甲"), QStringLiteral("类型一"), QStringLiteral("#ff0000"), {}, QStringLiteral("互斥")},
        {2, QStringLiteral("乙"), QStringLiteral("类型一"), QStringLiteral("#ff0000"), {}, QStringLiteral("互斥")},
        {3, QStringLiteral("丙"), QStringLiteral("类型二"), QStringLiteral("#ff0000")}};
    for (int i = 4; i < 60; ++i)
        tags.append({i, QStringLiteral("较长的标签%1").arg(i), QStringLiteral("类型一"), QStringLiteral("#ff0000")});
    darkeye::WorkTagSelector selector(tags);
    selector.setMutexCheckEnabled(false); // Python's tag management allows batch selection of conflicting tags.
    selector.resize(selector.width(), 220);
    selector.show();
    QTest::qWait(20);
    auto *tabs = selector.findChild<QTabWidget *>();
    auto *view = qobject_cast<QGraphicsView *>(tabs->widget(0));
    QVERIFY(view != nullptr);
    const auto cardFor = [](QGraphicsView *list, qint64 id) -> QGraphicsObject * {
        for (auto *item : list->scene()->items())
            if (auto *object = item->toGraphicsObject();
                object && object->objectName() == QStringLiteral("WorkTag_%1").arg(id))
                return object;
        return nullptr;
    };
    auto *first = cardFor(view, 1);
    QVERIFY(first != nullptr);
    QFont font(QStringLiteral("KaiTi"), 14);
    font.setBold(true);
    const QFontMetrics metrics(font);
    const qreal expectedHeight = metrics.height() + 27 * .8 - metrics.descent() * .3
                                 - metrics.horizontalAdvance(QChar(0x7532)) * .1;
    QVERIFY(qAbs(first->boundingRect().height() - expectedHeight) < .001);
    const auto hasPaintedColor = [first](const QColor &color) {
        QImage image(first->boundingRect().size().toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        first->paint(&painter, nullptr, nullptr);
        painter.end();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == color)
                    return true;
        return false;
    };
    QVERIFY(hasPaintedColor(Qt::white)); // Red uses white text under Python's weighted luminance rule.
    QGraphicsSceneHoverEvent hoverEnter(QEvent::GraphicsSceneHoverEnter);
    view->scene()->sendEvent(first, &hoverEnter);
    QVERIFY(hasPaintedColor(QColor("#C5C5C5")));
    QGraphicsSceneHoverEvent hoverLeave(QEvent::GraphicsSceneHoverLeave);
    view->scene()->sendEvent(first, &hoverLeave);
    QCOMPARE(first->pos(), QPointF(2, 2));
    const auto availableBounds = view->scene()->itemsBoundingRect();
    const int initialViewportWidth = view->viewport()->width();
    const qreal initialRightInset = initialViewportWidth - availableBounds.right();
    QVERIFY(initialRightInset >= 2.0);
    QCOMPARE(view->sceneRect().bottom() - availableBounds.bottom(), 2.0);
    const auto *second = cardFor(view, 2);
    QVERIFY(second != nullptr);
    QCOMPARE(first->boundingRect().width(), 27.0);
    QCOMPARE(second->boundingRect().width(), 27.0);
    QCOMPARE(second->pos().x() - first->sceneBoundingRect().right(), 2.0);
    selector.setAvailablePanelWidth(300);
    QTRY_VERIFY(view->viewport()->width() > initialViewportWidth);
    const qreal rightInset = view->viewport()->width() - view->scene()->itemsBoundingRect().right();
    QVERIFY(rightInset >= 2.0);
    QVERIFY(rightInset != initialRightInset);
    QCOMPARE(first->boundingRect().width(), 27.0);
    QCOMPARE(second->pos().x() - first->sceneBoundingRect().right(), 2.0);
    for (qint64 id : {1, 2})
    {
        auto *card = cardFor(view, id);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                          view->mapFromScene(card->sceneBoundingRect().center()));
    }
    QCOMPARE(selector.selectedIds(), (QList<qint64>{1, 2}));
    auto *scrollBar = view->verticalScrollBar();
    QVERIFY(scrollBar->maximum() > 0);
    QVERIFY(!scrollBar->isVisible());
    const QPoint wheelPosition = view->viewport()->rect().center();
    const auto scrollWithWheel = [&](int angle) {
        QWheelEvent wheel(wheelPosition, view->viewport()->mapToGlobal(wheelPosition),
                          {}, QPoint(0, angle), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &wheel);
    };
    for (int step = 0; step < 100 && scrollBar->value() < scrollBar->maximum(); ++step)
        scrollWithWheel(-120);
    QCOMPARE(scrollBar->value(), scrollBar->maximum());
    scrollWithWheel(120);
    QVERIFY(scrollBar->value() < scrollBar->maximum());
    QVERIFY(scrollBar->value() > 0);
    auto *tabBar = tabs->tabBar();
    QTest::mouseClick(tabBar, Qt::LeftButton, Qt::NoModifier, tabBar->tabRect(1).center());
    QCOMPARE(tabs->currentIndex(), 1);
    QTest::mouseClick(tabBar, Qt::LeftButton, Qt::NoModifier, tabBar->tabRect(0).center());
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(view->verticalScrollBar()->value(), 0);
    auto *search = findWorkControl<QLineEdit>(&selector, QStringLiteral("WorkTagSearch"));
    search->setText(QStringLiteral("标签30"));
    auto *middle = cardFor(view, 30);
    QVERIFY(middle != nullptr);
    QVERIFY(qAbs(view->mapFromScene(middle->sceneBoundingRect().center()).y()
                 - view->viewport()->rect().center().y()) <= 2);
    search->setText(QStringLiteral("标签59"));
    auto *last = cardFor(view, 59);
    QVERIFY(last != nullptr);
    QCOMPARE(scrollBar->value(), scrollBar->maximum());
    QVERIFY(view->viewport()->rect().contains(view->mapFromScene(last->sceneBoundingRect().center())));
    selector.setLoader([tags] { return tags; });
    selector.reloadTags();
    QCOMPARE(search->text(), QStringLiteral("标签59"));
    QCOMPARE(selector.selectedIds(), (QList<qint64>{1, 2}));

    selector.setSelectedColumnLayout(darkeye::WorkTagSelector::SelectedColumnLayout::Compact);
    auto *selectedView = selector.findChild<QGraphicsView *>(QStringLiteral("WorkTagList"));
    QVERIFY(selectedView != nullptr);
    for (const bool withScrollbar : {false, true})
    {
        QList<qint64> ids{1, 2};
        if (withScrollbar)
            for (qint64 id = 4; id < 20; ++id)
                ids.append(id);
        selector.setSelectedIds(ids);
        QTRY_COMPARE(selectedView->verticalScrollBar()->isVisible(), withScrollbar);
        const QRectF bounds = selectedView->scene()->itemsBoundingRect();
        QCOMPARE(bounds.topLeft(), QPointF(2, 2));
        QTRY_COMPARE(selectedView->viewport()->width() - bounds.right(), 2.0);
        QCOMPARE(selectedView->sceneRect().bottom() - bounds.bottom(), 2.0);
    }
}

void WorkPageTest::tagSelectionInsetsMatchAvailableInEditorAndManagement()
{
    const QString previousStyle = qApp->styleSheet();
    const auto restoreStyle = qScopeGuard([previousStyle] { qApp->setStyleSheet(previousStyle); });
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(QDir(directory.path()).filePath("public.db"), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    darkeye::CrawlerScheduler scheduler(QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler);
    editor.initialize();
    auto *editorSelector = editor.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(editorSelector != nullptr);
    auto *editorSelected = editorSelector->findChild<QGraphicsView *>(QStringLiteral("WorkTagList"));
    QVERIFY(editorSelected != nullptr);
    auto *editorScroll = qobject_cast<QScrollArea *>(editorSelector->parentWidget()->parentWidget());
    QVERIFY(editorScroll != nullptr);
    QCOMPARE(editorScroll->widget(), editorSelector);
    QCOMPARE(editorScroll->parentWidget()->layout()->contentsMargins(), QMargins());
    editor.resize(1200, 800);
    editor.show();
    auto *editorAvailable = editorSelector->findChild<QWidget *>(QStringLiteral("WorkTagAvailablePanel"));
    QVERIFY(editorAvailable != nullptr);
    QCOMPARE(editorAvailable->minimumWidth(), 200);
    QTRY_COMPARE(editorAvailable->width(),
                 qBound(200, editorScroll->viewport()->width() - editorSelected->width() - 5, 700));
    QCOMPARE(editorAvailable->x() - editorSelected->geometry().right() - 1, 1);
    darkeye::TagManagementWidget management(connection.database(), themes);
    management.initialize();
    auto *managementSelector = management.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(managementSelector != nullptr);
    auto *managementAvailable = managementSelector->findChild<QWidget *>(QStringLiteral("WorkTagAvailablePanel"));
    QVERIFY(managementAvailable != nullptr);
    QCOMPARE(managementAvailable->minimumWidth(), 200);
    QCOMPARE(managementAvailable->maximumWidth(), 400);

    QList<darkeye::TagOption> layoutTags;
    QList<qint64> selectedIds;
    for (qint64 id = 1; id <= 60; ++id)
    {
        layoutTags.append({id, QStringLiteral("测试标签%1").arg(id), QStringLiteral("类型一"), {}});
        selectedIds.append(id);
    }
    management.resize(management.minimumSizeHint().width(), 600);
    management.show();
    QTRY_COMPARE(managementAvailable->width(), 200);
    management.resize(1200, 600);
    for (auto *testedSelector : {editorSelector, managementSelector})
    {
        testedSelector->setLoader([layoutTags] { return layoutTags; });
        testedSelector->reloadTags();
        auto *availableView = testedSelector->findChild<QGraphicsView *>(QStringLiteral("WorkAvailableTagList"));
        auto *selectedView = testedSelector->findChild<QGraphicsView *>(QStringLiteral("WorkTagList"));
        QVERIFY(availableView != nullptr);
        QVERIFY(selectedView != nullptr);
        const qreal availableTop = availableView->scene()->itemsBoundingRect().top();
        QVERIFY(availableTop > 0);
        QSet<qreal> availableColumns;
        for (const auto *item : availableView->scene()->items())
            availableColumns.insert(item->pos().x());
        auto availablePositions = availableColumns.values();
        std::sort(availablePositions.begin(), availablePositions.end());
        QVERIFY(availablePositions.size() > 1);
        QCOMPARE(availablePositions.first(), availableTop);
        for (qsizetype i = 1; i < availablePositions.size(); ++i)
            QCOMPARE(availablePositions.at(i) - availablePositions.at(i - 1), 29.0);
        for (const bool withScrollbar : {false, true})
        {
            // Leave some tags available when filling the selected view enough to scroll.
            testedSelector->setSelectedIds(selectedIds.mid(0, withScrollbar ? 55 : 3));
            QTRY_COMPARE(selectedView->verticalScrollBar()->isVisible(), withScrollbar);
            const auto bounds = selectedView->scene()->itemsBoundingRect();
            QSet<qreal> columns;
            for (const auto *item : selectedView->scene()->items())
                columns.insert(item->pos().x());
            QCOMPARE(columns.size(), 3);
            QCOMPARE(bounds.left(), availableTop);
            QCOMPARE(bounds.top(), availableTop);
            QTRY_COMPARE(selectedView->viewport()->width() - bounds.right(), availableTop);
            QVERIFY(columns.contains(bounds.left() + 29));
            QVERIFY(columns.contains(bounds.left() + 58));
            QCOMPARE(selectedView->sceneRect().bottom() - bounds.bottom(), availableTop);
        }
    }
}

void WorkPageTest::tagSelectorPageWidthsAndSearchStyle()
{
    const QString previousStyle = qApp->styleSheet();
    const auto restoreStyle = qScopeGuard([previousStyle] { qApp->setStyleSheet(previousStyle); });
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    QList<darkeye::TagOption> tags{
        {1, QStringLiteral("标签甲"), QStringLiteral("类型一"), QStringLiteral("#bcf547")},
        {2, QStringLiteral("标签乙"), QStringLiteral("类型一"), QStringLiteral("#aaaaff")},
        {3, QStringLiteral("标签丙"), QStringLiteral("类型一"), QStringLiteral("#f7ec81")},
        {4, QStringLiteral("标签丁"), QStringLiteral("类型二"), QStringLiteral("#aaaaff")}};
    for (int id = 5; id < 19; ++id)
        tags.append({id, QStringLiteral("标签%1").arg(id), QStringLiteral("类型一"), QStringLiteral("#a7d5c5")});
    darkeye::WorkTagSelector selector(tags, &themes);
    selector.resize(selector.width(), 500);
    selector.setSelectedIds({1, 2, 3});
    selector.show();
    auto *search = findWorkControl<QLineEdit>(&selector, QStringLiteral("WorkTagSearch"));
    QVERIFY(search != nullptr);
    QCOMPARE(search->objectName(), QStringLiteral("DesignInput"));
    QCOMPARE(findWorkControl<QLabel>(&selector, QStringLiteral("WorkTagSearchResult"))->objectName(),
             QStringLiteral("DesignLabel"));
    QCOMPARE(findWorkControl<QPushButton>(&selector, QStringLiteral("WorkTagSearchNext"))->objectName(),
             QStringLiteral("DesignIconPushButton"));
    QWidget referenceHost;
    referenceHost.resize(400, 100);
    QLineEdit reference(&referenceHost);
    reference.setPlaceholderText(QStringLiteral("搜索"));
    reference.setClearButtonEnabled(true);
    search->setFocusPolicy(Qt::NoFocus);
    reference.setFocusPolicy(Qt::NoFocus);
    for (auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark})
    {
        QVERIFY(themes.setTheme(theme));
        QTest::qWait(20);
        reference.resize(search->size());
        referenceHost.show();
        reference.show();
        reference.clearFocus();
        search->clearFocus();
        QTest::qWait(20);
        QCOMPARE(search->sizeHint().height(), reference.sizeHint().height());
        QCOMPARE(search->grab().toImage(), reference.grab().toImage());
        reference.hide();
        referenceHost.hide();
        auto *selected = selector.findChild<QGraphicsView *>(QStringLiteral("WorkTagList"));
        QSet<qreal> columns;
        QRectF cardBounds;
        for (auto *item : selected->scene()->items())
        {
            if (item->isVisible())
            {
                columns.insert(item->pos().x());
                cardBounds = cardBounds.united(item->sceneBoundingRect());
            }
        }
        QCOMPARE(columns.size(), 3);
        QCOMPARE(cardBounds.left(), cardBounds.top());
        QCOMPARE(selected->viewport()->width() - cardBounds.right(), cardBounds.top());
        const QString output = qEnvironmentVariable("DARKEYE_TAG_SELECTOR_PREVIEW_DIR");
        if (!output.isEmpty())
        {
            QDir().mkpath(output);
            QVERIFY(selector.grab().save(QDir(output).filePath(
                theme == darkeye::ThemeId::Light ? "tag-selector-light.png" : "tag-selector-dark.png")));
        }
    }
}

void WorkPageTest::editorCompletesExistingDirectorsAndRefreshes()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                            &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("DEMO-001");
    work.director = QStringLiteral("Existing Director");
    const auto workId = repository.insertComplete(work, {}, {}, {}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::CrawlerScheduler scheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler);
    QVERIFY(editor.loadWork(*workId));
    auto *director = findWorkControl<darkeye::CompleterLineEdit>(
        &editor, QStringLiteral("WorkDirectorInput"));
    QVERIFY(director != nullptr);
    QTRY_COMPARE(director->items(), QStringList{work.director});
    QCOMPARE(director->text(), work.director);
    QCOMPARE(director->completer()->caseSensitivity(), Qt::CaseInsensitive);
    QCOMPARE(director->completer()->filterMode(), Qt::MatchContains);
    director->completer()->setCompletionPrefix(QStringLiteral("isting"));
    QCOMPARE(director->completer()->completionCount(), 1);
    QCOMPARE(director->completer()->currentCompletion(), work.director);

    work.serialNumber = QStringLiteral("DEMO-002");
    work.director = QStringLiteral("New Director");
    QVERIFY2(repository.insertComplete(work, {}, {}, {}, &errorMessage).has_value(),
             qPrintable(errorMessage));
    director->setText(QStringLiteral("Unsaved director"));
    editor.refreshCompletions();
    QTRY_VERIFY(director->items().contains(work.director));
    QCOMPARE(director->text(), QStringLiteral("Unsaved director"));
}

void WorkPageTest::loadingCrLfStoriesDoesNotMarkWorkModified()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));

    darkeye::Work work;
    work.serialNumber = QStringLiteral("IPZZ-729");
    work.chineseStory = QStringLiteral("第一行\r\n第二行");
    work.japaneseStory = QStringLiteral("一行目\r\n二行目");
    darkeye::WorkRepository repository(connection.database());
    const auto workId = repository.insertComplete(work, {}, {}, {}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    darkeye::CrawlerScheduler crawlerScheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themeService, crawlerScheduler,
                                    QDir(temporaryDirectory.path()).filePath("covers"),
                                    QDir(temporaryDirectory.path()).filePath("fanart"));

    QVERIFY(editor.loadWork(*workId));
    auto *save = editor.findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(save != nullptr);
    QVERIFY(!save->isEnabled());

    bool checkedChineseStory = false;
    bool checkedJapaneseStory = false;
    for (QPlainTextEdit *edit : editor.findChildren<QPlainTextEdit *>())
    {
        if (edit->toPlainText() == QStringLiteral("第一行\n第二行"))
        {
            checkedChineseStory = true;
            QVERIFY(!edit->property("addWorkModified").toBool());
        }
        if (edit->toPlainText() == QStringLiteral("一行目\n二行目"))
        {
            checkedJapaneseStory = true;
            QVERIFY(!edit->property("addWorkModified").toBool());
        }
    }
    QVERIFY(checkedChineseStory);
    QVERIFY(checkedJapaneseStory);
}

void WorkPageTest::personSelectionChangesRenderWarningFrame()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                            &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));
    darkeye::Work work;
    work.serialNumber = QStringLiteral("FRAME-001");
    darkeye::WorkRepository repository(connection.database());
    const auto workId = repository.insertComplete(work, {}, {}, {}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::CrawlerScheduler scheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler);
    QVERIFY(editor.loadWork(*workId));
    editor.resize(1400, 1000);
    editor.show();
    auto *save = editor.findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(save != nullptr);

    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark})
    {
        QVERIFY(themes.setTheme(theme));
        const QColor orange(themes.currentTokens().modified);
        for (const auto &name : {QStringLiteral("WorkActressSelector"),
                                 QStringLiteral("WorkActorSelector")})
        {
            auto *selector = editor.findChild<darkeye::PersonTransferSelector *>(name);
            QVERIFY(selector != nullptr);
            selector->setMinimumSize(220, 300);
            selector->setOptions({{1, QStringLiteral("Test person"), {}}});
            QApplication::processEvents();
            const auto lists = selector->findChildren<QListWidget *>();
            QCOMPARE(lists.size(), 2);
            auto *selected = lists.constLast();
            QVERIFY(selected->property("personSelectedList").toBool());
            const auto hasOrangeEdges = [orange](QWidget *widget) {
                const QImage image = widget->grab().toImage();
                if (image.isNull()) return false;
                return image.pixelColor(0, image.height() / 2) == orange
                    && image.pixelColor(image.width() - 1, image.height() / 2) == orange
                    && image.pixelColor(image.width() / 2, 0) == orange
                    && image.pixelColor(image.width() / 2, image.height() - 1) == orange;
            };
            QVERIFY(!selector->property("addWorkModified").toBool());
            QVERIFY(!hasOrangeEdges(selected));
            selector->selectId(1);
            QVERIFY(selector->property("addWorkModified").toBool());
            QVERIFY(save->isEnabled());
            selected->setFocus();
            QApplication::processEvents();
            QVERIFY(hasOrangeEdges(selected));
            QVERIFY(!hasOrangeEdges(lists.constFirst()));
            QVERIFY(!hasOrangeEdges(selector));
            selector->clearSelection();
            QVERIFY(!selector->property("addWorkModified").toBool());
            QVERIFY(!save->isEnabled());
            QVERIFY(!hasOrangeEdges(selected));
        }
    }
}

void WorkPageTest::fanartChangesRenderWarningFrame()
{
    const QString previousStyle = qApp->styleSheet();
    const auto restoreStyle = qScopeGuard([previousStyle] { qApp->setStyleSheet(previousStyle); });
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(QDir(directory.path()).filePath("public.db"), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("FANART-FRAME-001");
    const auto workId = repository.insertComplete(work, {}, {}, {}, &error);
    QVERIFY2(workId.has_value(), qPrintable(error));
    darkeye::ThemeService themes(*qApp);
    darkeye::CrawlerScheduler scheduler(QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler);
    editor.resize(1400, 1000);
    editor.show();
    auto *fanart = editor.findChild<darkeye::FanartStripWidget *>();
    QVERIFY(fanart != nullptr);
    const auto hasOrangeEdges = [fanart, &themes] {
        const QImage image = fanart->grab().toImage();
        const QColor orange(themes.currentTokens().modified);
        return !image.isNull()
            && image.pixelColor(0, image.height() / 2) == orange
            && image.pixelColor(image.width() - 1, image.height() / 2) == orange
            && image.pixelColor(image.width() / 2, 0) == orange
            && image.pixelColor(image.width() / 2, image.height() - 1) == orange;
    };
    for (auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark})
    {
        QVERIFY(themes.setTheme(theme));
        QVERIFY(editor.loadWork(*workId));
        QApplication::processEvents();
        QVERIFY(!hasOrangeEdges());
        QVERIFY(fanart->addImageUrl(QStringLiteral("https://example.invalid/scene.jpg"), &error));
        QVERIFY(fanart->property("addWorkModified").toBool());
        QApplication::processEvents();
        QVERIFY(hasOrangeEdges());
        QVERIFY(editor.loadWork(*workId));
        QApplication::processEvents();
        QVERIFY(!fanart->property("addWorkModified").toBool());
        QVERIFY(!hasOrangeEdges());
    }
}

void WorkPageTest::tagSelectionChangesRenderWarningFrame()
{
    const QString previousStyle = qApp->styleSheet();
    const auto restoreStyle = qScopeGuard([previousStyle] { qApp->setStyleSheet(previousStyle); });
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(QDir(directory.path()).filePath("public.db"), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    QSqlQuery seed(connection.database());
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO tag(tag_id,tag_name,tag_type_id,color) VALUES"
                                    "(90001,'原标签',1,'#ccccff'),(90002,'新标签',1,'#aaffcc')")));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("TAG-FRAME-001");
    const auto workId = repository.insertComplete(work, {}, {}, {90001}, &error);
    QVERIFY2(workId.has_value(), qPrintable(error));
    darkeye::ThemeService themes(*qApp);
    darkeye::CrawlerScheduler scheduler(QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler);
    QVERIFY(editor.loadWork(*workId));
    editor.resize(1400, 1000);
    editor.show();
    auto *selector = editor.findChild<darkeye::WorkTagSelector *>();
    QVERIFY(selector != nullptr);
    auto *selected = selector->findChild<QGraphicsView *>(QStringLiteral("WorkTagList"));
    QVERIFY(selected != nullptr);
    auto *save = editor.findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(save != nullptr);
    const auto hasBorderColor = [](QWidget *widget, const QColor &color) {
        const QImage image = widget->grab().toImage();
        if (image.isNull()) return false;
        for (int y = 0; y < image.height(); ++y)
            if (image.pixelColor(0, y) == color || image.pixelColor(image.width() - 1, y) == color)
                return true;
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, 0) == color || image.pixelColor(x, image.height() - 1) == color)
                return true;
        return false;
    };
    for (auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark})
    {
        QVERIFY(themes.setTheme(theme));
        const QColor warning(themes.currentTokens().warning);
        const QColor normal(themes.currentTokens().border);
        QVERIFY(editor.loadWork(*workId));
        QApplication::processEvents();
        QVERIFY(!hasBorderColor(selected, warning));
        QVERIFY(hasBorderColor(selected, normal));
        const QSize originalSize = selector->size();
        selector->setSelectedIds({90001, 90002});
        QVERIFY(selector->property("addWorkModified").toBool());
        QVERIFY(selected->property("addWorkModified").toBool());
        QVERIFY(save->isEnabled());
        selected->setFocus();
        QApplication::processEvents();
        QVERIFY(hasBorderColor(selected, warning));
        QCOMPARE(selector->size(), originalSize);
        for (auto *view : selector->findChildren<QGraphicsView *>(QStringLiteral("WorkAvailableTagList")))
        {
            QVERIFY(hasBorderColor(view, normal));
            QVERIFY(!hasBorderColor(view, warning));
        }
        // Reloading the theme must retain the warning on the selected view.
        QVERIFY(themes.setTheme(theme));
        QVERIFY(hasBorderColor(selected, warning));
        selector->setSelectedIds({90001});
        QVERIFY(!hasBorderColor(selected, warning));
        QVERIFY(!save->isEnabled());
        selector->clearSelection();
        QVERIFY(hasBorderColor(selected, warning));
        QVERIFY(save->isEnabled());
        QVERIFY(editor.loadWork(*workId));
        QVERIFY(!hasBorderColor(selected, warning));
        QVERIFY(!selected->property("addWorkModified").toBool());
        QVERIFY(!save->isEnabled());
    }
}

void WorkPageTest::droppingOriginalAfterSaveDoesNotEnableModifyAction_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<bool>("bitmapDrop");
    QTest::newRow("jpeg-file") << QStringLiteral("JPEG") << false;
    QTest::newRow("jpeg-bitmap") << QStringLiteral("JPEG") << true;
    QTest::newRow("png-file") << QStringLiteral("PNG") << false;
    QTest::newRow("png-bitmap") << QStringLiteral("PNG") << true;
}

void WorkPageTest::droppingOriginalAfterSaveDoesNotEnableModifyAction()
{
    QFETCH(QString, format);
    QFETCH(bool, bitmapDrop);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath("public.db"), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    QImage image(180, 120, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor((x * 7 + y) % 256,
                                            (y * 11 + x) % 256, (x + y * 3) % 256));
    const QString browserPath = directory.filePath("browser-image");
    QVERIFY(image.save(browserPath, format.toLatin1().constData(), 85));
    darkeye::ThemeService themes(*qApp);
    darkeye::CrawlerScheduler scheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler,
                                    directory.filePath("covers"));
    editor.beginCreate();
    auto *serial = findWorkControl<QLineEdit>(&editor, QStringLiteral("WorkSerialInput"));
    auto *dropWidget = editor.findChild<darkeye::ImageDropWidget *>("WorkCoverDropWidget");
    auto *save = editor.findChild<QPushButton *>("WorkSaveButton");
    QVERIFY(serial != nullptr);
    QVERIFY(dropWidget != nullptr);
    QVERIFY(save != nullptr);
    QMimeData mime;
    if (bitmapDrop)
    {
        mime.setUrls({QUrl(QStringLiteral("https://example.test/cover"))});
        mime.setImageData(QImage(browserPath));
    }
    else
        mime.setUrls({QUrl::fromLocalFile(browserPath)});
    const auto dropOriginal = [&] {
        QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(dropWidget, &enter);
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(dropWidget, &drop);
        return enter.isAccepted() && drop.isAccepted();
    };
    serial->setText(QStringLiteral("REPEAT-001"));
    QVERIFY(dropOriginal());
    QSignalSpy saved(&editor, &darkeye::AddWorkTabPage3::workSaved);
    save->click();
    QCOMPARE(saved.count(), 1);
    QVERIFY(!save->isEnabled());
    const QString savedPath = dropWidget->imagePath();
    QVERIFY(dropOriginal());
    QVERIFY(!dropWidget->isDirty());
    QVERIFY(!save->isEnabled());
    QCOMPARE(dropWidget->imagePath(), savedPath);
    // The comparison must also work after reopening the saved work.
    QVERIFY(editor.loadWork(saved.first().at(0).toLongLong()));
    QVERIFY(dropOriginal());
    QVERIFY(!dropWidget->isDirty());
    QVERIFY(!dropWidget->property("addWorkModified").toBool());
    QVERIFY(!save->isEnabled());
}

void WorkPageTest::identicalCoverDoesNotEnableModifyAction()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(QDir(directory.path()).filePath("public.db"), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    QImage original(600, 400, QImage::Format_RGB32);
    original.fill(QColor(20, 80, 160));
    const QString originalName = QStringLiteral("original.png");
    QVERIFY(original.save(directory.filePath(originalName)));
    const QString replacementPath = directory.filePath(QStringLiteral("browser.bmp"));
    QVERIFY(original.save(replacementPath));
    darkeye::Work work;
    work.serialNumber = QStringLiteral("SAME-001");
    work.imageUrl = originalName;
    darkeye::WorkRepository repository(connection.database());
    const auto id = repository.insertComplete(work, {}, {}, {}, &error);
    QVERIFY2(id.has_value(), qPrintable(error));
    darkeye::ThemeService themes(*qApp);
    darkeye::CrawlerScheduler scheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler, directory.path());
    QVERIFY(editor.loadWork(*id));
    auto *imageDrop = editor.findChild<darkeye::ImageDropWidget *>("WorkCoverDropWidget");
    auto *save = editor.findChild<QPushButton *>("WorkSaveButton");
    QVERIFY(imageDrop != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(!save->isEnabled());
    QVERIFY(imageDrop->setPendingImagePath(replacementPath));
    QVERIFY(!imageDrop->isDirty());
    QVERIFY(!imageDrop->property("addWorkModified").toBool());
    QVERIFY(!save->isEnabled());
    QCOMPARE(imageDrop->imagePath(), originalName);

    original.setPixelColor(599, 399, QColor(21, 80, 160));
    QVERIFY(original.save(replacementPath));
    QVERIFY(imageDrop->setPendingImagePath(replacementPath));
    QVERIFY(imageDrop->isDirty());
    QVERIFY(save->isEnabled());
    QVERIFY(imageDrop->property("addWorkModified").toBool());
    QVERIFY(imageDrop->setPendingImagePath(directory.filePath(originalName)));
    QVERIFY(!save->isEnabled());
    QVERIFY(!imageDrop->property("addWorkModified").toBool());
}

void WorkPageTest::downloadedCoverEnablesModifyAction()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));

    darkeye::Work work;
    work.serialNumber = QStringLiteral("IPZZ-730");
    work.imageUrl = QStringLiteral("IPZZ-730.jpg");
    darkeye::WorkRepository repository(connection.database());
    const auto workId = repository.insertComplete(work, {}, {}, {}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    darkeye::CrawlerScheduler crawlerScheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themeService, crawlerScheduler,
                                    QDir(temporaryDirectory.path()).filePath("covers"),
                                    QDir(temporaryDirectory.path()).filePath("fanart"));

    QVERIFY(editor.loadWork(*workId));
    auto *imageDrop = editor.findChild<darkeye::ImageDropWidget *>(
        QStringLiteral("WorkCoverDropWidget"));
    auto *qualityBadge = editor.findChild<QLabel *>(QStringLiteral("coverQualityBadge"));
    auto *save = editor.findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(imageDrop != nullptr);
    QVERIFY(qualityBadge != nullptr);
    QVERIFY(save != nullptr);
    const auto tokens = themeService.currentTokens();
    QVERIFY(qualityBadge->styleSheet().contains(QStringLiteral("background-color: %1").arg(tokens.background)));
    QVERIFY(qualityBadge->styleSheet().contains(QStringLiteral("color: %1").arg(tokens.warning)));
    QVERIFY(qualityBadge->styleSheet().contains(QStringLiteral("border: %1 solid %2").arg(tokens.borderWidth, tokens.warning)));
    QVERIFY(!save->isEnabled());

    const QString downloadedCover =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("downloaded-cover.jpg"));
    QImage lowQualityCover(80, 120, QImage::Format_RGB32);
    lowQualityCover.fill(Qt::blue);
    QVERIFY(lowQualityCover.save(downloadedCover, "JPEG", 80));
    editor.show();
    QVERIFY(imageDrop->setPendingImagePath(downloadedCover));
    QApplication::processEvents();

    QVERIFY(imageDrop->isDirty());
    QVERIFY(imageDrop->property("addWorkModified").toBool());
    QVERIFY(save->isEnabled());
    QCOMPARE(save->text(), QStringLiteral("修改"));
    QVERIFY(qualityBadge->isVisible());
    QCOMPARE(qualityBadge->accessibleName(), QStringLiteral("非高清图"));

    const QImage renderedBadge = qualityBadge->grab().toImage();
    const QColor warning(QStringLiteral("#ed6c02"));
    bool foundWarningTextPixel = false;
    for (int y = renderedBadge.height() / 4;
         y < renderedBadge.height() * 3 / 4 && !foundWarningTextPixel; ++y)
        for (int x = renderedBadge.width() / 4; x < renderedBadge.width() * 3 / 4; ++x)
            if (renderedBadge.pixelColor(x, y) == warning)
            {
                foundWarningTextPixel = true;
                break;
            }
    if (QApplication::platformName() != QStringLiteral("offscreen"))
        QVERIFY2(foundWarningTextPixel,
                 "the low-quality badge must render warning-coloured text");
}

void WorkPageTest::addWorkBasicInfoSerialMatchesPythonBehavior()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    darkeye::CrawlerScheduler crawlerScheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themeService, crawlerScheduler);
    editor.beginCreate();

    auto *serial = findWorkControl<QLineEdit>(&editor, QStringLiteral("WorkSerialInput"));
    auto *director = findWorkControl<QLineEdit>(&editor, QStringLiteral("WorkDirectorInput"));
    auto *save = editor.findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(serial != nullptr);
    QVERIFY(director != nullptr);
    QVERIFY(save != nullptr);

    serial->setText(QStringLiteral("  abp-001  "));
    QCOMPARE(serial->text(), QStringLiteral("ABP-001"));
    QVERIFY(save->isEnabled());
    director->setText(QStringLiteral("待清除导演"));
    serial->setText(QStringLiteral("new-002"));
    QCOMPARE(serial->text(), QStringLiteral("NEW-002"));
    QCOMPARE(director->text(), QString());
    QVERIFY(save->isEnabled());
}

void WorkPageTest::addWorkBasicInfoKeepsRowsHorizontalWhenNarrow()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    darkeye::CrawlerScheduler crawlerScheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themeService, crawlerScheduler);

    editor.initialize();
    QWidget *basicPane = nullptr;
    for (QWidget *widget : editor.findChildren<QWidget *>())
    {
        if (widget->property("addwork_slot").toString() == QStringLiteral("basic"))
        {
            basicPane = widget;
            break;
        }
    }
    QVERIFY(basicPane != nullptr);
    auto *form = basicPane->findChild<QFormLayout *>();
    QVERIFY(form != nullptr);
    QCOMPARE(form->rowWrapPolicy(), QFormLayout::DontWrapRows);
}

void WorkPageTest::editorAutomaticallySavesLayoutWithoutSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataDir = qgetenv("DARKEYE_DATA_DIR");
    qputenv("DARKEYE_DATA_DIR", QFile::encodeName(directory.path()));
    const auto restoreDataDir = qScopeGuard([previousDataDir] {
        if (previousDataDir.isNull())
            qunsetenv("DARKEYE_DATA_DIR");
        else
            qputenv("DARKEYE_DATA_DIR", previousDataDir);
    });
    const darkeye::settings::Paths paths;
    QFile navigationFile(paths.crawlerNavButtonsFile());
    QVERIFY(navigationFile.open(QIODevice::WriteOnly));
    const QByteArray navigationConfig = R"([{"name":"Test link","url":"https://example.com"}])";
    QCOMPARE(navigationFile.write(navigationConfig), navigationConfig.size());
    navigationFile.close();
    const QString path = paths.addWorkWorkspaceLayoutFile();
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY2(connection.open(QDir(directory.path()).filePath(QStringLiteral("public.db")), false, &error),
             qPrintable(error));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error),
             qPrintable(error));
    darkeye::ThemeService themes(*qApp);
    darkeye::CrawlerScheduler scheduler(QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    const QString settingsId = QStringLiteral("addwork_settings");
    const QString navigationId = QStringLiteral("addwork_nav");
    const QString basicId = QStringLiteral("addwork_basic");
    QString destinationId;
    {
        darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler);
        editor.initialize();
        auto *workspace = editor.findChild<darkeye::myads::WorkspaceWidget *>();
        QVERIFY(workspace);
        QVERIFY(!workspace->findPaneByContentId(settingsId));
        auto *navigation = workspace->findPaneByContentId(navigationId);
        auto *basic = workspace->findPaneByContentId(basicId);
        QVERIFY(navigation);
        QVERIFY(basic);
        auto *navigationScroll = qobject_cast<QScrollArea *>(navigation->contentWidget(navigationId));
        QVERIFY(navigationScroll);
        QCOMPARE(navigationScroll->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        destinationId = basic->paneId();
        QVERIFY(workspace->moveContent(navigation->paneId(), navigationId, destinationId,
                                       darkeye::myads::DropZone::Center));
        QTRY_VERIFY(QFileInfo::exists(path));
    }

    QFile layoutFile(path);
    QVERIFY(layoutFile.open(QIODevice::ReadOnly));
    auto saved = QJsonDocument::fromJson(layoutFile.readAll()).object();
    layoutFile.close();
    auto contents = saved.value(QStringLiteral("pane_contents")).toObject();
    auto tabs = contents.value(destinationId).toArray();
    tabs.prepend(QJsonObject{{QStringLiteral("addwork_slot"), QStringLiteral("settings")},
                             {QStringLiteral("content_id"), settingsId}});
    contents.insert(destinationId, tabs);
    saved.insert(QStringLiteral("pane_contents"), contents);
    QVERIFY(layoutFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray legacyLayout = QJsonDocument(saved).toJson();
    QCOMPARE(layoutFile.write(legacyLayout), legacyLayout.size());
    layoutFile.close();

    {
        darkeye::AddWorkTabPage3 editor(connection.database(), themes, scheduler);
        editor.initialize();
        auto *workspace = editor.findChild<darkeye::myads::WorkspaceWidget *>();
        QVERIFY(workspace);
        QVERIFY(!workspace->findPaneByContentId(settingsId));
        auto *destination = workspace->pane(destinationId);
        QVERIFY(destination);
        QCOMPARE(workspace->findPaneByContentId(navigationId), destination);
        QCOMPARE(destination->currentContentId(), navigationId);
        destination->setCurrentContentId(basicId);
    }

    QVERIFY(layoutFile.open(QIODevice::ReadOnly));
    const QByteArray updatedLayout = layoutFile.readAll();
    QVERIFY(!updatedLayout.contains(settingsId.toUtf8()));
    const auto metadata = QJsonDocument::fromJson(updatedLayout).object()
        .value(QStringLiteral("pane_metadata")).toObject();
    QCOMPARE(metadata.value(destinationId).toObject().value(QStringLiteral("current_content_id")).toString(),
             basicId);
}

void WorkPageTest::importsCoverAndRollsBackOnDatabaseFailure()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString coverDirectory = QDir(temporaryDirectory.path()).filePath("covers");
    const QString fanartDirectory = QDir(temporaryDirectory.path()).filePath("fanart");
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &errorMessage));
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    darkeye::WorkRepository repository(connection.database());

    const QString sourcePath = QDir(temporaryDirectory.path()).filePath("source.png");
    QImage source(40, 30, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(sourcePath));

    darkeye::CrawlerScheduler crawlerScheduler(
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    darkeye::AddWorkTabPage3 editor(connection.database(), themeService, crawlerScheduler,
                                    coverDirectory, fanartDirectory);
    editor.beginCreate();
    auto *serial = findWorkControl<QLineEdit>(&editor, QStringLiteral("WorkSerialInput"));
    auto *title = findWorkControl<QPlainTextEdit>(
        &editor, QStringLiteral("WorkChineseTitleInput"));
    auto *imageDrop = editor.findChild<darkeye::ImageDropWidget *>(QStringLiteral("WorkCoverDropWidget"));
    auto *fanart = findWorkControl<darkeye::FanartStripWidget>(
        &editor, QStringLiteral("WorkFanartStrip"));
    auto *save = editor.findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(serial != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(imageDrop != nullptr);
    QVERIFY(fanart != nullptr);
    QVERIFY(save != nullptr);
    QSignalSpy savedSpy(&editor, &darkeye::AddWorkTabPage3::workSaved);
    serial->setText(QStringLiteral("cover-001"));
    title->setPlainText(QStringLiteral("封面导入"));
    imageDrop->setImagePath(sourcePath);
    imageDrop->setDirty(true);
    QVERIFY(fanart->addImageUrl(QStringLiteral("https://example.invalid/scene.jpg"), &errorMessage));
    QVERIFY(fanart->addLocalImage(sourcePath, &errorMessage));
    // 保存按钮位于可停靠工作区的基础信息窗格；测试业务行为时直接触发其槽，
    // 不依赖当前活动标签页的可见状态。
    save->click();

    QCOMPARE(savedSpy.count(), 1);
    const qint64 savedId = savedSpy.first().at(0).toLongLong();
    const auto saved = repository.findById(savedId, &errorMessage);
    QVERIFY2(saved.has_value(), qPrintable(errorMessage));
    QCOMPARE(saved->imageUrl, QStringLiteral("COVER-001.jpg"));
    const QString savedCover = QDir(coverDirectory).filePath(saved->imageUrl);
    QVERIFY(QFileInfo::exists(savedCover));
    QImage imported(savedCover);
    QVERIFY(!imported.isNull());
    QList<darkeye::FanartEntry> savedFanart;
    QVERIFY(darkeye::FanartStripWidget::parseJson(saved->fanartJson, &savedFanart, &errorMessage));
    QCOMPARE(savedFanart.size(), 2);
    QCOMPARE(savedFanart.at(0).url, QStringLiteral("https://example.invalid/scene.jpg"));
    QVERIFY(savedFanart.at(1).file.startsWith(QStringLiteral("cover001_fa_")));
    const QString importedFanart = QDir(fanartDirectory).filePath(savedFanart.at(1).file);
    QVERIFY(QFileInfo::exists(importedFanart));

    darkeye::Work duplicate;
    duplicate.serialNumber = QStringLiteral("DUPLICATE");
    QVERIFY(repository.insertComplete(duplicate, {}, {}, {}, &errorMessage).has_value());
    QVERIFY(QDir().mkpath(coverDirectory));
    const QString existingCover = QDir(coverDirectory).filePath("DUPLICATE.jpg");
    QImage original(40, 30, QImage::Format_RGB32);
    original.fill(Qt::green);
    QVERIFY(original.save(existingCover, "JPEG", 100));

    editor.beginCreate();
    serial->setText(QStringLiteral("DUPLICATE"));
    imageDrop->setImagePath(sourcePath);
    imageDrop->setDirty(true);
    QVERIFY(fanart->addLocalImage(sourcePath, &errorMessage));
    const int fanartCountBeforeFailure = QDir(fanartDirectory).entryList({QStringLiteral("*.jpg")}, QDir::Files).size();
    QTest::mouseClick(save, Qt::LeftButton);

    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(QDir(fanartDirectory).entryList({QStringLiteral("*.jpg")}, QDir::Files).size(), fanartCountBeforeFailure);
    QImage restored(existingCover);
    QVERIFY(!restored.isNull());
    const QColor restoredColor = restored.pixelColor(restored.width() / 2, restored.height() / 2);
    QVERIFY(restoredColor.green() > restoredColor.blue());

    QVERIFY(editor.loadWork(savedId));
    QVERIFY(fanart->removeEntry(1));
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(savedSpy.count(), 2);
    QVERIFY(!QFileInfo::exists(importedFanart));
    const auto withoutLocalFanart = repository.findById(savedId, &errorMessage);
    QVERIFY(withoutLocalFanart.has_value());
    QList<darkeye::FanartEntry> remainingFanart;
    QVERIFY(darkeye::FanartStripWidget::parseJson(withoutLocalFanart->fanartJson, &remainingFanart, &errorMessage));
    QCOMPARE(remainingFanart.size(), 1);
    QCOMPARE(remainingFanart.first().url, QStringLiteral("https://example.invalid/scene.jpg"));
}

void WorkPageTest::normalWorkCardTitleFitsTwoLines()
{
    const auto titleName = QStringLiteral("WorkCardTitle");
    darkeye::WorkSummary work;
    work.chineseTitle = QStringLiteral("作品标题");
    darkeye::WorkCard shortCard(work, {}, true);
    const auto *shortTitle = shortCard.findChild<QLabel *>(titleName);
    QVERIFY(shortTitle);
    QCOMPARE(shortTitle->text(), work.chineseTitle);

    work.chineseTitle = QStringLiteral("很长的作品标题").repeated(20);
    darkeye::WorkCard longCard(work, {}, true);
    const auto *longTitle = longCard.findChild<QLabel *>(titleName);
    QVERIFY(longTitle);
    const auto lines = longTitle->text().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.last().endsWith(QChar(0x2026)));
    for (const auto &line : lines)
        QVERIFY(longTitle->fontMetrics().horizontalAdvance(line) <= longTitle->contentsRect().width());
    QVERIFY(longTitle->height() >= longTitle->sizeHint().height());
    QCOMPARE(longCard.height(), shortCard.height());

    darkeye::WorkCard waterfallCard(work, {}, false);
    const auto *waterfallTitle = waterfallCard.findChild<QLabel *>(titleName);
    QVERIFY(waterfallTitle);
    QCOMPARE(waterfallTitle->text(), work.chineseTitle);
    QVERIFY(waterfallTitle->wordWrap());
}

void WorkPageTest::workCardCopiesSerialAndActivatesOnlyCover()
{
    darkeye::WorkSummary work;
    work.id = 42;
    work.serialNumber = QStringLiteral("CARD-042");
    work.chineseTitle = QStringLiteral("作品标题");

    ContextMenuSpy contextMenuHost;
    darkeye::WorkCard card(work, {}, false, &contextMenuHost);
    card.show();
    QCoreApplication::processEvents();

    auto *serial = card.findChild<darkeye::ClickableLabel *>(
        QStringLiteral("WorkCardSerialNumber"));
    auto *cover = card.findChild<darkeye::AsyncImageLabel *>(
        QStringLiteral("WorkCardCover"));
    QVERIFY(serial != nullptr);
    QVERIFY(cover != nullptr);
    QSignalSpy activated(&card, &darkeye::WorkCard::activated);
    QSignalSpy editRequested(&card, &darkeye::WorkCard::editRequested);

    QTest::mouseClick(serial, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("CARD-042"));
    QCOMPARE(activated.count(), 0);
    QCOMPARE(editRequested.count(), 0);

    QTest::mouseClick(&card, Qt::LeftButton, Qt::NoModifier,
                      card.rect().bottomLeft() + QPoint(4, -4));
    QCOMPARE(activated.count(), 0);

    QTest::mouseClick(cover, Qt::LeftButton);
    QCOMPARE(activated.count(), 1);
    QTest::mouseClick(cover, Qt::RightButton);
    QCoreApplication::processEvents();
    QCOMPARE(editRequested.count(), 1);
    QContextMenuEvent contextMenu(QContextMenuEvent::Mouse, cover->rect().center(),
                                  cover->mapToGlobal(cover->rect().center()));
    QCoreApplication::sendEvent(cover, &contextMenu);
    QCOMPARE(contextMenuHost.contextMenuEvents, 0);

    QWidget host;
    auto *layout = new QHBoxLayout(&host);
    darkeye::WorkCard first(work, {}, true, &host);
    work.id = 43;
    darkeye::WorkCard second(work, {}, true, &host);
    layout->addWidget(&first);
    layout->addWidget(&second);
    host.show();
    first.setFocus();
    QTRY_VERIFY(first.hasFocus());
    QTest::keyClick(&first, Qt::Key_Right);
    QTRY_VERIFY(second.hasFocus());
    QTest::keyClick(&second, Qt::Key_Left);
    QTRY_VERIFY(first.hasFocus());
}

DARKEYE_TEST_MAIN(WorkPageTest, QApplication)
#include "WorkPageTest.moc"
