#include "ui/layouts/myads/PaneWidget.h"
#include "ui/layouts/myads/ThemeAdapter.h"
#include "ui/layouts/myads/WorkspaceWidget.h"

#include <QAbstractAnimation>
#include <QFile>
#include <QCloseEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QSplitter>
#include <QStyle>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>
#include <stdexcept>

using namespace darkeye::myads;

class CloseAwarePage final : public QWidget
{
public:
    bool allowClose = false;
    int requests = 0;
    std::function<void()> onRequest;

protected:
    void closeEvent(QCloseEvent *event) override
    {
        ++requests;
        if (onRequest) onRequest();
        event->setAccepted(allowClose);
    }
};

class MyAdsTest final : public QObject
{
    Q_OBJECT
private slots:
    void splitAndSerialize();
    void splitRootAndNormalize();
    void rejectsInvalidTreesAndRatios();
    void savesAtomically();
    void paneTransfersCompleteContent();
    void tabsShareWidthBeforeScrolling();
    void appendedTabScrollsIntoView();
    void closeButtonsFollowSelectionAndHover();
    void tabClickSwitchesPageWhenReleaseLeavesTab();
    void closingTabsKeepsWidthsUntilPointerLeaves();
    void selectedTabLooksTheSameWhenPaneFocusChanges();
    void draggingTabFollowsPointer();
    void cancelledTabDragReturnsToOriginalPosition_data();
    void cancelledTabDragReturnsToOriginalPosition();
    void tabMenuClosesOnlyEligibleTabs();
    void tabMenuMovesLastTabToAnotherGroup();
    void pageCanVetoCloseButtonAndAllowRetry();
    void closedPagesAreDeletedAfterTheCurrentEvent();
    void paneUsesPageAndContentColors();
    void retainedPagesSurviveClosingTheirPane();
    void bulkCloseStopsOnVeto_data();
    void bulkCloseStopsOnVeto();
    void closeRequestRejectsReentryAndRevalidatesOwnership();
    void workspacePreservesContentWhileSplitting();
    void workspaceBatchesProgrammaticSplits();
    void nestedSplitterResizesBeforeRelease();
    void workspaceMovesTabsAcrossPanes();
    void dropsTabsAtInsertionLine_data();
    void dropsTabsAtInsertionLine();
    void workspaceSavesAndRestoresContent();
    void sidebarsToggleAndCollapseOnFocusLoss();
    void sidebarTabsSurviveLayoutRestore();
    void sidebarWidthsAreRestoredAsRatios();
    void sidebarSplitsVertically_data();
    void sidebarSplitsVertically();
    void unknownSchemaDoesNotDestroyWorkspace();
    void invalidLayoutDoesNotDestroyWorkspace();
    void factoryFailurePreservesWorkspace_data();
    void factoryFailurePreservesWorkspace();
    void restoredWorkspaceReusesPagesAndKeepsCallbacks();
    void hitTestingMatchesPythonZones();
    void previewGeometryMatchesPythonOverlay();
    void activePaneFollowsUserInteraction();
    void docksThroughGraphicsView_data();
    void docksThroughGraphicsView();
    void preservesChildDropsAndRejectsInvalidDockData();
};

void MyAdsTest::splitAndSerialize()
{
    LayoutTree tree;
    tree.addPaneToRoot(QStringLiteral("left"));
    tree.split(QStringLiteral("left"), Qt::Horizontal, false,
               QStringLiteral("right"), 30);
    QCOMPARE(tree.paneIds(), QList<QString>({QStringLiteral("left"), QStringLiteral("right")}));
    QCOMPARE(tree.root()->sizes, QList<int>({700, 300}));
    const QJsonObject json = tree.toJson();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("split"));
    QCOMPARE(json.value(QStringLiteral("children")).toArray().size(), 2);
    const LayoutTree restored = LayoutTree::fromJson(json);
    QCOMPARE(restored.paneIds(), tree.paneIds());
    QCOMPARE(restored.root()->sizes, tree.root()->sizes);
}

void MyAdsTest::splitRootAndNormalize()
{
    LayoutTree tree;
    tree.addPaneToRoot(QStringLiteral("center"));
    tree.split(QStringLiteral("center"), Qt::Vertical, false,
               QStringLiteral("bottom"), 25);
    tree.splitRoot(Qt::Horizontal, true, QStringLiteral("left"), 20);
    QCOMPARE(tree.paneIds(), QList<QString>({QStringLiteral("left"),
                                             QStringLiteral("center"),
                                             QStringLiteral("bottom")}));
    QCOMPARE(tree.root()->orientation, Qt::Horizontal);
    QCOMPARE(tree.root()->sizes, QList<int>({200, 800}));
    QVERIFY(tree.removePane(QStringLiteral("bottom")));
    QCOMPARE(tree.paneIds(), QList<QString>({QStringLiteral("left"),
                                             QStringLiteral("center")}));
}

void MyAdsTest::rejectsInvalidTreesAndRatios()
{
    LayoutTree tree;
    tree.addPaneToRoot(QStringLiteral("one"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
        tree.addPaneToRoot(QStringLiteral("one")));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
        tree.split(QStringLiteral("one"), Qt::Horizontal, false,
                   QStringLiteral("two"), 0));
    QJsonObject invalidPane{{QStringLiteral("type"), QStringLiteral("pane")},
                            {QStringLiteral("pane_id"), QString()}};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
        (void)LayoutTree::fromJson(invalidPane));
    QJsonObject invalidSplit{
        {QStringLiteral("type"), QStringLiteral("split")},
        {QStringLiteral("orientation"), QStringLiteral("horizontal")},
        {QStringLiteral("children"), QJsonArray{tree.toJson()}},
        {QStringLiteral("size_ratios"), QJsonArray{-1.0}}};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
        (void)LayoutTree::fromJson(invalidSplit));
}

void MyAdsTest::savesAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nested/workspace.json"));
    QJsonObject data{{QStringLiteral("schema_version"), LayoutTree::SchemaVersion}};
    QString error;
    QVERIFY2(saveLayoutAtomic(path, data, &error), qPrintable(error));
    QCOMPARE(loadLayoutFile(path, &error).value(QStringLiteral("schema_version")).toInt(), 1);
}

void MyAdsTest::paneTransfersCompleteContent()
{
    PaneWidget source(QStringLiteral("source"));
    PaneWidget destination(QStringLiteral("destination"));
    auto *label = new QLabel(QStringLiteral("payload"));
    QVERIFY(source.addContent(QStringLiteral("movie"), QStringLiteral("影片"),
                              label, QIcon(), false));
    QCOMPARE(source.currentContentId(), QStringLiteral("movie"));
    const PaneContent content = source.takeContent(QStringLiteral("movie"));
    QCOMPARE(content.widget, label);
    QCOMPARE(content.title, QStringLiteral("影片"));
    QVERIFY(!content.closeable);
    QVERIFY(destination.restoreContent(content));
    QCOMPARE(destination.contentWidget(QStringLiteral("movie")), label);
    QVERIFY(!destination.isContentCloseable(QStringLiteral("movie")));
}

