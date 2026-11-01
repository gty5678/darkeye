#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/TokenControls.h"
#include "graph/GraphManager.h"
#include "graph_view/ForceViewRhiWidget.h"
#include "graph_view/GraphViewWidget.h"
#include "ui/pages/ForceDirectPage.h"
#include "ui/pages/ForceViewSettingsPanel.h"

#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QEventLoop>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QtTest>

class GraphPageRenderingTest final : public QObject
{
    Q_OBJECT

private slots:
    void coveredRendererSubmitsFrame();
    void rendersWhenRendererExistsBeforeWindowShow();
    void loadsFirstGraphSnapshotAfterAnInitiallyEmptyView();
    void preparesLatestEgoGraphWhileHidden();
    void changingNodeColorsPreservesLayout();
    void hiddenManagerChangesPreserveLayout();
};

void GraphPageRenderingTest::hiddenManagerChangesPreserveLayout()
{
    darkeye::graph::GraphManager manager({}, {});
    QVERIFY(manager.upsertRuntimeNode(
        {QStringLiteral("w1"), QStringLiteral("Original"), QStringLiteral("work")}));
    darkeye::graph_view::GraphViewWidget graph(manager);
    graph.resize(480, 400);
    auto *view = graph.view();
    view->setSimulationBackendMode(QStringLiteral("cpu"));
    graph.show();
    QVERIFY(QTest::qWaitForWindowExposed(&graph));
    QTRY_COMPARE(view->getNodeIds(), QStringList{QStringLiteral("w1")});
    QTest::qWait(50); // Drain the initial fit-to-content callback.
    view->setGraph(1, {}, {123.0f, 456.0f}, {QStringLiteral("w1")},
                   {QStringLiteral("Original")}, {7.0f});
    view->pauseSimulation();
    QTest::qWait(50); // setGraph also schedules its own initial camera fit.
    const QPointF position = view->getNodePosition(QStringLiteral("w1"));
    const float zoom = view->getZoom();
    const float panX = view->getPanX();
    const float panY = view->getPanY();
    QSignalSpy started(view, &ForceViewRhiWidget::simulationStarted);

    // Editing another page can emit several notifications while the graph is hidden.
    for (int cycle = 0; cycle < 2; ++cycle) {
        graph.hide();
        for (int edit = 0; edit < 2; ++edit) {
            QVERIFY(manager.upsertRuntimeNode(
                {QStringLiteral("w1"), QStringLiteral("Edited %1 %2").arg(cycle).arg(edit),
                 QStringLiteral("work")}));
        }
        graph.show();
        QVERIFY(QTest::qWaitForWindowExposed(&graph));
        QTest::qWait(50);
        QCOMPARE(started.count(), 0);
        QCOMPARE(view->getNodePosition(QStringLiteral("w1")), position);
        QCOMPARE(view->getZoom(), zoom);
        QCOMPARE(view->getPanX(), panX);
        QCOMPARE(view->getPanY(), panY);
    }

    // An explicit filter change still requires a full snapshot, even if a
    // manager notification arrives before the page is shown again.
    graph.hide();
    graph.showEmpty();
    QVERIFY(manager.upsertRuntimeNode(
        {QStringLiteral("w1"), QStringLiteral("Final"), QStringLiteral("work")}));
    graph.show();
    QVERIFY(QTest::qWaitForWindowExposed(&graph));
    QTRY_VERIFY(view->getNodeIds().isEmpty());
}

void GraphPageRenderingTest::changingNodeColorsPreservesLayout()
{
    darkeye::graph::GraphManager manager({}, {});
    QVERIFY(manager.upsertRuntimeNode(
        {QStringLiteral("a1"), QStringLiteral("Actress"), QStringLiteral("actress")}));
    darkeye::graph_view::GraphViewWidget graph(manager);
    graph.resize(480, 400);
    graph.setEgoGraph(QStringLiteral("a1"), 1, true);
    auto *view = graph.view();
    view->setSimulationBackendMode(QStringLiteral("cpu"));
    view->setGraph(1, {}, {123.0f, 456.0f}, {QStringLiteral("a1")},
                   {QStringLiteral("Actress")}, {7.0f});
    view->pauseSimulation();
    const QPointF position = view->getNodePosition(QStringLiteral("a1"));
    const float zoom = view->getZoom();
    const float panX = view->getPanX();
    const float panY = view->getPanY();

    for (const auto &group : {"actress", "work", "center", "default"}) {
        graph.setNodeColor(QLatin1String(group), QColor(Qt::green));
        QCOMPARE(view->getNodePosition(QStringLiteral("a1")), position);
        QCOMPARE(view->getZoom(), zoom);
        QCOMPARE(view->getPanX(), panX);
        QCOMPARE(view->getPanY(), panY);
        QCOMPARE(view->getNodeIds(), QStringList{QStringLiteral("a1")});
    }
}