void MyAdsTest::tabsShareWidthBeforeScrolling()
{
    PaneWidget pane(QStringLiteral("tabs"));
    QVERIFY(pane.addContent("one", "One", new QWidget));
    QVERIFY(pane.addContent("two", "A much longer tab title", new QWidget));
    QVERIFY(pane.addContent("three", "Pinned", new QWidget, {}, false));
    QVERIFY(pane.addContent("four", "Four", new QWidget));
    QVERIFY(pane.setCurrentContentId("one"));

    pane.resize(800, 200);
    pane.show();
    auto *bar = pane.findChild<QTabBar *>();
    QVERIFY(bar);

    const auto scrollButtons = [bar] {
        QList<QToolButton *> buttons;
        for (auto *button : bar->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (button->isVisible() && button->objectName() != QStringLiteral("MyAdsCloseButton"))
                buttons.append(button);
        }
        return buttons;
    };
    const auto checkWidths = [bar](int width) {
        for (int i = 0; i < bar->count(); ++i)
            QCOMPARE(bar->tabRect(i).width(), width);
    };

    checkWidths(168);
    QVERIFY(scrollButtons().isEmpty());

    pane.resize(500, 200);
    checkWidths(125);
    QVERIFY(scrollButtons().isEmpty());

    pane.resize(380, 200);
    checkWidths(120);
    QVERIFY(scrollButtons().isEmpty());
    const auto tabLeft = [&pane, bar](int index) {
        return bar->mapTo(&pane, bar->tabRect(index).topLeft()).x();
    };
    const auto scroll = [bar](const QPoint &pixels, const QPoint &angle) {
        const QPoint pos = bar->mapFromParent(QPoint(100, 16));
        QWheelEvent event(pos, bar->mapToGlobal(pos), pixels, angle, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(bar, &event);
    };
    const int beforeScroll = tabLeft(0);
    scroll({}, QPoint(0, -120));
    QVERIFY(tabLeft(0) < beforeScroll);
    QVERIFY(tabLeft(3) + bar->tabRect(3).width() <= pane.width());
    QCOMPARE(pane.currentContentId(), QStringLiteral("one"));
    const int scrolledLeft = tabLeft(0);
    scroll(QPoint(20, 0), {});
    QCOMPARE(tabLeft(0), scrolledLeft + 20);
    scroll({}, QPoint(0, 120));
    QCOMPARE(tabLeft(0), beforeScroll);
    scroll({}, QPoint(0, -120));
    QCOMPARE(pane.currentContentId(), QStringLiteral("one"));

    QVERIFY(pane.removeContent("four"));
    QVERIFY(scrollButtons().isEmpty());
    QTRY_COMPARE(tabLeft(0), beforeScroll);

    QVERIFY(pane.addContent("four", "Four", new QWidget));
    QTRY_VERIFY(tabLeft(3) + bar->tabRect(3).width() <= pane.width());

    pane.resize(800, 200);
    checkWidths(168);
}

void MyAdsTest::appendedTabScrollsIntoView()
{
    PaneWidget pane(QStringLiteral("tabs"));
    pane.resize(380, 200);
    pane.show();
    auto *bar = pane.findChild<QTabBar *>();
    QVERIFY(bar);

    for (int i = 0; i < 8; ++i)
        QVERIFY(pane.addContent(QString::number(i), QStringLiteral("Tab"), new QWidget));
    QCoreApplication::processEvents();
    QVERIFY(pane.setCurrentContentId(QStringLiteral("0")));

    QVERIFY(pane.addContent(QStringLiteral("new"), QStringLiteral("New tab"), new QWidget));
    QCOMPARE(pane.currentContentId(), QStringLiteral("new"));
    const auto currentTabIsVisible = [bar] {
        const QRect tab = bar->tabRect(bar->currentIndex());
        return bar->parentWidget()->rect().contains(
            QRect(bar->mapToParent(tab.topLeft()), tab.size()));
    };
    QTRY_VERIFY(currentTabIsVisible());
}

void MyAdsTest::closeButtonsFollowSelectionAndHover()
{
    PaneWidget pane("tabs");
    QVERIFY(pane.addContent("first", "First", new QWidget));
    QVERIFY(pane.addContent("second", "Second", new QWidget));
    QVERIFY(pane.addContent("pinned", "Pinned", new QWidget, {}, false));
    QVERIFY(pane.setCurrentContentId("first"));
    pane.resize(600, 200);
    pane.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pane));

    auto *bar = pane.findChild<QTabBar *>();
    QVERIFY(bar);
    auto *first = bar->tabButton(0, QTabBar::RightSide);
    auto *second = bar->tabButton(1, QTabBar::RightSide);
    auto *pinned = bar->tabButton(2, QTabBar::RightSide);
    QVERIFY(first && second && pinned);
    const auto moveOverBar = [&](const QPoint &point) {
        QTest::mouseMove(pane.windowHandle(), bar->mapTo(&pane, point));
    };

    moveOverBar(QPoint(10, bar->height() + 10));
    QVERIFY(first->isVisible());
    QVERIFY(second->isHidden());
    QVERIFY(pinned->isHidden());

    const QRect secondTab = bar->tabRect(1);
    const QRect secondButton = second->geometry();
    moveOverBar(secondTab.center());
    QTRY_VERIFY(second->isVisible());
    QCOMPARE(bar->tabRect(1), secondTab);
    QCOMPARE(second->geometry(), secondButton);
    QCOMPARE(pane.currentContentId(), QStringLiteral("first"));

    moveOverBar(secondButton.center());
    QVERIFY(second->isVisible());
    moveOverBar(bar->tabRect(2).center());
    QTRY_VERIFY(second->isHidden());
    QVERIFY(pinned->isHidden());

    QVERIFY(pane.setCurrentContentId("second"));
    QVERIFY(first->isHidden());
    QVERIFY(second->isVisible());

    moveOverBar(bar->tabRect(0).center());
    QTRY_VERIFY(first->isVisible());
    moveOverBar(QPoint(10, bar->height() + 10));
    QTRY_VERIFY(first->isHidden());
    QVERIFY(second->isVisible());

    moveOverBar(bar->tabRect(0).center());
    QTRY_VERIFY(first->isVisible());
    QTest::mouseClick(first, Qt::LeftButton);
    QCOMPARE(pane.contentIds(), QStringList({"second", "pinned"}));
    QCOMPARE(pane.currentContentId(), QStringLiteral("second"));
}

void MyAdsTest::tabClickSwitchesPageWhenReleaseLeavesTab()
{
    WorkspaceWidget workspace;
    auto *pane = workspace.rootPane();
    auto *first = new QLabel(QStringLiteral("First page"));
    QVERIFY(pane->addContent("first", "First tab", first));
    QVERIFY(pane->addContent("second", "Second tab", new QLabel));
    workspace.resize(600, 300);
    workspace.show();
    QVERIFY(QTest::qWaitForWindowExposed(&workspace));
    auto *bar = pane->findChild<QTabBar *>();
    QVERIFY(bar);
    const QRect tab = bar->tabRect(0);
    const QPoint press(tab.center().x(), tab.bottom());
    QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, press);
    QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, press + QPoint(0, 1));
    QCOMPARE(pane->currentContentId(), QStringLiteral("first"));
    QVERIFY(first->isVisible());
}

void MyAdsTest::closingTabsKeepsWidthsUntilPointerLeaves()
{
    PaneWidget pane(QStringLiteral("pane"));
    for (const QString &id : {QStringLiteral("first"), QStringLiteral("second"),
                              QStringLiteral("third")}) {
        QVERIFY(pane.addContent(id, id, new QLabel));
    }
    pane.resize(450, 250);
    pane.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pane));

    auto *bar = pane.findChild<QTabBar *>();
    QVERIFY(bar);
    const int secondWidth = bar->tabRect(1).width();
    const int thirdWidth = bar->tabRect(2).width();
    const auto moveOverBar = [&](const QPoint &point) {
        QTest::mouseMove(pane.windowHandle(), bar->mapTo(&pane, point));
    };

    for (int remaining : {2, 1}) {
        auto *button = bar->tabButton(0, QTabBar::RightSide);
        moveOverBar(bar->tabRect(0).center());
        QTRY_VERIFY(button->isVisible());
        moveOverBar(button->geometry().center());
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(bar->count(), remaining);
        QCoreApplication::processEvents();
        QCOMPARE(bar->tabRect(remaining - 1).width(), thirdWidth);
        if (remaining == 2) QCOMPARE(bar->tabRect(0).width(), secondWidth);
    }

    moveOverBar(QPoint(bar->width() - 1, bar->height() / 2));
    QCOMPARE(bar->tabRect(0).width(), thirdWidth);
    moveOverBar(QPoint(10, bar->height() + 10));
    QTRY_VERIFY(bar->tabRect(0).width() > thirdWidth);
}

void MyAdsTest::selectedTabLooksTheSameWhenPaneFocusChanges()
{
    WorkspaceWidget workspace;
    auto *left = workspace.rootPane();
    QVERIFY(left->addContent("left", "Left tab", new QLabel));
    auto *right = workspace.split(left, Placement::Right);
    QVERIFY(right);
    QVERIFY(right->addContent("right", "Right tab", new QLabel));
    workspace.resize(600, 300);
    workspace.show();
    workspace.activateWindow();
    QTRY_VERIFY(workspace.isActiveWindow());

    auto *leftBar = left->findChild<QTabBar *>();
    auto *rightBar = right->findChild<QTabBar *>();
    QVERIFY(leftBar);
    QVERIFY(rightBar);

    right->focusTabBar();
    QTRY_VERIFY(rightBar->hasFocus());
    const QImage unfocused = leftBar->grab().toImage();
    left->focusTabBar();
    QTRY_VERIFY(leftBar->hasFocus());
    QCOMPARE(leftBar->grab().toImage(), unfocused);
}

void MyAdsTest::draggingTabFollowsPointer()
{
    DockTheme theme;
    theme.tabFontSize = 19;
    theme.tabSelected = QColor(QStringLiteral("#86b7e0"));
    WorkspaceWidget workspace(nullptr, theme);
    auto *pane = workspace.rootPane();
    QVERIFY(pane->addContent("left", "Left tab", new QLabel));
    QVERIFY(pane->addContent("middle", "Middle tab", new QLabel));
    QVERIFY(pane->addContent("right", "Right tab", new QLabel));
    workspace.resize(600, 300);
    workspace.show();
    QVERIFY(QTest::qWaitForWindowExposed(&workspace));
    auto *bar = pane->findChild<QTabBar *>();
    QVERIFY(bar);

    bar->setCurrentIndex(1);
    const QRect middle = bar->tabRect(1);
    const QPoint press = middle.center();
    QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, press);
    auto release = qScopeGuard([bar, press] {
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, press);
    });
    const QImage resting = bar->grab().toImage();
    const QPoint trailingBackground(bar->width() - 2, bar->height() / 2);
    QVERIFY(trailingBackground.x() > bar->tabRect(2).right());
    QCOMPARE(resting.pixelColor(trailingBackground * resting.devicePixelRatio()),
             theme.tabBarBackground);
    const auto checkStripEdges = [&](const QImage &image) {
        const QRect left = bar->tabRect(0);
        const QRect right = bar->tabRect(2);
        for (const int x : {left.left() + 3, right.right() - 3}) {
            for (const int y : {left.top(), left.center().y()})
                QCOMPARE(image.pixelColor(QPoint(x, y) * image.devicePixelRatio()),
                         theme.tabBarBackground);
            QCOMPARE(image.pixelColor(QPoint(x, left.bottom()) * image.devicePixelRatio()),
                     theme.border);
        }
    };
    checkStripEdges(resting);
    const qreal restingScale = resting.devicePixelRatio();
    QCOMPARE(resting.pixelColor(QPoint(middle.center().x(), middle.top()) * restingScale),
             theme.border);
    QCOMPARE(resting.pixelColor(QPoint(middle.center().x(), middle.bottom()) * restingScale),
             theme.tabSelected);
    const auto move = [bar](const QPoint &position) {
        QMouseEvent event(QEvent::MouseMove, position, bar->mapToGlobal(position),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bar, &event);
    };
    for (int dx : {-24, 24}) {
        move(press + QPoint(dx, 0));
        QCOMPARE(pane->contentIds(), QStringList({"left", "middle", "right"}));
        const QPixmap dragged = bar->grab();
        checkStripEdges(dragged.toImage());
        const QPoint edge(dx < 0 ? middle.right() - 14 : middle.left() + 14, middle.top() + 6);
        const qreal scale = dragged.devicePixelRatio();
        QCOMPARE(dragged.toImage().pixelColor((edge + QPoint(dx, 0)) * scale), theme.tabSelected);
        QCOMPARE(dragged.toImage().pixelColor(edge * scale), theme.tabBarBackground);
    }
    const int boundary = (middle.center().x() + bar->tabRect(2).center().x()) / 2;
    const QPoint beforeBoundary(boundary - 1, press.y());
    const QPoint afterBoundary(boundary + 1, press.y());
    move(beforeBoundary);
    QCOMPARE(pane->contentIds(), QStringList({"left", "middle", "right"}));
    move(afterBoundary);
    QCOMPARE(pane->contentIds(), QStringList({"left", "right", "middle"}));
    move(afterBoundary);
    QCOMPARE(pane->contentIds(), QStringList({"left", "right", "middle"}));
    move(beforeBoundary);
    QCOMPARE(pane->contentIds(), QStringList({"left", "middle", "right"}));

    const QPoint pastLastTab(bar->tabRect(2).right() + 20, press.y());
    QVERIFY(bar->rect().contains(pastLastTab));
    move(pastLastTab);
    QCOMPARE(pane->contentIds(), QStringList({"left", "right", "middle"}));
    QCOMPARE(pane->currentContentId(), QStringLiteral("middle"));
    auto *animation = bar->findChild<QWidget *>(QStringLiteral("MyAdsTabReorderAnimation"));
    QVERIFY(animation && animation->isVisible());
    QVERIFY(animation->testAttribute(Qt::WA_TransparentForMouseEvents));
    const QImage transitionStart = bar->grab().toImage();
    const QRect title = bar->tabRect(0).adjusted(8, 4, -34, -4);
    const qreal scale = resting.devicePixelRatio();
    const QRect titlePixels(title.topLeft() * scale, title.size() * scale);
    const auto inkBounds = [&titlePixels](const QImage &image) {
        QRect bounds;
        for (int y = titlePixels.top(); y <= titlePixels.bottom(); ++y) {
            for (int x = titlePixels.left(); x <= titlePixels.right(); ++x) {
                if (qGray(image.pixel(x, y)) < 128)
                    bounds |= QRect(x, y, 1, 1);
            }
        }
        return bounds;
    };
    QVERIFY(inkBounds(resting).isValid());
    QCOMPARE(inkBounds(transitionStart).size(), inkBounds(resting).size());
    const QPoint drop = bar->tabRect(2).center() + QPoint(20, 0);
    move(drop);
    QVERIFY(bar->grab().toImage() != transitionStart);
    QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, drop);
    release.dismiss();
    QVERIFY(!animation->isVisible());
    QCOMPARE(pane->contentIds(), QStringList({"left", "right", "middle"}));
}

void MyAdsTest::cancelledTabDragReturnsToOriginalPosition_data()
{
    QTest::addColumn<bool>("reorderFirst");
    QTest::newRow("offset-only") << false;
    QTest::newRow("reordered-before-leaving") << true;
}

void MyAdsTest::cancelledTabDragReturnsToOriginalPosition()
{
    if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
        QSKIP("Run by darkeye_myads_drag_tests with the offscreen drag backend.");
    QFETCH(bool, reorderFirst);
    PaneWidget pane(QStringLiteral("source"));
    auto *first = new QLabel(QStringLiteral("first page"));
    QVERIFY(pane.addContent("first", "First tab", first));
    QVERIFY(pane.addContent("second", "Second tab", new QLabel));
    QVERIFY(pane.addContent("third", "Third tab", new QLabel));
    QVERIFY(pane.setCurrentContentId("first"));
    pane.resize(600, 300);
    pane.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pane));
    auto *bar = pane.findChild<QTabBar *>();
    QVERIFY(bar);
    const QStringList originalOrder = pane.contentIds();
    QWidget *closeButton = bar->tabButton(0, QTabBar::RightSide);
    QVERIFY(closeButton);
    const QRect originalButtonGeometry = closeButton->geometry();
    const QRect originalTab = bar->tabRect(0);
    const QPoint press = originalTab.center();
    const int overlap = bar->style()->pixelMetric(QStyle::PM_TabBarTabOverlap, nullptr, bar);
    QVERIFY(overlap >= 7);
    const auto pixelAt = [](const QPixmap &pixmap, QPoint point) {
        return pixmap.toImage().pixelColor(point * pixmap.devicePixelRatio());
    };
    // Sample the lower bevel past the nominal tab rectangle.
    const QPoint foot(originalTab.right() + 3, originalTab.bottom());
    const QPixmap restingTabBar = bar->grab();
    const QColor footColor = pixelAt(restingTabBar, foot);
    const QPoint closeCenter = originalButtonGeometry.center();
    const QColor closeColor = pixelAt(restingTabBar, closeCenter);
    QVERIFY(closeColor != footColor);
    QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, press);
    const auto releaseOnExit = qScopeGuard([bar] {
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    });
    const auto move = [bar](const QPoint &position) {
        QMouseEvent event(QEvent::MouseMove, position, bar->mapToGlobal(position),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bar, &event);
    };
    move(reorderFirst ? bar->tabRect(2).center() : press + QPoint(20, 0));
    if (reorderFirst) QVERIFY(pane.contentIds() != originalOrder);
    if (!reorderFirst) QCOMPARE(closeButton->geometry(), originalButtonGeometry);
    auto *animation = bar->findChild<QWidget *>(QStringLiteral("MyAdsTabReorderAnimation"));
    QVERIFY(animation && animation->isVisible());

    // Some offscreen drag backends return immediately. Sample the preview
    // when it appears and advance its animation without relying on a drag loop.
    class PreviewCapture final : public QObject
    {
    public:
        QPointer<QWidget> preview;
        QPixmap start;
        QPixmap middle;
        QPixmap end;

        bool eventFilter(QObject *object, QEvent *event) override
        {
            if (event->type() != QEvent::Show ||
                object->objectName() != QStringLiteral("MyAdsDetachedTabPreview")) return false;
            preview = qobject_cast<QWidget *>(object);
            auto *morph = object->findChild<QAbstractAnimation *>();
            if (!preview || !morph) return false;
            start = preview->grab();
            morph->setCurrentTime(morph->duration() / 4);
            middle = preview->grab();
            morph->setCurrentTime(morph->duration());
            end = preview->grab();
            morph->setCurrentTime(0);
            return false;
        }
    } capture;
    qApp->installEventFilter(&capture);
    QTimer cancelTimer;
    cancelTimer.setSingleShot(true);
    connect(&cancelTimer, &QTimer::timeout, &pane, [] { QDrag::cancel(); });
    cancelTimer.start(10);
    QSignalSpy finished(&pane, &PaneWidget::dragFinished);
    move(QPoint(press.x(), bar->height() + 40));
    cancelTimer.stop();
    qApp->removeEventFilter(&capture);
    const auto &dockPixmap = capture.end;
    QVERIFY(!capture.start.isNull() && !capture.middle.isNull() && !dockPixmap.isNull());
    const QPoint pixmapOffset = QPoint(overlap, 0) - originalTab.topLeft();
    QCOMPARE(pixelAt(capture.start, foot + pixmapOffset), footColor);
    QVERIFY(capture.middle.toImage() != capture.start.toImage());
    QVERIFY(capture.middle.toImage() != dockPixmap.toImage());
    QCOMPARE(pixelAt(dockPixmap, foot + pixmapOffset).alpha(), 0);
    QCOMPARE(pixelAt(dockPixmap, originalTab.topLeft() + pixmapOffset + QPoint(1, 1)).alpha(), 0);
    QCOMPARE(pixelAt(dockPixmap, originalTab.topRight() + pixmapOffset + QPoint(-1, 1)).alpha(), 0);
    QCOMPARE(pixelAt(dockPixmap, originalTab.bottomLeft() + pixmapOffset + QPoint(1, -1)).alpha(), 0);
    QCOMPARE(pixelAt(dockPixmap, originalTab.bottomRight() + pixmapOffset + QPoint(-1, -1)).alpha(), 0);
    QCOMPARE(pixelAt(dockPixmap, QPoint(press.x(), originalTab.bottom()) + pixmapOffset), footColor);
    QCOMPARE(pixelAt(dockPixmap, closeCenter + pixmapOffset), closeColor);
    QVERIFY(capture.preview.isNull());
    QCOMPARE(pane.contentIds(), originalOrder);
    QCOMPARE(pane.currentContentId(), QStringLiteral("first"));
    QCOMPARE(pane.contentWidget("first"), first);
    QCOMPARE(finished.count(), 1);
    QVERIFY(!animation->isVisible());
    QTRY_COMPARE(closeButton->geometry(), originalButtonGeometry);
    // A later ordinary click must still work after cancellation.
    QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(1).center());
    QCOMPARE(pane.currentContentId(), QStringLiteral("second"));
}

void MyAdsTest::tabMenuClosesOnlyEligibleTabs()
{
    PaneWidget pane(QStringLiteral("test"));
    QVERIFY(!pane.createTabContextMenu(QStringLiteral("missing")));
    QVERIFY(pane.addContent(QStringLiteral("left"), QStringLiteral("Left"), new QLabel));
    QVERIFY(pane.addContent(QStringLiteral("anchor"), QStringLiteral("Anchor"), new QLabel));
    QVERIFY(pane.addContent(QStringLiteral("protected"), QStringLiteral("Protected"),
                            new QLabel, {}, false));
    QVERIFY(pane.addContent(QStringLiteral("right"), QStringLiteral("Right"), new QLabel));
    QVERIFY(pane.setCurrentContentId(QStringLiteral("left")));
    QSignalSpy emptied(&pane, &PaneWidget::paneEmpty);
    std::unique_ptr<QMenu> menu(pane.createTabContextMenu(QStringLiteral("anchor")));
    auto *closeRight = menu->findChild<QAction *>(QStringLiteral("closeTabsToRight"));
    QVERIFY(closeRight && closeRight->isEnabled());
    closeRight->trigger();
    QCOMPARE(pane.contentIds(), QStringList({QStringLiteral("left"), QStringLiteral("anchor"),
                                           QStringLiteral("protected")}));
    QCOMPARE(pane.currentContentId(), QStringLiteral("left"));
    menu.reset(pane.createTabContextMenu(QStringLiteral("anchor")));
    QVERIFY(!menu->findChild<QAction *>(QStringLiteral("closeTabsToRight"))->isEnabled());
    menu->findChild<QAction *>(QStringLiteral("closeOtherTabs"))->trigger();
    QCOMPARE(pane.contentIds(), QStringList({QStringLiteral("anchor"), QStringLiteral("protected")}));
    QCOMPARE(emptied.count(), 0);
    menu.reset(pane.createTabContextMenu(QStringLiteral("anchor")));
    QVERIFY(!menu->findChild<QAction *>(QStringLiteral("closeOtherTabs"))->isEnabled());
}