void GraphPageRenderingTest::coveredRendererSubmitsFrame()
{
    QWidget window;
    window.resize(900, 600);
    auto *prewarm = new QWidget(&window);
    prewarm->setGeometry(0, 0, 240, 200);
    auto *layout = new QVBoxLayout(prewarm);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *view = new ForceViewRhiWidget(prewarm);
    layout->addWidget(view);
    view->setGraph(1, {}, {0.0f, 0.0f}, {QStringLiteral("prewarm")},
                   {QStringLiteral("prewarm")}, {4.0f}, {QColor(QStringLiteral("#808080"))});
    auto *cover = new QLabel(QStringLiteral("占位页"), &window);
    cover->setGeometry(window.rect());
    cover->setAutoFillBackground(true);
    prewarm->lower();
    QSignalSpy frameSpy(view, &ForceViewRhiWidget::firstFrameSubmitted);
    window.show();
    for (int frame = 0; frame < 10; ++frame) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    QVERIFY(!frameSpy.isEmpty());
    view->pauseSimulation();
}

void GraphPageRenderingTest::rendersWhenRendererExistsBeforeWindowShow()
{
    const QString publicConnectionName = QStringLiteral("graph_page_public");
    const QString privateConnectionName = QStringLiteral("graph_page_private");
    {
        QSqlDatabase publicDatabase =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), publicConnectionName);
        publicDatabase.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(publicDatabase.open());
        QSqlQuery query(publicDatabase);
        const QStringList statements = {
            QStringLiteral("CREATE TABLE actress (actress_id INTEGER PRIMARY KEY)"),
            QStringLiteral("CREATE TABLE actress_name (actress_id INTEGER, cn TEXT, jp TEXT)"),
            QStringLiteral("CREATE TABLE work (work_id INTEGER PRIMARY KEY, serial_number TEXT, "
                           "notes TEXT, series_id INTEGER, is_deleted INTEGER)"),
            QStringLiteral("CREATE TABLE work_actress_relation (work_id INTEGER, actress_id INTEGER)"),
            QStringLiteral("INSERT INTO actress VALUES (1)"),
            QStringLiteral("INSERT INTO actress_name VALUES (1, '演员一', '')"),
            QStringLiteral("INSERT INTO work VALUES (10, 'PAGE-001', '', 1, 0)"),
            QStringLiteral("INSERT INTO work_actress_relation VALUES (10, 1)"),
        };
        for (const QString &statement : statements) {
            QVERIFY2(query.exec(statement), qPrintable(query.lastError().text()));
        }

        QSqlDatabase privateDatabase =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), privateConnectionName);
        privateDatabase.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(privateDatabase.open());
        QSqlQuery privateQuery(privateDatabase);
        QVERIFY(privateQuery.exec(QStringLiteral("CREATE TABLE favorite_work (work_id INTEGER)")));

        auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
        QVERIFY(application != nullptr);
        darkeye::ThemeService themeService(*application);
        QVERIFY(themeService.setTheme(darkeye::ThemeId::Light));
        darkeye::graph::GraphManager manager(publicDatabase, privateDatabase);

        QMainWindow window;
        auto *stack = new QStackedWidget(&window);
        stack->addWidget(new QLabel(QStringLiteral("占位页"), stack));
        auto *page = new darkeye::ForceDirectPage(themeService, manager, stack);
        stack->addWidget(page);
        stack->setCurrentIndex(0);
        auto *view = page->findChild<ForceViewRhiWidget *>();
        QVERIFY(view != nullptr);
        QVERIFY(page->findChild<darkeye::ForceViewSettingsPanel *>());
        QVERIFY(!view->findChild<QWidget *>(QStringLiteral("forcePerformanceOverlay")));
        QVERIFY(!view->findChild<QMenu *>(QStringLiteral("forceViewContextMenu")));
        const QStringList testControls = {
            QStringLiteral("测试"), QStringLiteral("适配视图"), QStringLiteral("重启"),
            QStringLiteral("暂停"), QStringLiteral("继续"), QStringLiteral("加点"),
            QStringLiteral("编辑点"), QStringLiteral("减点"), QStringLiteral("加边"),
            QStringLiteral("减边"), QStringLiteral("2000 点图")};
        for (const auto *button : page->findChildren<QAbstractButton *>())
            QVERIFY2(!testControls.contains(button->text()), qPrintable(button->text()));
        auto *loadingOverlay = page->findChild<QLabel *>(QStringLiteral("graphLoadingOverlay"));
        QVERIFY(loadingOverlay != nullptr);
        QSignalSpy paintSpy(view, &ForceViewRhiWidget::paintTimeUpdated);
        window.setCentralWidget(stack);
        window.resize(900, 600);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!loadingOverlay->isVisible());

        // Match MainWindow: the page data is initialized on first navigation,
        // while the QRhi renderer itself existed before the window was shown.
        page->initialize();
        stack->setCurrentWidget(page);
        QVERIFY(loadingOverlay->isVisible());
        QCOMPARE(loadingOverlay->geometry(), page->rect());
        // A QSQLITE :memory: database is local to one connection, whereas
        // GraphManager's scheduled loader uses a worker connection.  Complete
        // this fixture's data initialization on its owning connection instead
        // of expecting the worker to see a different empty database.
        QString initializeError;
        QVERIFY2(manager.initialize(&initializeError), qPrintable(initializeError));
        QTRY_COMPARE_WITH_TIMEOUT(view->getNodeIds().size(), 2, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!paintSpy.isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!loadingOverlay->isVisible(), 5000);

        auto *graphWidget = page->findChild<darkeye::graph_view::GraphViewWidget *>();
        QVERIFY(graphWidget != nullptr);
        QTRY_COMPARE(graphWidget->geometry(), page->contentsRect());

        // Statistics remain available on the real application page even though
        // simulation and graph-editing test buttons belong to the demo.
        QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(20, 20),
                                  view->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(view, &context);
        auto *menu = view->findChild<QMenu *>(QStringLiteral("forceViewContextMenu"));
        auto *action = view->findChild<QAction *>(QStringLiteral("togglePerformanceOverlayAction"));
        QVERIFY(menu && menu->isVisible());
        QVERIFY(action);
        action->trigger();
        menu->hide();
        QVERIFY(view->isPerformanceOverlayVisible());
        action->trigger();
        QVERIFY(!view->isPerformanceOverlayVisible());
    }
    QSqlDatabase::removeDatabase(publicConnectionName);
    QSqlDatabase::removeDatabase(privateConnectionName);
}