void MyAdsTest::tabMenuMovesLastTabToAnotherGroup()
{
    WorkspaceWidget workspace;
    PaneWidget *source = workspace.rootPane();
    auto *payload = new QLabel(QStringLiteral("kept"));
    QVERIFY(workspace.fillPane(source, ContentConfig(QStringLiteral("payload"))
        .setWidget(payload).setWindowTitle(QStringLiteral("Payload")).setCloseable(false)));
    std::unique_ptr<QMenu> menu(source->createTabContextMenu(QStringLiteral("payload")));
    auto *moveMenu = menu->findChild<QMenu *>(QStringLiteral("moveTabToGroup"));
    QVERIFY(moveMenu && !moveMenu->isEnabled());
    PaneWidget *target = workspace.split(source, Placement::Right);
    QVERIFY(workspace.fillPane(target, ContentConfig(QStringLiteral("other"))
        .setWidget(new QLabel).setWindowTitle(QStringLiteral("Other"))));
    const QString targetId = target->paneId();
    QSignalSpy moved(&workspace, &WorkspaceWidget::contentMoved);
    menu.reset(source->createTabContextMenu(QStringLiteral("payload")));
    moveMenu = menu->findChild<QMenu *>(QStringLiteral("moveTabToGroup"));
    QVERIFY(moveMenu && moveMenu->isEnabled());
    QCOMPARE(moveMenu->actions().size(), 1);
    QCOMPARE(moveMenu->actions().first()->data().toString(), targetId);
    moveMenu->actions().first()->trigger();
    QCOMPARE(workspace.panes().size(), 1);
    QCOMPARE(workspace.activePane(), target);
    QCOMPARE(target->currentContentId(), QStringLiteral("payload"));
    QCOMPARE(target->contentWidget(QStringLiteral("payload")), payload);
    QVERIFY(!target->isContentCloseable(QStringLiteral("payload")));
    QCOMPARE(moved.count(), 1);
}

void MyAdsTest::pageCanVetoCloseButtonAndAllowRetry()
{
    WorkspaceWidget workspace;
    PaneWidget *pane = workspace.rootPane();
    auto *page = new CloseAwarePage;
    QVERIFY(pane->addContent(QStringLiteral("edit"), QStringLiteral("Edit"), page));
    QSignalSpy emptied(pane, &PaneWidget::paneEmpty);
    auto *tabs = pane->findChild<QTabBar *>();
    auto *button = qobject_cast<QToolButton *>(tabs->tabButton(0, QTabBar::RightSide));
    QVERIFY(button);
    button->click();
    QCOMPARE(page->requests, 1);
    QCOMPARE(pane->contentWidget(QStringLiteral("edit")), page);
    QCOMPARE(pane->currentContentId(), QStringLiteral("edit"));
    QCOMPARE(emptied.count(), 0);
    page->allowClose = true;
    button->click();
    QCOMPARE(page->requests, 2);
    QCOMPARE(pane->contentCount(), 0);
    QCOMPARE(emptied.count(), 1);

    auto *protectedPage = new CloseAwarePage;
    protectedPage->allowClose = true;
    QVERIFY(pane->addContent(QStringLiteral("protected"), QStringLiteral("Protected"),
                             protectedPage, {}, false));
    QVERIFY(!pane->removeContent(QStringLiteral("protected")));
    QCOMPARE(protectedPage->requests, 0);
    QVERIFY(!pane->removeContent(QStringLiteral("missing")));
}

void MyAdsTest::closedPagesAreDeletedAfterTheCurrentEvent()
{
    PaneWidget pane("test");
    QPointer<CloseAwarePage> page = new CloseAwarePage;
    page->setAttribute(Qt::WA_DeleteOnClose);
    QVERIFY(pane.addContent("edit", "Edit", page));
    QVERIFY(!pane.removeContent("edit"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(page);
    page->allowClose = true;
    QVERIFY(pane.removeContent("edit"));
    QVERIFY(page); // No synchronous destruction in the button's event handler.
    QCOMPARE(pane.contentCount(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(page.isNull());
}

void MyAdsTest::paneUsesPageAndContentColors()
{
    WorkspaceWidget workspace;
    workspace.resize(400, 300);
    auto *pane = workspace.rootPane();
    workspace.show();
    QCoreApplication::processEvents();

    const QPoint top(pane->width() - 8, 8);
    const QPoint middle = pane->contentRect().center();
    auto *bar = pane->findChild<QTabBar *>();
    QVERIFY(bar);
    const auto colorAt = [](const QImage &image, const QPoint &point) {
        return image.pixelColor(point * image.devicePixelRatio());
    };

    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark}) {
        const auto tokens = darkeye::ThemeService::tokens(theme);
        workspace.applyTheme(dockThemeFromTokens(tokens));
        const QColor pageColor(tokens.pageBackground);
        const QColor contentColor(tokens.background);
        QVERIFY(pageColor != contentColor);

        QVERIFY(workspace.fillPane(pane, ContentConfig("page").setWidget(new QWidget)));
        const QRect tab = bar->tabRect(0);
        const QPoint tabFill = bar->mapTo(pane, QPoint(tab.center().x(), tab.bottom() - 2));
        const QImage occupied = pane->grab().toImage();
        QCOMPARE(colorAt(occupied, top), pageColor);
        QCOMPARE(colorAt(occupied, tabFill), pageColor);
        QCOMPARE(colorAt(occupied, middle), contentColor);

        QVERIFY(pane->removeContent("page"));
        QCOMPARE(workspace.panes(), QList<PaneWidget *>{pane});
        QCOMPARE(pane->contentCount(), 0);
        const QImage empty = pane->grab().toImage();
        QCOMPARE(colorAt(empty, top), pageColor);
        QCOMPARE(colorAt(empty, tabFill), pageColor);
        QCOMPARE(colorAt(empty, middle), pageColor);
    }
}

void MyAdsTest::retainedPagesSurviveClosingTheirPane()
{
    WorkspaceWidget workspace;
    PaneWidget *left = workspace.rootPane();
    PaneWidget *right = workspace.split(left, Placement::Right);
    QVERIFY(left->addContent("anchor", "Anchor", new QLabel));
    QPointer<PaneWidget> oldRight(right);
    auto retained = std::make_unique<QLabel>(QStringLiteral("reusable"));
    ContentConfig config("retained");
    config.setWidget(retained.get()).setClosePolicy(ClosePolicy::KeepAlive);
    QVERIFY(workspace.fillPane(left, config));
    QVERIFY(workspace.moveContent(left->paneId(), "retained", right->paneId(), DropZone::Center));
    QCOMPARE(right->contentClosePolicy("retained"), ClosePolicy::KeepAlive);
    QVERIFY(right->removeContent("retained"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(oldRight.isNull());
    QVERIFY(!retained->parentWidget());
    QVERIFY(retained->isHidden());
    QVERIFY(workspace.fillPane(left, config));
    QCOMPARE(left->contentWidget("retained"), retained.get());
    QVERIFY(left->removeContent("retained"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!retained->parentWidget());
}

void MyAdsTest::bulkCloseStopsOnVeto_data()
{
    QTest::addColumn<QString>("actionName");
    QTest::newRow("other") << QStringLiteral("closeOtherTabs");
    QTest::newRow("right") << QStringLiteral("closeTabsToRight");
}

void MyAdsTest::bulkCloseStopsOnVeto()
{
    QFETCH(QString, actionName);
    PaneWidget pane(QStringLiteral("test"));
    QVERIFY(pane.addContent(QStringLiteral("anchor"), QStringLiteral("Anchor"), new QLabel));
    auto *saved = new CloseAwarePage;
    saved->allowClose = true;
    auto *cancelled = new CloseAwarePage;
    auto *later = new CloseAwarePage;
    later->allowClose = true;
    QVERIFY(pane.addContent(QStringLiteral("saved"), QStringLiteral("Saved"), saved));
    QVERIFY(pane.addContent(QStringLiteral("cancelled"), QStringLiteral("Cancelled"), cancelled));
    QVERIFY(pane.addContent(QStringLiteral("later"), QStringLiteral("Later"), later));
    QVERIFY(pane.setCurrentContentId(QStringLiteral("cancelled")));
    std::unique_ptr<QMenu> menu(pane.createTabContextMenu(QStringLiteral("anchor")));
    menu->findChild<QAction *>(actionName)->trigger();
    QCOMPARE(saved->requests, 1);
    QCOMPARE(cancelled->requests, 1);
    QCOMPARE(later->requests, 0);
    QCOMPARE(pane.contentIds(), QStringList({QStringLiteral("anchor"),
        QStringLiteral("cancelled"), QStringLiteral("later")}));
    QCOMPARE(pane.currentContentId(), QStringLiteral("cancelled"));
    cancelled->allowClose = true;
    menu.reset(pane.createTabContextMenu(QStringLiteral("anchor")));
    menu->findChild<QAction *>(actionName)->trigger();
    QCOMPARE(pane.contentIds(), QStringList({QStringLiteral("anchor")}));
}

void MyAdsTest::closeRequestRejectsReentryAndRevalidatesOwnership()
{
    WorkspaceWidget workspace;
    PaneWidget *source = workspace.rootPane();
    PaneWidget *target = workspace.split(source, Placement::Right);
    // Keep the source alive even after its edit tab is moved.
    QVERIFY(source->addContent(QStringLiteral("anchor"), QStringLiteral("Anchor"), new QLabel));
    auto *page = new CloseAwarePage;
    page->allowClose = true;
    QVERIFY(source->addContent(QStringLiteral("edit"), QStringLiteral("Edit"), page));
    page->onRequest = [&] {
        QVERIFY(!source->removeContent(QStringLiteral("edit")));
        QVERIFY(workspace.moveContent(source->paneId(), QStringLiteral("edit"),
                                      target->paneId(), DropZone::Center));
    };
    QVERIFY(!source->removeContent(QStringLiteral("edit")));
    QCOMPARE(page->requests, 1);
    QCOMPARE(target->contentWidget(QStringLiteral("edit")), page);
    // Moving and rebuilding retain the page's close behavior without asking again.
    PaneWidget *bottom = workspace.split(target, Placement::Bottom);
    QVERIFY(bottom);
    QCOMPARE(page->requests, 1);
    page->onRequest = {};
    page->allowClose = false;
    QVERIFY(!target->removeContent(QStringLiteral("edit")));
    QCOMPARE(page->requests, 2);
    page->allowClose = true;
    QVERIFY(target->removeContent(QStringLiteral("edit")));
}

void MyAdsTest::workspacePreservesContentWhileSplitting()
{
    WorkspaceWidget workspace;
    PaneWidget *root = workspace.rootPane();
    QPointer<PaneWidget> rootGuard(root);
    ContentConfig first(QStringLiteral("list"));
    first.setWindowTitle(QStringLiteral("列表"))
         .setWidget(new QLabel(QStringLiteral("A"))).setCloseable(false);
    QVERIFY(workspace.fillPane(root, first));
    QWidget *originalWidget = root->contentWidget(QStringLiteral("list"));
    PaneWidget *right = workspace.split(root, Placement::Right, 35);
    QVERIFY(right);
    QCOMPARE(workspace.pane(QStringLiteral("pane_1")), root);
    QCOMPARE(workspace.findPaneByContentId(QStringLiteral("list"))->contentWidget(
                 QStringLiteral("list")), originalWidget);
    QCOMPARE(workspace.layoutTree().root()->sizes, QList<int>({650, 350}));
    workspace.show();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QVERIFY(rootGuard);
    QCOMPARE(rootGuard->contentCount(), 1);
    QVERIFY(rootGuard->isVisible());

    PaneWidget *bottom = workspace.split(rootGuard, Placement::Bottom);
    QVERIFY(bottom);
    ContentConfig second(QStringLiteral("search"));
    second.setWindowTitle(QStringLiteral("搜索结果"))
          .setWidget(new QLabel(QStringLiteral("B")));
    QVERIFY(workspace.fillPane(rootGuard, second));
    QCoreApplication::processEvents();
    QCOMPARE(rootGuard->contentCount(), 2);
    QTabBar *tabBar = rootGuard->findChild<QTabBar *>(QStringLiteral("MyAdsTabBar"));
    QVERIFY(tabBar);
    QVERIFY(tabBar->isVisible());
    QCOMPARE(tabBar->height(), 32);
}

void MyAdsTest::workspaceBatchesProgrammaticSplits()
{
    WorkspaceWidget workspace;
    QSignalSpy layoutChanges(&workspace, &WorkspaceWidget::layoutChanged);
    PaneWidget *root = workspace.rootPane();

    workspace.beginLayoutUpdate();
    PaneWidget *basic = workspace.split(root, Placement::Right, 70);
    PaneWidget *tag = workspace.split(basic, Placement::Right, 25);
    PaneWidget *text = workspace.split(basic, Placement::Bottom, 42);
    PaneWidget *fanart = workspace.split(tag, Placement::Bottom, 20);
    PaneWidget *force = workspace.split(tag, Placement::Right, 50);
    PaneWidget *actress = workspace.split(root, Placement::Bottom, 50);
    PaneWidget *editor = workspace.split(force, Placement::Bottom, 40);

    QVERIFY(basic);
    QVERIFY(tag);
    QVERIFY(text);
    QVERIFY(fanart);
    QVERIFY(force);
    QVERIFY(actress);
    QVERIFY(editor);
    QCOMPARE(layoutChanges.size(), 0);

    workspace.endLayoutUpdate();
    QCOMPARE(layoutChanges.size(), 1);
    QCOMPARE(workspace.panes().size(), 8);
    QCOMPARE(workspace.pane(root->paneId()), root);
    QCOMPARE(workspace.pane(editor->paneId()), editor);
}

void MyAdsTest::workspaceMovesTabsAcrossPanes()
{
    WorkspaceWidget workspace;
    PaneWidget *left = workspace.rootPane();
    ContentConfig first(QStringLiteral("one"));
    first.setWidget(new QLabel(QStringLiteral("one")));
    ContentConfig second(QStringLiteral("two"));
    second.setWidget(new QLabel(QStringLiteral("two")));
    QVERIFY(workspace.fillPane(left, first));
    QVERIFY(workspace.fillPane(left, second));
    PaneWidget *right = workspace.split(left, Placement::Right);
    QCOMPARE(workspace.layoutTree().root()->sizes, QList<int>({500, 500}));
    ContentConfig third(QStringLiteral("three"));
    third.setWidget(new QLabel(QStringLiteral("three")));
    QVERIFY(workspace.fillPane(right, third));
    QVERIFY(workspace.moveContent(QStringLiteral("pane_1"), QStringLiteral("one"),
                                  right->paneId(), DropZone::Center));
    QCOMPARE(workspace.pane(right->paneId())->contentIds(),
             QStringList({QStringLiteral("three"), QStringLiteral("one")}));
    QVERIFY(workspace.moveContent(QStringLiteral("pane_1"), QStringLiteral("two"),
                                  right->paneId(), DropZone::RootBottom));
    QVERIFY(!workspace.pane(QStringLiteral("pane_1")));
    QCOMPARE(workspace.layoutTree().paneIds().size(), 2);
    QVERIFY(workspace.findPaneByContentId(QStringLiteral("two")));
}

void MyAdsTest::workspaceSavesAndRestoresContent()
{
    QTemporaryDir directory;
    WorkspaceWidget source;
    PaneWidget *root = source.rootPane();
    ContentConfig list(QStringLiteral("movie_list"));
    list.setWindowTitle(QStringLiteral("影片列表"))
        .setWidget(new QLabel(QStringLiteral("list"))).setCloseable(false);
    QVERIFY(source.fillPane(root, list));
    PaneWidget *detailPane = source.split(root, Placement::Right, 30);
    detailPane->setIconOnly(true);
    ContentConfig detail(QStringLiteral("movie_detail"));
    detail.setWindowTitle(QStringLiteral("影片详情"))
          .setWidget(new QLabel(QStringLiteral("detail")));
    QVERIFY(source.fillPane(detailPane, detail));
    ContentConfig auxiliary(QStringLiteral("detail_aux"));
    auxiliary.setWindowTitle(QStringLiteral("附加资料"))
             .setWidget(new QLabel(QStringLiteral("auxiliary")));
    QVERIFY(source.fillPane(detailPane, auxiliary));
    QVERIFY(detailPane->setCurrentContentId(QStringLiteral("movie_detail")));
    const QString path = directory.filePath(QStringLiteral("workspace.json"));
    QString error;
    QVERIFY2(source.saveLayout(path,
        [](const PaneWidget *pane, const QString &id) {
            return QJsonObject{{QStringLiteral("content_id"), id},
                               {QStringLiteral("title"), pane->contentTitle(id)},
                               {QStringLiteral("closeable"), pane->isContentCloseable(id)}};
        }, {}, &error), qPrintable(error));

    WorkspaceWidget restored;
    QVERIFY2(restored.loadLayout(path,
        [](const QJsonObject &value) -> std::optional<ContentConfig> {
            const QString id = value.value(QStringLiteral("content_id")).toString();
            ContentConfig config(id);
            config.setWindowTitle(value.value(QStringLiteral("title")).toString())
                  .setCloseable(value.value(QStringLiteral("closeable")).toBool(true))
                  .setWidget(new QLabel(id));
            return config;
        }, &error), qPrintable(error));
    QCOMPARE(restored.layoutTree().paneIds(), source.layoutTree().paneIds());
    QVERIFY(restored.findPaneByContentId(QStringLiteral("movie_list")));
    PaneWidget *restoredDetail = restored.findPaneByContentId(QStringLiteral("movie_detail"));
    QVERIFY(restoredDetail);
    QVERIFY(restoredDetail->iconOnly());
    QCOMPARE(restoredDetail->contentIds(), QStringList({QStringLiteral("movie_detail"),
                                                        QStringLiteral("detail_aux")}));
    QCOMPARE(restoredDetail->currentContentId(), QStringLiteral("movie_detail"));
}

void MyAdsTest::sidebarsToggleAndCollapseOnFocusLoss()
{
    WorkspaceWidget workspace;
    workspace.resize(1000, 500);
    auto *center = workspace.rootPane();
    auto *left = workspace.sidebarPane(SidebarSide::Left);
    auto *right = workspace.sidebarPane(SidebarSide::Right);
    QVERIFY(!workspace.isSidebarOpen(SidebarSide::Left));
    QVERIFY(!workspace.isSidebarOpen(SidebarSide::Right));
    QCOMPARE(left->contentCount(), 0);
    QCOMPARE(right->contentCount(), 0);

    auto *editor = new QLineEdit;
    QVERIFY(workspace.fillPane(left, ContentConfig("editor").setWidget(editor)));
    workspace.show();
    workspace.activateWindow();
    QTRY_VERIFY(workspace.isActiveWindow());

    workspace.toggleSidebar(SidebarSide::Left);
    QVERIFY(left->isVisible());
    QCOMPARE(workspace.activePane(), left);
    center->focusTabBar();
    QCoreApplication::processEvents();
    QVERIFY(left->isVisible());
    workspace.toggleSidebar(SidebarSide::Right);
    QVERIFY(right->isVisible());
    QTRY_VERIFY(left->mapTo(&workspace, QPoint(left->width(), 0)).x() <=
                center->mapTo(&workspace, QPoint()).x());
    QTRY_VERIFY(center->mapTo(&workspace, QPoint(center->width(), 0)).x() <=
                right->mapTo(&workspace, QPoint()).x());
    workspace.toggleSidebar(SidebarSide::Right);
    QVERIFY(!workspace.isSidebarOpen(SidebarSide::Right));

    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    workspace.setSidebarAutoCollapse(true);
    left->focusTabBar();
    QCoreApplication::processEvents();
    QVERIFY(left->isVisible());
    center->focusTabBar();
    QTRY_VERIFY(!workspace.isSidebarOpen(SidebarSide::Left));
    QCOMPARE(workspace.activePane(), center);

    workspace.toggleSidebar(SidebarSide::Left);
    QVERIFY(left->isVisible());
    QCOMPARE(left->contentWidget(QStringLiteral("editor")), editor);
    workspace.toggleSidebar(SidebarSide::Left);
    QVERIFY(!workspace.isSidebarOpen(SidebarSide::Left));
}

void MyAdsTest::sidebarTabsSurviveLayoutRestore()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("layout.json"));
    WorkspaceWidget workspace;
    auto *root = workspace.rootPane();
    auto *right = workspace.sidebarPane(SidebarSide::Right);
    const auto id = QStringLiteral("sidebar-tab");
    auto *widget = new QLabel;
    QVERIFY(workspace.fillPane(root, ContentConfig(id).setWidget(widget)));
    QVERIFY(workspace.moveContent(root->paneId(), id, right->paneId(), DropZone::Center));
    QCOMPARE(right->contentWidget(id), widget);
    QCOMPARE(workspace.layoutTree().paneIds(), QList<QString>{root->paneId()});
    workspace.toggleSidebar(SidebarSide::Right);
    QVERIFY(!workspace.isSidebarOpen(SidebarSide::Right));
    QVERIFY(workspace.saveLayout(path, [](const PaneWidget *, const QString &contentId) {
        return QJsonObject{{QStringLiteral("content_id"), contentId}};
    }));

    QVERIFY(workspace.loadLayout(path, [widget](const QJsonObject &descriptor) {
        return ContentConfig(descriptor.value(QStringLiteral("content_id")).toString()).setWidget(widget);
    }));
    right = workspace.sidebarPane(SidebarSide::Right);
    QVERIFY(!workspace.isSidebarOpen(SidebarSide::Right));
    QCOMPARE(workspace.findPaneByContentId(id), right);
    QCOMPARE(right->contentWidget(id), widget);

    workspace.toggleSidebar(SidebarSide::Right);
    root = workspace.rootPane();
    QVERIFY(workspace.moveContent(right->paneId(), id, root->paneId(), DropZone::Center));
    QCOMPARE(root->contentWidget(id), widget);
    QCOMPARE(workspace.sidebarPane(SidebarSide::Right), right);
    QCOMPARE(right->contentCount(), 0);
}

void MyAdsTest::sidebarWidthsAreRestoredAsRatios()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("layout.json"));
    WorkspaceWidget workspace;
    workspace.resize(1000, 500);
    workspace.show();
    workspace.toggleSidebar(SidebarSide::Left);
    workspace.toggleSidebar(SidebarSide::Right);
    QVERIFY(QTest::qWaitForWindowExposed(&workspace));
    QCoreApplication::processEvents();
    auto *splitter = workspace.findChild<QSplitter *>(QStringLiteral("MyAdsSidebarSplitter"));
    QVERIFY(splitter);
    const auto widthRatio = [splitter](int index) {
        const auto sizes = splitter->sizes();
        return double(sizes.at(index)) / (sizes.at(0) + sizes.at(1) + sizes.at(2));
    };
    QTRY_VERIFY(qAbs(widthRatio(0) - 0.22) < 0.01);
    QTRY_VERIFY(qAbs(widthRatio(2) - 0.22) < 0.01);

    auto *handle = splitter->handle(1);
    const QPoint start = handle->rect().center();
    const QPoint globalStart = handle->mapToGlobal(start);
    QMouseEvent press(QEvent::MouseButtonPress, start, globalStart,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &press);
    QMouseEvent move(QEvent::MouseMove, start + QPoint(100, 0), globalStart + QPoint(100, 0),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move);
    const double savedRatio = widthRatio(0);
    QVERIFY2(savedRatio > 0.28, qPrintable(QString::number(savedRatio)));
    QMouseEvent release(QEvent::MouseButtonRelease, handle->rect().center(),
                        globalStart + QPoint(100, 0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &release);
    workspace.toggleSidebar(SidebarSide::Left);
    workspace.resize(1400, 500);
    workspace.toggleSidebar(SidebarSide::Left);
    QTRY_VERIFY(qAbs(widthRatio(0) - savedRatio) < 0.01);
    QTRY_VERIFY(qAbs(widthRatio(2) - 0.22) < 0.01);

    QVERIFY(workspace.save(path));
    WorkspaceWidget restored;
    QVERIFY(restored.load(path));
    restored.resize(900, 500);
    restored.show();
    restored.toggleSidebar(SidebarSide::Left);
    auto *restoredLeft = restored.sidebarPane(SidebarSide::Left);
    QTRY_VERIFY(qAbs(double(restoredLeft->width()) / (restored.width() - splitter->handleWidth())
                     - savedRatio) < 0.01);
}

void MyAdsTest::sidebarSplitsVertically_data()
{
    QTest::addColumn<bool>("left");
    QTest::newRow("left-top") << true;
    QTest::newRow("right-bottom") << false;
}

void MyAdsTest::sidebarSplitsVertically()
{
    QFETCH(bool, left);
    const auto side = left ? SidebarSide::Left : SidebarSide::Right;
    const QString residentId = QStringLiteral("resident");
    const QString movingId = QStringLiteral("moving");
    WorkspaceWidget workspace;
    workspace.resize(1000, 600);
    workspace.show();
    workspace.activateWindow();
    QTRY_VERIFY(workspace.isActiveWindow());
    workspace.toggleSidebar(side);
    auto *sidebar = workspace.sidebarPane(side);
    auto *center = workspace.rootPane();
    auto *editor = new QLineEdit;
    QVERIFY(workspace.fillPane(sidebar, ContentConfig(residentId).setWidget(editor)));
    QVERIFY(workspace.fillPane(center, ContentConfig(movingId).setWidget(new QLabel)));
    QVERIFY(!workspace.split(sidebar, Placement::Left));
    QVERIFY(!workspace.split(sidebar, Placement::Right));
    for (const auto zone : {DropZone::Left, DropZone::Right, DropZone::RootLeft, DropZone::RootRight})
        QVERIFY(!workspace.moveContent(center->paneId(), movingId, sidebar->paneId(), zone));
    QCOMPARE(workspace.findPaneByContentId(movingId), center);

    const auto body = sidebar->contentRect();
    const QPoint dropPosition(body.center().x(),
                              left ? body.top() + body.height() / 8 : body.bottom() - body.height() / 8);
    QMimeData mime;
    mime.setData(MyAdsTabMimeType, QJsonDocument(QJsonObject{
        {"pane_id", center->paneId()}, {"content_id", movingId}}).toJson());
    QDragEnterEvent enter(QPointF(dropPosition), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(sidebar, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(dropPosition, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(sidebar, &drop);
    QVERIFY(drop.isAccepted());
    auto *moved = workspace.findPaneByContentId(movingId);
    QVERIFY(moved && moved != sidebar && workspace.isSidebar(moved));
    QCOMPARE(workspace.panes(), QList<PaneWidget *>{center});
    QCOMPARE(workspace.sidebarPane(side), left ? moved : sidebar);
    auto *splitter = qobject_cast<QSplitter *>(moved->parentWidget());
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Vertical);
    QCOMPARE(splitter->widget(left ? 0 : 1), moved);
    QTRY_COMPARE(moved->width(), sidebar->width());

    workspace.setSidebarAutoCollapse(true);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QCoreApplication::processEvents();
    QVERIFY(workspace.isSidebarOpen(side));
    center->focusTabBar();
    QTRY_VERIFY(!workspace.isSidebarOpen(side));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("layout.json"));
    QVERIFY(workspace.saveLayout(path, [](const PaneWidget *, const QString &id) {
        return QJsonObject{{QStringLiteral("content_id"), id}};
    }));
    QVERIFY(workspace.loadLayout(path, [](const QJsonObject &descriptor) {
        return ContentConfig(descriptor.value(QStringLiteral("content_id")).toString())
            .setWidget(new QLabel);
    }));
    QVERIFY(!workspace.isSidebarOpen(side));
    workspace.toggleSidebar(side);
    sidebar = workspace.findPaneByContentId(residentId);
    moved = workspace.findPaneByContentId(movingId);
    QVERIFY(sidebar && moved && sidebar != moved);
    QCOMPARE(workspace.sidebarPane(side), left ? moved : sidebar);
    QVERIFY(moved->removeContent(movingId));
    QCOMPARE(workspace.sidebarPane(side), sidebar);
    QCoreApplication::processEvents();
    QVERIFY(workspace.isSidebarOpen(side));
}

void MyAdsTest::unknownSchemaDoesNotDestroyWorkspace()
{
    QTemporaryDir directory;
    WorkspaceWidget workspace;
    ContentConfig content(QStringLiteral("keep"));
    content.setWidget(new QLabel(QStringLiteral("keep")));
    QVERIFY(workspace.fillPane(workspace.rootPane(), content));
    const QString path = directory.filePath(QStringLiteral("future.json"));
    QString error;
    QVERIFY(saveLayoutAtomic(path,
        QJsonObject{{QStringLiteral("schema_version"), 999},
                    {QStringLiteral("layout"), workspace.layoutTree().toJson()}}, &error));
    QVERIFY(!workspace.loadLayout(path, {}, &error));
    QVERIFY(workspace.findPaneByContentId(QStringLiteral("keep")));
}

void MyAdsTest::invalidLayoutDoesNotDestroyWorkspace()
{
    QTemporaryDir directory;
    WorkspaceWidget workspace;
    ContentConfig content(QStringLiteral("keep"));
    content.setWidget(new QLabel(QStringLiteral("keep")));
    QVERIFY(workspace.fillPane(workspace.rootPane(), content));
    const QString path = directory.filePath(QStringLiteral("invalid.json"));
    QString error;
    QVERIFY(saveLayoutAtomic(path,
        QJsonObject{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("layout"), QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("pane")},
                        {QStringLiteral("pane_id"), QStringLiteral("not_a_root")}}}}, &error));
    QVERIFY(!workspace.loadLayout(path, {}, &error));
    QVERIFY(workspace.findPaneByContentId(QStringLiteral("keep")));
}