void GraphPageRenderingTest::preparesLatestEgoGraphWhileHidden()
{
    darkeye::graph::GraphManager manager({}, {});
    darkeye::graph_view::GraphViewWidget graph(manager);
    graph.resize(480, 400);
    graph.setEgoGraph(QStringLiteral("w1"), 3, true);
    QVERIFY(graph.isHidden());
    QVERIFY(graph.view()->getNodeIds().isEmpty());

    // Data can arrive after preparation starts, before any QRhi surface exists.
    QVERIFY(manager.upsertRuntimeNode({QStringLiteral("w1"), QStringLiteral("First"), QStringLiteral("work")}));
    QCOMPARE(graph.view()->getNodeIds(), QStringList{QStringLiteral("w1")});
    QVERIFY(manager.upsertRuntimeNode({QStringLiteral("w2"), QStringLiteral("Second"), QStringLiteral("work")}));
    graph.setEgoGraph(QStringLiteral("w2"), 3, true);
    QCOMPARE(graph.view()->getNodeIds(), QStringList{QStringLiteral("w2")});
    QVERIFY(graph.isHidden());
    graph.show();
    QVERIFY(QTest::qWaitForWindowExposed(&graph));
    QTRY_VERIFY(graph.view()->getZoom() > 0.01f);
    QCOMPARE(graph.view()->getNodeIds(), QStringList{QStringLiteral("w2")});
}

void GraphPageRenderingTest::loadsFirstGraphSnapshotAfterAnInitiallyEmptyView()
{
    darkeye::graph::GraphManager manager({}, {});
    QMainWindow window;
    auto *graphWidget = new darkeye::graph_view::GraphViewWidget(manager, &window);
    window.setCentralWidget(graphWidget);
    window.resize(900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Let the first-show callback load the empty store.  This reproduces the
    // production race where the page becomes visible before its asynchronous
    // GraphManager database load finishes.
    QTest::qWait(50);
    QVERIFY(graphWidget->view()->getNodeIds().isEmpty());

    QVERIFY(manager.upsertRuntimeNode(
        {QStringLiteral("a1"), QStringLiteral("演员一"), QStringLiteral("actress")}));

    // A complete first snapshot is required here: the QRhi widget has no
    // PhysicsState after setGraph(0, ...), so an add-node diff alone cannot
    // create the initial graph.
    QTRY_COMPARE_WITH_TIMEOUT(graphWidget->view()->getNodeIds().size(), 1, 2000);
}

QTEST_MAIN(GraphPageRenderingTest)
#include "GraphPageRenderingTest.moc"