void MyAdsTest::factoryFailurePreservesWorkspace_data()
{
    QTest::addColumn<int>("failureKind");
    QTest::newRow("exception") << 0;
    QTest::newRow("non-standard-exception") << 1;
    QTest::newRow("duplicate-widget") << 2;
}

void MyAdsTest::factoryFailurePreservesWorkspace()
{
    QFETCH(int, failureKind);
    QTemporaryDir directory;
    WorkspaceWidget source;
    ContentConfig content(QStringLiteral("saved"));
    content.setWidget(new QLabel(QStringLiteral("saved")));
    QVERIFY(source.fillPane(source.rootPane(), content));
    QVERIFY(source.fillPane(source.rootPane(), ContentConfig("second").setWidget(new QLabel)));
    QVERIFY(source.fillPane(source.rootPane(), ContentConfig("third").setWidget(new QLabel)));
    const QString path = directory.filePath(QStringLiteral("factory.json"));
    QString error;
    QVERIFY(source.saveLayout(path, [](const PaneWidget *, const QString &id) {
        return QJsonObject{{QStringLiteral("content_id"), id}};
    }, {}, &error));

    WorkspaceWidget restored;
    auto *left = restored.rootPane();
    auto *right = restored.split(left, Placement::Right, 30);
    QPointer<QWidget> original = new QLabel("keep");
    QVERIFY(restored.fillPane(left, ContentConfig("keep").setWidget(original)));
    QVERIFY(restored.fillPane(left, ContentConfig("other").setWidget(new QLabel)));
    QVERIFY(left->setCurrentContentId("keep"));
    right->setIconOnly(true);
    restored.setActivePane(right);
    const QJsonObject originalTree = restored.layoutTree().toJson();
    const QStringList originalOrder = left->contentIds();
    QSignalSpy changes(&restored, &WorkspaceWidget::layoutChanged);
    QPointer<QWidget> preparedPage;
    int calls = 0;
    bool originalUntouched = true;
    QVERIFY(!restored.loadLayout(path,
        [&](const QJsonObject &) -> std::optional<ContentConfig> {
            ++calls;
            originalUntouched = originalUntouched && restored.rootPane() == left &&
                left->contentIds() == originalOrder && left->contentWidget("keep") == original;
            if (calls == 1) {
                preparedPage = new QLabel("temporary");
                return ContentConfig("new").setWidget(preparedPage);
            }
            if (calls == 2) return ContentConfig("reused").setWidget(original);
            if (failureKind == 1) throw 42;
            if (failureKind == 2) return ContentConfig("duplicate").setWidget(original);
            throw std::runtime_error("factory failed");
        }, &error));
    QCOMPARE(calls, 3);
    QVERIFY(originalUntouched);
    QVERIFY(!error.isEmpty());
    if (failureKind == 0) QCOMPARE(error, QStringLiteral("factory failed"));
    QCOMPARE(restored.layoutTree().toJson(), originalTree);
    QCOMPARE(restored.rootPane(), left);
    QCOMPARE(left->contentIds(), originalOrder);
    QCOMPARE(left->currentContentId(), QStringLiteral("keep"));
    QCOMPARE(left->contentWidget("keep"), original.data());
    QCOMPARE(restored.activePane(), right);
    QVERIFY(right->iconOnly());
    QCOMPARE(changes.count(), 0);
    QVERIFY(preparedPage.isNull());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(original);
    QCOMPARE(left->contentWidget("keep"), original.data());
}

void MyAdsTest::restoredWorkspaceReusesPagesAndKeepsCallbacks()
{
    QTemporaryDir directory;
    WorkspaceWidget workspace;
    auto *left = workspace.rootPane();
    auto *right = workspace.split(left, Placement::Right);
    QPointer<QWidget> page = new QLabel("reused");
    QVERIFY(workspace.fillPane(left, ContentConfig("keep").setWidget(page)
        .setClosePolicy(ClosePolicy::KeepAlive)));
    QVERIFY(workspace.fillPane(right, ContentConfig("other").setWidget(new QLabel)));
    const QString path = directory.filePath("reuse.json");
    QString error;
    QVERIFY(workspace.saveLayout(path, [](const PaneWidget *, const QString &id) {
        return QJsonObject{{"content_id", id}};
    }, {}, &error));
    QPointer<PaneWidget> oldPane(left);
    QVERIFY2(workspace.loadLayout(path, [&](const QJsonObject &descriptor)
        -> std::optional<ContentConfig> {
        const QString id = descriptor.value("content_id").toString();
        return ContentConfig(id).setWidget(id == "keep" ? page.data() : new QLabel)
            .setClosePolicy(ClosePolicy::KeepAlive);
    }, &error), qPrintable(error));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(oldPane.isNull());
    QVERIFY(page);
    left = workspace.findPaneByContentId("keep");
    right = workspace.findPaneByContentId("other");
    QVERIFY(left && right);
    QCOMPARE(left->contentWidget("keep"), page.data());
    QCOMPARE(left->contentClosePolicy("keep"), ClosePolicy::KeepAlive);
    auto *splitter = workspace.findChild<QSplitter *>(QStringLiteral("MyAdsSplitter"));
    QVERIFY(splitter);
    workspace.resize(800, 400);
    workspace.show();
    QCoreApplication::processEvents();
    splitter->setSizes({200, 600});
    QVERIFY(QMetaObject::invokeMethod(splitter, "splitterMoved", Qt::DirectConnection,
        Q_ARG(int, 200), Q_ARG(int, 1)));
    QCOMPARE(workspace.layoutTree().root()->sizes, splitter->sizes());
    // The adopted panes must dispatch their menus to the surviving workspace.
    QSignalSpy moved(&workspace, &WorkspaceWidget::contentMoved);
    std::unique_ptr<QMenu> menu(left->createTabContextMenu("keep"));
    auto *moveMenu = menu->findChild<QMenu *>(QStringLiteral("moveTabToGroup"));
    QVERIFY(moveMenu && moveMenu->actions().size() == 1);
    moveMenu->actions().first()->trigger();
    QCOMPARE(moved.count(), 1);
    QCOMPARE(workspace.findPaneByContentId("keep"), right);
    QCOMPARE(right->contentWidget("keep"), page.data());
}

void MyAdsTest::nestedSplitterResizesBeforeRelease()
{
    WorkspaceWidget workspace;
    PaneWidget *left = workspace.rootPane();
    QVERIFY(workspace.split(left, Placement::Right));
    PaneWidget *bottom = workspace.split(left, Placement::Bottom);
    QVERIFY(bottom);
    workspace.resize(800, 600);
    workspace.show();
    QCoreApplication::processEvents();

    auto *splitter = qobject_cast<QSplitter *>(left->parentWidget());
    QVERIFY(splitter);
    QSplitterHandle *handle = splitter->handle(1);
    QVERIFY(handle);
    const QPoint press = handle->rect().center();
    const QPoint global = handle->mapToGlobal(press);
    const int oldHeight = left->height();
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, press);
    const QPoint offset(0, 50);
    QMouseEvent move(QEvent::MouseMove, press + offset, global + offset, Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move);
    QVERIFY(left->height() > oldHeight);
    QVERIFY(bottom->height() > 0);
    QCOMPARE(workspace.layoutTree().root()->children.first()->sizes, splitter->sizes());
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
}

void MyAdsTest::hitTestingMatchesPythonZones()
{
    const QRect rectangle(0, 0, 100, 100);
    QCOMPARE(WorkspaceWidget::hitTest(rectangle, QPoint(50, 50)), DropZone::Center);
    QCOMPARE(WorkspaceWidget::hitTest(rectangle, QPoint(50, 10)), DropZone::Top);
    QCOMPARE(WorkspaceWidget::hitTest(rectangle, QPoint(50, 90)), DropZone::Bottom);
    QCOMPARE(WorkspaceWidget::hitTest(rectangle, QPoint(10, 50)), DropZone::Left);
    QCOMPARE(WorkspaceWidget::hitTest(rectangle, QPoint(90, 50)), DropZone::Right);
}

void MyAdsTest::previewGeometryMatchesPythonOverlay()
{
    const QRect paneArea(17, 23, 101, 81);
    QCOMPARE(WorkspaceWidget::previewRectForZone(paneArea, DropZone::Center), paneArea);
    QCOMPARE(WorkspaceWidget::previewRectForZone(paneArea, DropZone::Left),
             QRect(17, 23, 50, 81));
    QCOMPARE(WorkspaceWidget::previewRectForZone(paneArea, DropZone::Right),
             QRect(67, 23, 51, 81));
    QCOMPARE(WorkspaceWidget::previewRectForZone(paneArea, DropZone::Top),
             QRect(17, 23, 101, 40));
    QCOMPARE(WorkspaceWidget::previewRectForZone(paneArea, DropZone::Bottom),
             QRect(17, 63, 101, 41));
}

void MyAdsTest::activePaneFollowsUserInteraction()
{
    WorkspaceWidget workspace;
    PaneWidget *left = workspace.rootPane();
    auto *leftContent = new QLabel(QStringLiteral("left"));
    ContentConfig content(QStringLiteral("left_content"));
    content.setWidget(leftContent);
    QVERIFY(workspace.fillPane(left, content));
    PaneWidget *right = workspace.split(left, Placement::Right);
    QCOMPARE(workspace.activePane(), right);

    QTest::mouseClick(leftContent, Qt::LeftButton);
    QCOMPARE(workspace.activePane(), left);
    PaneWidget *bottom = workspace.split(workspace.activePane(), Placement::Bottom);
    QVERIFY(bottom);
    QCOMPARE(workspace.activePane(), bottom);
}

void MyAdsTest::dropsTabsAtInsertionLine_data()
{
    QTest::addColumn<int>("gap");
    QTest::addColumn<bool>("rtl");
    QTest::addColumn<int>("targetCount");
    QTest::addColumn<bool>("blank");
    for (bool rtl : {false, true})
        for (int gap = 0; gap <= 3; ++gap)
            QTest::newRow(qPrintable(QStringLiteral("%1-gap-%2")
                .arg(rtl ? "rtl" : "ltr").arg(gap))) << gap << rtl << 3 << false;
    QTest::newRow("blank-strip") << 3 << false << 3 << true;
    QTest::newRow("empty-strip") << 0 << false << 0 << true;
}

void MyAdsTest::dropsTabsAtInsertionLine()
{
    QFETCH(int, gap);
    QFETCH(bool, rtl);
    QFETCH(int, targetCount);
    QFETCH(bool, blank);
    WorkspaceWidget workspace;
    workspace.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
    workspace.resize(1000, 500);
    PaneWidget *source = workspace.rootPane();
    const QString sourceId = source->paneId();
    auto *page = new CloseAwarePage;
    QVERIFY(workspace.fillPane(source, ContentConfig("moving").setWidget(page)
        .setCloseable(false).setClosePolicy(ClosePolicy::KeepAlive)));
    PaneWidget *target = workspace.split(source, Placement::Right);
    const QStringList original = QStringList{"a", "b", "c"}.mid(0, targetCount);
    for (const QString &id : original)
        QVERIFY(workspace.fillPane(target, ContentConfig(id).setWidget(new QLabel(id))));
    workspace.show();
    QCoreApplication::processEvents();
    auto *bar = target->findChild<QTabBar *>("MyAdsTabBar");
    QVERIFY(bar);
    // Use the first/second half of a real tab, not an implementation-derived
    // marker coordinate, to verify before/after placement.
    const int tabIndex = qMin(gap, 2);
    const QRect tab = bar->tabRect(tabIndex);
    const bool after = gap == 3;
    const int offset = (after != rtl) ? tab.width() * 3 / 4 : tab.width() / 4;
    const QPoint local = blank ? QPoint(bar->width() - 8, bar->height() / 2)
                               : QPoint(tab.left() + offset, tab.center().y());
    const QPoint global = bar->mapToGlobal(local);
    QCOMPARE(target->tabInsertionIndex(global), gap);

    QWidget *preview = nullptr;
    for (QWidget *child : workspace.findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        if (child->testAttribute(Qt::WA_TransparentForMouseEvents)) preview = child;
    QVERIFY(preview);
    QMimeData mime;
    mime.setData(MyAdsTabMimeType, QJsonDocument(QJsonObject{
        {"pane_id", sourceId}, {"content_id", "moving"}}).toJson());
    QDragEnterEvent enter(QPointF(local), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &enter);
    QVERIFY(enter.isAccepted());
    QDragMoveEvent move(QPointF(local), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &move);
    QVERIFY(move.isAccepted());
    QVERIFY(preview->isVisible());
    const QRect marker = target->tabInsertionRect(gap);
    QCOMPARE(marker.width(), 3);
    // The painted preview must be the insertion line, not a pane split.
    const QPoint markerCenter = preview->mapFromGlobal(target->mapToGlobal(marker.center()));
    const QImage previewImage = preview->grab().toImage();
    QCOMPARE(previewImage.pixelColor(markerCenter * previewImage.devicePixelRatio()),
             workspace.theme().primary);
    QDragLeaveEvent leave;
    QApplication::sendEvent(bar, &leave);
    QVERIFY(!preview->isVisible());
    QDragEnterEvent reenter(QPointF(local), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &reenter);
    QDropEvent drop(local, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(!preview->isVisible());
    QStringList expected = original;
    expected.insert(gap, "moving");
    QCOMPARE(target->contentIds(), expected);
    QCOMPARE(target->currentContentId(), QString("moving"));
    QCOMPARE(target->contentWidget("moving"), page);
    QCOMPARE(target->contentClosePolicy("moving"), ClosePolicy::KeepAlive);
    QVERIFY(!target->isContentCloseable("moving"));
    QCOMPARE(page->requests, 0);
    QCOMPARE(workspace.panes().size(), 1);
    QCOMPARE(workspace.activePane(), target);
}

void MyAdsTest::docksThroughGraphicsView_data()
{
    QTest::addColumn<int>("zone");
    QTest::newRow("center") << int(DropZone::Center);
    QTest::newRow("pane-right") << int(DropZone::Right);
    QTest::newRow("root-bottom") << int(DropZone::RootBottom);
}

void MyAdsTest::docksThroughGraphicsView()
{
    QFETCH(int, zone);
    DockTheme theme;
    theme.previewAlpha = 255;
    WorkspaceWidget workspace(nullptr, theme);
    workspace.resize(800, 600);
    PaneWidget *source = workspace.rootPane();
    const QString sourceId = source->paneId();
    QVERIFY(workspace.fillPane(source, ContentConfig("moving").setWidget(new QLabel("moving"))));
    QVERIFY(workspace.fillPane(source, ContentConfig("stay").setWidget(new QLabel("stay"))));
    PaneWidget *target = workspace.split(source, Placement::Right);
    const QString targetId = target->paneId();
    auto *view = new QGraphicsView;
    auto *scene = new QGraphicsScene(view);
    scene->addText("tag selector");
    view->setScene(scene);
    QVERIFY(workspace.fillPane(target, ContentConfig("tags").setWidget(view)));
    workspace.show();
    QCoreApplication::processEvents();

    QWidget *preview = nullptr;
    for (QWidget *child : workspace.findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        if (child->testAttribute(Qt::WA_TransparentForMouseEvents)) preview = child;
    QVERIFY(preview);
    QWidget *receiver = view->viewport();
    QPoint global = target->mapToGlobal(target->rect().center());
    if (DropZone(zone) == DropZone::Right)
        global = target->mapToGlobal(QPoint(target->width() * 9 / 10, target->height() / 2));
    if (DropZone(zone) == DropZone::RootBottom)
        global = workspace.mapToGlobal(QPoint(workspace.width() * 6 / 10, workspace.height() - 12));
    const QPoint local = receiver->mapFromGlobal(global);
    QVERIFY(receiver->rect().contains(local));
    QMimeData mime;
    mime.setData(MyAdsTabMimeType, QJsonDocument(QJsonObject{
        {"pane_id", sourceId}, {"content_id", "moving"}}).toJson());
    QDragEnterEvent enter(QPointF(local), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &enter);
    QVERIFY(enter.isAccepted());
    const QRect originalTargetRect(target->mapTo(&workspace, QPoint()), target->size());
    const QJsonObject originalLayout = workspace.layoutTree().toJson();
    QDragMoveEvent move(QPointF(local), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &move);
    QVERIFY(move.isAccepted());
    QVERIFY(preview->isVisible());
    QCOMPARE(QRect(target->mapTo(&workspace, QPoint()), target->size()), originalTargetRect);
    QCOMPARE(workspace.layoutTree().toJson(), originalLayout);
    QVERIFY(preview->findChildren<PaneWidget *>().isEmpty());
    const QImage previewImage = preview->grab().toImage();
    const QRect area = DropZone(zone) == DropZone::RootBottom ? workspace.rect() : originalTargetRect;
    const QPoint maskCenter = WorkspaceWidget::previewRectForZone(area, DropZone(zone)).center();
    QCOMPARE(previewImage.pixelColor(maskCenter * previewImage.devicePixelRatio()),
             theme.primary);
    QCOMPARE(workspace.findPaneByContentId("moving"), source);
    QDragLeaveEvent leave;
    QApplication::sendEvent(receiver, &leave);
    QVERIFY(!preview->isVisible());
    QDragEnterEvent reenter(QPointF(local), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &reenter);
    QVERIFY(reenter.isAccepted());
    QDropEvent drop(local, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(drop.dropAction(), Qt::MoveAction);
    QVERIFY(!preview->isVisible());
    PaneWidget *destination = workspace.findPaneByContentId("moving");
    QVERIFY(destination);
    QCOMPARE(destination->paneId() == targetId, DropZone(zone) == DropZone::Center);
    QCOMPARE(workspace.panes().size(), DropZone(zone) == DropZone::Center ? 2 : 3);
    QCOMPARE(workspace.pane(sourceId)->contentIds(), QStringList{"stay"});
    QCOMPARE(workspace.findPaneByContentId("tags")->contentWidget("tags"), view);
}

void MyAdsTest::preservesChildDropsAndRejectsInvalidDockData()
{
    class DropReceiver final : public QWidget {
    public:
        DropReceiver() { setAcceptDrops(true); }
        int received = 0;
    protected:
        void dragEnterEvent(QDragEnterEvent *event) override {
            ++received;
            event->acceptProposedAction();
        }
    };
    WorkspaceWidget workspace;
    WorkspaceWidget otherWorkspace;
    auto *child = new DropReceiver;
    QVERIFY(workspace.fillPane(workspace.rootPane(), ContentConfig("keep").setWidget(child)));
    workspace.resize(400, 300);
    workspace.show();
    QCoreApplication::processEvents();
    QMimeData mime;
    mime.setText("ordinary child drag");
    QDragEnterEvent ordinary(QPointF(child->rect().center()), Qt::CopyAction, &mime,
                             Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(child, &ordinary);
    QVERIFY(ordinary.isAccepted());
    QCOMPARE(child->received, 1);
    QDragLeaveEvent leave;
    QApplication::sendEvent(child, &leave);

    mime.setData(MyAdsTabMimeType, "invalid json");
    QDragEnterEvent invalid(QPointF(child->rect().center()), Qt::MoveAction, &mime,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(child, &invalid);
    QVERIFY(!invalid.isAccepted());
    QCOMPARE(child->received, 1);
    QVERIFY(workspace.findPaneByContentId("keep"));
    QCOMPARE(otherWorkspace.rootPane()->contentCount(), 0);
}

QTEST_MAIN(MyAdsTest)
#include "MyAdsTest.moc"

