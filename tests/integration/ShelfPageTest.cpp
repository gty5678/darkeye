#include "darkeye_ui/theme/ThemeService.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"
#include "database/repositories/PersonRepository.h"
#include "ui/components/DvdShelfView.h"
#include "ui/components/FanartStripWidget.h"
#include "ui/components/WorkTagSelector.h"
#include "ui/pages/ShelfPage.h"
#include "ui/pages/ForceViewSettingsPanel.h"
#include "darkeye_ui/components/StateToggleButton.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include "graph/GraphManager.h"
#include "graph_view/GraphViewWidget.h"
#include "graph_view/ForceViewRhiWidget.h"
#include <QTextCursor>
#include <QSqlQuery>
#include "dvd_scene/DvdSceneWidget.h"
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class ShelfPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void initialCameraFramesShelf_data();
    void initialCameraFramesShelf();
    void filtersAndLoadsVirtualizedShelf();
    void nativeRenderingAndActions();
    void rightDragRotatesWithoutContextMenu();
    void relationGraphWaitsUntilLidNearlyCloses();
    void existingShelfVisualProbe();
    void scrollPerformanceProbe();
    void filtersByPrivateScope();
    void heartTogglesPersistedFavorite_data();
    void heartTogglesPersistedFavorite();
    void deletingExpandedWorkResetsScene_data();
    void deletingExpandedWorkResetsScene();
    void routeJumpReplacesAnAlreadyExpandedWork();
    void routeJumpClearsFiltersAndKeepsWorkExpanded_data();
    void routeJumpClearsFiltersAndKeepsWorkExpanded();
};

void ShelfPageTest::rightDragRotatesWithoutContextMenu()
{
    darkeye::DvdShelfView view({}, {}, nullptr, {}, {});
    darkeye::WorkSummary work;
    work.id=1;
    view.setWorks({work});
    QVERIFY(view.openWork(1));
    auto *scene=view.findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene);
    auto *menu=view.findChild<QMenu *>("ShelfContextMenu");
    QVERIFY(menu);
    // Cocoa can request a context menu immediately on right-button press.
    // It must not steal the gesture before the user has a chance to rotate.
    auto *canvas=scene->renderWidget();
    QSignalSpy menuOpened(menu,&QMenu::aboutToShow);
    const QPoint press(100,100);
    QTest::mousePress(canvas,Qt::RightButton,Qt::NoModifier,press);
    QContextMenuEvent mouseContext(QContextMenuEvent::Mouse,press,canvas->mapToGlobal(press));
    QTimer::singleShot(0,menu,&QMenu::hide); // Keep a regression from blocking the test.
    QApplication::sendEvent(canvas,&mouseContext);
    QCOMPARE(menuOpened.count(),0);
    const auto beforeRotation=canvas->camera().yaw;
    const QPoint dragged=press+QPoint(QApplication::startDragDistance()+30,0);
    QMouseEvent move(QEvent::MouseMove,dragged,canvas->mapToGlobal(dragged),
                     Qt::NoButton,Qt::RightButton,Qt::NoModifier);
    QApplication::sendEvent(canvas,&move);
    QTest::mouseRelease(canvas,Qt::RightButton,Qt::NoModifier,dragged);
    QApplication::processEvents();
    QVERIFY(canvas->camera().yaw!=beforeRotation);
    QCOMPARE(menuOpened.count(),0);
    // A stationary click still opens exactly one menu after release.
    QTest::mousePress(canvas,Qt::RightButton,Qt::NoModifier,press);
    QApplication::sendEvent(canvas,&mouseContext);
    QTest::mouseRelease(canvas,Qt::RightButton,Qt::NoModifier,press);
    QTimer::singleShot(0,menu,&QMenu::hide);
    QApplication::processEvents();
    QCOMPARE(menuOpened.count(),1);
}

void ShelfPageTest::initialCameraFramesShelf_data()
{
    QTest::addColumn<int>("workCount");
    QTest::addColumn<int>("viewportWidth");
    QTest::newRow("empty") << 0 << 1200;
    QTest::newRow("single") << 1 << 1200;
    QTest::newRow("short-shelf") << 8 << 1200;
    QTest::newRow("fits-wide-screen") << 30 << 1200;
    QTest::newRow("fills-narrow-screen") << 30 << 600;
    QTest::newRow("virtualized-shelf") << 100 << 1200;
}

void ShelfPageTest::initialCameraFramesShelf()
{
    QFETCH(int, workCount);
    QFETCH(int, viewportWidth);
    darkeye::DvdShelfView view({}, {}, nullptr, {}, {});
    for (const auto *widget : view.findChildren<QWidget *>())
        QVERIFY(!widget->inherits("QQuickWidget"));
    auto *scene=view.findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene != nullptr);
    view.resize(viewportWidth, 600);
    view.show();
    QTest::qWait(100);
    QList<darkeye::WorkSummary> works;
    for (int index = 0; index < workCount; ++index)
    {
        darkeye::WorkSummary work;
        work.id = index + 1;
        works.append(work);
    }
    view.setWorks(works);
    QTest::qWait(100);
    QCOMPARE(view.cameraTargetX(), view.cameraX());
    QCOMPARE(scene->itemCount(),workCount);
    QVERIFY(scene->visibleItemCount()<=60);
    if (workCount == 0)
    {
        QCOMPARE(view.cameraX(), 0.0);
        return;
    }
    auto projectX = [scene](qreal x) {
        const auto p=scene->renderWidget()->camera().project(QVector3D(float(x),0.1f,0),scene->renderWidget()->size());
        return p?p->x():qQNaN();
    };
    const qreal left = projectX(-0.0067071);
    const qreal right = projectX((workCount - 1) * 0.0145 + 0.0067071);
    const qreal actualWidth = scene->renderWidget()->width();
    if (right <= actualWidth + 0.1)
        QVERIFY(qAbs(left + right - actualWidth) < 0.2);
    else
        QVERIFY(qAbs(left) < 0.2);

    // A wider first layout must reframe the collection while it is still
    // untouched. Explicit scrolling then owns the camera across resizes.
    view.resize(2400, 600);
    QTest::qWait(100);
    const qreal wideLeft = projectX(-0.0067071);
    const qreal wideRight = projectX((workCount - 1) * 0.0145 + 0.0067071);
    QVERIFY(qAbs(wideLeft + wideRight - scene->renderWidget()->width()) < 0.2 || qAbs(wideLeft) < 0.2);
    view.set_camera_x(view.cameraX());
    const qreal scrolledCamera = view.cameraX();
    view.resize(600, 600);
    QCOMPARE(view.cameraX(), scrolledCamera);
    view.setWorks(works.mid(0, 1));
    QCOMPARE(view.cameraX(), 0.0);
}

void ShelfPageTest::filtersAndLoadsVirtualizedShelf()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(temporaryDirectory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work first;
    first.serialNumber = QStringLiteral("SHELF-001");
    first.chineseTitle = QStringLiteral("书架目标");
    first.notes = QStringLiteral("保留");
    QVERIFY(repository.insertComplete(first, {}, {}, {}, &error).has_value());
    darkeye::Work second;
    second.serialNumber = QStringLiteral("SHELF-002");
    second.chineseTitle = QStringLiteral("另一作品");
    QVERIFY(repository.insertComplete(second, {}, {}, {}, &error).has_value());

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::ShelfPage page(connection.database(), {}, themes);
    page.initialize();
    page.resize(1280,800);
    page.show();
    page.findChild<darkeye::WorkTagSelector *>()->clearSelection();
    page.refresh();
    auto *count = page.findChild<QLabel *>(QStringLiteral("ShelfCountLabel"));
    auto *view = page.findChild<darkeye::DvdShelfView *>(QStringLiteral("DvdShelfView"));
    auto *serial = page.findChild<QLineEdit *>();
    QVERIFY(count != nullptr);
    QVERIFY(view != nullptr);
    QVERIFY(serial != nullptr);
    for (const auto *widget : view->findChildren<QWidget *>())
        QVERIFY(!widget->inherits("QQuickWidget"));
    auto *scene=view->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene != nullptr);
    QCOMPARE(scene->itemCount(),2);
    QCOMPARE(count->text(), QStringLiteral("过滤总数:2"));

    serial->setText(QStringLiteral("002"));
    QTRY_COMPARE(count->text(), QStringLiteral("过滤总数:1"));
}

void ShelfPageTest::nativeRenderingAndActions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage cover(4096,2560,QImage::Format_RGBA8888);
    cover.fill(QColor("#c45b35"));
    {
        QPainter painter(&cover);
        painter.setPen(Qt::white);
        painter.setFont(QFont(QStringLiteral("Arial"),80,QFont::Bold));
        painter.drawText(QRect(2048,0,2048,2560),Qt::AlignCenter,QStringLiteral("HIGH RESOLUTION\nCOVER 2048+"));
        painter.fillRect(QRect(1952,0,200,2560),QColor("#163953"));
        const QString spine=QStringLiteral("SPINE 1024");
        for(int i=0;i<spine.size();++i)
            painter.drawText(QRect(1952,120+i*220,200,220),Qt::AlignCenter,spine.mid(i,1));
    }
    const auto sourceCover=qEnvironmentVariable("DARKEYE_SHELF_TEST_COVER");
    if(!sourceCover.isEmpty()) QVERIFY(cover.load(sourceCover));
    QVERIFY(cover.save(directory.filePath("cover.jpg"),"JPG",95));
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath("public.db"),false,&error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection,darkeye::DatabaseKind::Public,&error));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work work;
    work.serialNumber=QStringLiteral("NATIVE-001");
    work.chineseTitle=QStringLiteral("原生书架渲染验证");
    work.chineseStory=QStringLiteral("这段简介应显示在展开的盒盖内，跟随 DVD 位置，而不是弹出右侧详情栏。");
    work.releaseDate=QStringLiteral("2026-10-23");
    work.director=QStringLiteral("测试导演");
    work.imageUrl=QStringLiteral("cover.jpg");
    work.fanartJson=darkeye::FanartStripWidget::toJson({
        {"https://example.invalid/1.jpg",{},{}},{"https://example.invalid/2.jpg",{},{}},
        {"https://example.invalid/3.jpg",{},{}}});
    darkeye::PersonRepository people(connection.database());
    const auto actress=people.create(darkeye::PersonKind::Actress,QStringLiteral("测试女优"),{},&error);
    const auto actor=people.create(darkeye::PersonKind::Actor,QStringLiteral("测试男优"),{},&error);
    QVERIFY(actress); QVERIFY(actor);
    QSqlQuery tag(connection.database());
    QVERIFY(tag.exec(QStringLiteral("INSERT INTO tag(tag_name) VALUES('展示标签')")));
    const auto tagId=tag.lastInsertId().toLongLong();
    const auto id=repository.insertComplete(work,{*actress},{*actor},{tagId},&error);
    QVERIFY(id.has_value());
    auto *application=qobject_cast<QApplication *>(QCoreApplication::instance());
    darkeye::ThemeService themes(*application);
    darkeye::graph::GraphManager graph(connection.database());
    QVERIFY(graph.initialize(&error));
    darkeye::ShelfPage page(connection.database(),{},themes,&graph,directory.path(),{});
    page.resize(1280,800);
    page.initialize(); page.show();
    auto *view=page.findChild<darkeye::DvdShelfView *>();
    QVERIFY(view);
    for (const auto *widget : view->findChildren<QWidget *>())
        QVERIFY(!widget->inherits("QQuickWidget"));
    auto *scene=view->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene);
    // This test drives the scene API and overlay controls. Keep the physical
    // desktop pointer from injecting canvas selections during those checks.
    scene->renderWidget()->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *info=view->findChild<QScrollArea *>("ShelfCoverInfo");
    QVERIFY(info);
    const int shelfWidth=scene->width();
    QVERIFY(page.showWork(*id));
    QVERIFY(info->isHidden());
    QTRY_VERIFY(scene->isExpanded());
    QTRY_COMPARE(scene->pendingCovers(),0);
    QTest::qWait(550);
    QTRY_VERIFY(scene->isFullyExpanded());
    QTRY_VERIFY(info->isVisible());
    const auto discPoseMatches=[&](float angle) {
        for(const auto &part:scene->renderWidget()->scene()->renderItems()) {
            const auto *node=scene->renderWidget()->scene()->node(part.node);
            const auto *parent=node?scene->renderWidget()->scene()->node(node->parent):nullptr;
            if(parent && parent->name=="disc")
                return std::abs(QQuaternion::dotProduct(parent->transform.rotation,
                    QQuaternion::fromAxisAndAngle(1,0,0,angle)))>0.99999f;
        }
        return false;
    };
    QTRY_VERIFY(discPoseMatches(30));
    auto *coverActions=view->findChild<QWidget *>("ShelfCoverActions");
    QVERIFY(coverActions && coverActions->isVisible());
    const auto spineCenter=scene->selectedSpineCenter();
    QVERIFY(spineCenter);
    QVERIFY(QLineF(coverActions->geometry().center(),*spineCenter).length()<2);
    const auto infoText=[&] {
        QString result;
        for(auto *label:info->findChildren<QLabel *>()) result+=label->text();
        for(auto *button:info->findChildren<QToolButton *>()) result+=button->text();
        return result;
    };
    const auto text=infoText();
    QVERIFY(text.contains(work.chineseTitle));
    QVERIFY(text.contains(work.chineseStory));
    QVERIFY(text.contains(work.serialNumber));
    QVERIFY(text.contains(QStringLiteral("测试女优")));
    QVERIFY(text.contains(QStringLiteral("测试男优")));
    QVERIFY(text.contains(QStringLiteral("展示标签")));
    QVERIFY(info->findChild<QToolButton *>("actress"+QString::number(*actress))->styleSheet().contains("background:#ffffff"));
    auto *fanart=view->findChild<QWidget *>("ShelfFanartOverlay");
    QVERIFY(fanart && fanart->isVisible());
    const auto front=scene->selectedFrontRect();
    QVERIFY(front);
    QVERIFY(front->adjusted(-1,-1,1,1).contains(info->geometry()));
    // Short native desktops may need the available cover height and scrolling.
    if (info->verticalScrollBar()->maximum()==0)
        QVERIFY(info->height()<front->height()*0.8);
    const auto clickLink=[&](const QString &label) {
        for(auto *button:info->findChildren<QToolButton *>()) {
            if(button->text()!=label) continue;
            if(!button->isVisible()) return false;
            QTest::mouseClick(button,Qt::LeftButton); return true;
        }
        return false;
    };
    QSignalSpy actressLink(&page,&darkeye::ShelfPage::actressRequested);
    QSignalSpy actorLink(&page,&darkeye::ShelfPage::actorRequested);
    QSignalSpy tagLink(&page,&darkeye::ShelfPage::tagRequested);
    QVERIFY(clickLink(QStringLiteral("测试女优")));
    QCOMPARE(actressLink.count(),1); QCOMPARE(actressLink.first().first().toLongLong(),*actress);
    QVERIFY(clickLink(QStringLiteral("测试男优")));
    QCOMPARE(actorLink.count(),1); QCOMPARE(actorLink.first().first().toLongLong(),*actor);
    QVERIFY(clickLink(QStringLiteral("展示标签")));
    QCOMPARE(tagLink.count(),1); QCOMPARE(tagLink.first().first().toLongLong(),tagId);
    const auto image=scene->renderWidget()->grabFramebuffer();
    QVERIFY(!image.isNull());
    QVERIFY(scene->renderWidget()->lastError().isEmpty());
    QVERIFY(scene->renderWidget()->statistics().geometryCount>0);
    QCOMPARE(scene->renderWidget()->sampleCount(),4);
    const auto lighting=scene->renderWidget()->lighting();
    QVERIFY(lighting.imageBasedLighting && lighting.ambientOcclusion && lighting.shadows && lighting.toneMapping);
    QCOMPARE(lighting.environmentRotation,155.0f);
    QCOMPARE(lighting.exposure,0.95f);
    QCOMPARE(lighting.environmentIntensity,1.0f);
    QVERIFY(!lighting.keyLightCameraRelative);
    QVERIFY(lighting.keyLightDirection.x()<0 && lighting.keyLightDirection.y()>0 && lighting.keyLightDirection.z()>0);
    bool coverLoaded=false;
    int whiteShells=0;
    for(const auto &item:scene->renderWidget()->scene()->renderItems()) {
        if(item.material && item.material->name=="trans") {
            QCOMPARE(item.material->baseColor,QVector4D(1,1,1,1));
            QCOMPARE(item.material->transmission,0.0f);
            QCOMPARE(item.material->alphaMode,darkeye::render3d::Material::AlphaMode::Opaque);
            ++whiteShells;
        }
        if(item.material && item.material->name=="cover" && item.material->baseColorTexture) {
            const auto &texture=*item.material->baseColorTexture;
            coverLoaded |= texture.source==directory.filePath("cover.jpg");
            QVERIFY(texture.image.width()>=std::min(2048,cover.width()));
        }
    }
    QVERIFY(coverLoaded);
    QCOMPARE(whiteShells,3);
    const auto checkTransparentPixel=[&](const QPoint &point) {
        const auto base=scene->renderWidget()->grabFramebuffer();
        const auto composed=view->grab().toImage();
        const auto local=scene->renderWidget()->mapFrom(view,point);
        const auto underlying=base.pixelColor(qRound(double(local.x())*base.width()/scene->renderWidget()->width()),qRound(double(local.y())*base.height()/scene->renderWidget()->height()));
        const auto overlay=composed.pixelColor(qRound(double(point.x())*composed.width()/view->width()),qRound(double(point.y())*composed.height()/view->height()));
        return std::abs(underlying.red()-overlay.red())<6 && std::abs(underlying.green()-overlay.green())<6 && std::abs(underlying.blue()-overlay.blue())<6;
    };
    QVERIFY(checkTransparentPixel(info->pos()+QPoint(4,4)));
    QVERIFY(checkTransparentPixel(coverActions->pos()+QPoint(2,2)));
    auto *fanartScroll=fanart->findChild<QScrollArea *>();
    QVERIFY(fanartScroll);
    QVERIFY(!fanartScroll->viewport()->autoFillBackground());
    QVERIFY(checkTransparentPixel(fanartScroll->viewport()->mapTo(view,fanartScroll->viewport()->rect().topRight()+QPoint(-4,4))));
    bool discCoverLoaded=false;
    for(const auto &item:scene->renderWidget()->scene()->renderItems())
        if(item.material && item.material->name=="disc-cover") {
            QVERIFY(item.material->baseColorTexture);
            QCOMPARE(item.material->baseColorTexture->source,directory.filePath("cover.jpg"));
            discCoverLoaded=true;
        }
    QVERIFY(discCoverLoaded);
    if(sourceCover.isEmpty()) {
        const auto center=scene->selectedDiscCenter();
        QVERIFY(center);
        const auto sample=*center+QPointF(70,-70);
        const auto color=image.pixelColor(qRound(sample.x()*image.width()/scene->renderWidget()->width()),
            qRound(sample.y()*image.height()/scene->renderWidget()->height()));
        QVERIFY2(color.red()>color.green()+20 && color.green()>color.blue()+10,"The visible disc face must contain the orange front artwork instead of a white material");
    }
    QCOMPARE(scene->width(),shelfWidth);
    QVERIFY(view->findChild<QScrollArea *>("ShelfWorkPanel")==nullptr);
    const auto camera=scene->renderWidget()->camera();
    const auto viewport=scene->renderWidget()->size();
    for(const auto &item:scene->renderWidget()->scene()->renderItems()) {
        for(const auto &vertex:item.mesh->vertices) {
            const auto point=camera.project(item.world.map(QVector3D(vertex.position[0],vertex.position[1],vertex.position[2])),viewport);
            QVERIFY(point.has_value());
            QVERIFY(point->x()>=0 && point->x()<=viewport.width());
            QVERIFY(point->y()>=0 && point->y()<=viewport.height());
        }
    }
    QSignalSpy edit(&page,&darkeye::ShelfPage::editRequested);
    auto *editButton=view->findChild<QAction *>("ShelfEdit");
    QVERIFY(editButton);
    auto *menu=view->findChild<QMenu *>("ShelfContextMenu");
    QVERIFY(menu);
    QTimer::singleShot(0,menu,[menu,editButton] { editButton->trigger(); menu->hide(); });
    QContextMenuEvent context(QContextMenuEvent::Keyboard,QPoint(20,20),scene->renderWidget()->mapToGlobal(QPoint(20,20)));
    QApplication::sendEvent(scene->renderWidget(),&context);
    QCOMPARE(edit.count(),1); QCOMPARE(edit.first().first().toLongLong(),*id);
    const auto artifacts=qEnvironmentVariable("DARKEYE_SHELF_ARTIFACTS");
    if(!artifacts.isEmpty()) {
        QDir().mkpath(artifacts);
        QVERIFY(page.grab().save(QDir(artifacts).filePath("shelf-native-open.png")));
    }
    auto longWork=repository.findById(*id,&error);
    QVERIFY(longWork);
    longWork->videoUrl="local-video.mp4";
    QVERIFY(repository.updateDetails(*longWork,&error));
    view->refresh_expanded_work_meta(0);
    QTRY_VERIFY(discPoseMatches(0));
    QVERIFY(scene->isExpanded());
    QCOMPARE(scene->selectedId(),QString::number(*id));
    longWork->videoUrl.clear();
    QVERIFY(repository.updateDetails(*longWork,&error));
    view->refresh_expanded_work_meta(0);
    QTRY_VERIFY(discPoseMatches(30));
    longWork->chineseTitle=QStringLiteral("完整作品信息的双行标题，用于验证原来的盒内信息排版与可用空间。").repeated(3);
    longWork->chineseStory=QStringLiteral("很长的简介应在六行内省略，保留演员和作品标签的位置。").repeated(100);
    QSqlQuery metadata(connection.database());
    QVERIFY(metadata.exec("INSERT INTO maker(cn_name) VALUES('测试制作商')"));
    longWork->makerId=metadata.lastInsertId().toLongLong();
    QVERIFY(metadata.exec("INSERT INTO label(cn_name) VALUES('测试厂牌')"));
    longWork->labelId=metadata.lastInsertId().toLongLong();
    QVERIFY(metadata.exec("INSERT INTO series(cn_name) VALUES('测试系列')"));
    longWork->seriesId=metadata.lastInsertId().toLongLong();
    for(int i=0;i<8;++i) {
        metadata.prepare("INSERT INTO tag(tag_name) VALUES(?)");
        metadata.addBindValue(QStringLiteral("完整标签%1").arg(i)); QVERIFY(metadata.exec());
        const auto newTag=metadata.lastInsertId();
        metadata.prepare("INSERT INTO work_tag_relation(work_id,tag_id) VALUES(?,?)");
        metadata.addBindValue(*id); metadata.addBindValue(newTag); QVERIFY(metadata.exec());
    }
    QVERIFY(repository.updateDetails(*longWork,&error));
    view->refresh_expanded_work_meta(0);
    QVERIFY(infoText().size()<longWork->chineseStory.size());
    QTRY_COMPARE(info->verticalScrollBar()->maximum(),qMax(0,info->widget()->height()-info->viewport()->height()));
    for(auto *label:info->findChildren<QLabel *>())
        if(label->wordWrap()) QVERIFY(label->height()>=label->heightForWidth(label->width()));
    QVERIFY(infoText().contains(QStringLiteral("测试制作商")));
    QVERIFY(infoText().contains(QStringLiteral("完整标签7")));
    if(!artifacts.isEmpty())
        QVERIFY(page.grab().save(QDir(artifacts).filePath("shelf-native-full-info.png")));
    QVERIFY(clickLink(QStringLiteral("展示标签")));
    QCOMPARE(tagLink.count(),2);
    page.resize(1000,800);
    QTest::qWait(100);
    const auto resizedFront=scene->selectedFrontRect();
    QVERIFY(resizedFront);
    // The transparent layout margins can extend past the paper, just as in
    // QML; all visible text/chips must remain inside it and the page viewport.
    QVERIFY(resizedFront->adjusted(-1,-1,1,1).contains(info->geometry().adjusted(12,12,-12,-12)));
    QVERIFY(view->rect().contains(info->geometry()));
    QCOMPARE(info->verticalScrollBar()->maximum(),qMax(0,info->widget()->height()-info->viewport()->height()));
    QVERIFY(view->findChild<QScrollArea *>("ShelfWorkPanel")==nullptr);
    page.resize(1280,800);
    QTest::qWait(100);
    auto *close=view->findChild<QAction *>("ShelfToggleOpen");
    QVERIFY(close); close->trigger();
    QVERIFY(!scene->isExpanded());
    QVERIFY(info->isHidden());
    QVERIFY(coverActions->isHidden());
    QTest::qWait(550);
    if(sourceCover.isEmpty()) {
        // Read back the final closed frame, after the animation and pending
        // resize/render events have settled on slower high-DPI configurations.
        const auto closedArtworkVisible=[&] {
            const auto closed=scene->renderWidget()->grabFramebuffer();
            const auto bounds=scene->selectedFrontRect();
            if(!bounds || closed.isNull()) return false;
            const auto sample=bounds->topLeft()+QPointF(bounds->width()*0.25,bounds->height()*0.25);
            const auto color=closed.pixelColor(qRound(sample.x()*closed.width()/scene->renderWidget()->width()),
                qRound(sample.y()*closed.height()/scene->renderWidget()->height()));
            return color.red()>color.green()+20 && color.green()>color.blue()+10;
        };
        QTRY_VERIFY_WITH_TIMEOUT(closedArtworkVisible(),3000);
    }
    if(!artifacts.isEmpty())
        QVERIFY(page.grab().save(QDir(artifacts).filePath("shelf-native-closed.png")));
    view->findChild<QAction *>("ShelfReturn")->trigger();
    QTRY_COMPARE(view->currentWorkId(),qint64(0));
    QVERIFY(view->findChild<QScrollArea *>("ShelfWorkPanel")==nullptr);
    QVERIFY(!scene->isExpanded());
    QTRY_COMPARE(scene->pendingCovers(),0);
    for(const auto &item:scene->renderWidget()->scene()->renderItems()) {
        if(item.material && item.material->name=="cover" && item.material->baseColorTexture) {
            QVERIFY(item.material->baseColorTexture->image.width()>=std::min(1024,cover.width()));
            QCOMPARE(item.material->baseColorTexture->maxAnisotropy,8);
        }
    }
    if(!artifacts.isEmpty())
        QVERIFY(page.grab().save(QDir(artifacts).filePath("shelf-native-spines.png")));
    auto *relation=view->findChild<QWidget *>("ShelfRelationGraphOverlay");
    QVERIFY(relation);
    auto *relationGraph=relation->findChild<darkeye::graph_view::GraphViewWidget *>();
    QVERIFY(relationGraph);
    QVERIFY(scene->focusItem(QString::number(*id)));
    QCOMPARE(scene->state(),darkeye::dvd_scene::DvdInteractionController::State::Focusing);
    QVERIFY(relation->isHidden());
    // Preparation must finish its CPU snapshot before the animation ends,
    // even though the QRhi widget itself has never been shown.
    QVERIFY(relationGraph->view()->getNodeIds().contains(QStringLiteral("w%1").arg(*id)));
    QTRY_COMPARE(scene->state(),darkeye::dvd_scene::DvdInteractionController::State::Focused);
    QVERIFY(relation->isVisible());
    auto *graphSettingsButton = relation->findChild<darkeye::StateToggleButton *>("ShelfRelationGraphSettingsButton");
    auto *graphSettingsPanel = relation->findChild<darkeye::ForceViewSettingsPanel *>();
    QVERIFY(graphSettingsButton && graphSettingsPanel);
    QVERIFY(graphSettingsButton->isVisible());
    QVERIFY(graphSettingsPanel->isHidden());
    QTest::mouseClick(graphSettingsButton, Qt::LeftButton);
    QVERIFY(graphSettingsPanel->isVisible());
    QVERIFY(relation->rect().contains(graphSettingsPanel->geometry()));
    QVERIFY(relation->rect().contains(graphSettingsButton->geometry()));
    QTest::mouseClick(graphSettingsButton, Qt::LeftButton);
    QVERIFY(graphSettingsPanel->isHidden());
    QTest::qWait(100);
    const auto box=scene->selectedCaseRect();
    QVERIFY(box);
    const auto anchor=scene->selectedInfoAnchor();
    QVERIFY(anchor);
    QVERIFY(anchor->x()<scene->selectedFrontRect()->center().x()-100);
    QCOMPARE(relation->x(),std::clamp(qRound(anchor->x()-relation->width()*0.5-scene->height()*0.42),0,view->width()-relation->width()));
    const auto graphImage=relation->grab().toImage();
    const auto graphBackground=graphImage.pixelColor(qRound(12*graphImage.devicePixelRatio()),qRound(12*graphImage.devicePixelRatio()));
    QVERIFY(graphBackground.red()>245 && graphBackground.green()>245 && graphBackground.blue()>245);
    QVERIFY(view->rect().contains(relation->geometry()));
    if(!artifacts.isEmpty())
        QVERIFY(page.grab().save(QDir(artifacts).filePath("shelf-native-relation.png")));
    page.resize(1000,800);
    QTest::qWait(100);
    QVERIFY(view->rect().contains(relation->geometry()));
    scene->toggleOpen();
    QVERIFY(relation->isHidden());

}

void ShelfPageTest::relationGraphWaitsUntilLidNearlyCloses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath("public.db"),false,&error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection,darkeye::DatabaseKind::Public,&error));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work work;
    work.serialNumber=QStringLiteral("CLOSE-TIMING-001");
    const auto id=repository.insertComplete(work,{},{},{},&error);
    QVERIFY(id);
    darkeye::graph::GraphManager graph(connection.database());
    QVERIFY(graph.initialize(&error));
    darkeye::DvdShelfView view(connection.database(),{},&graph,{},{ });
    QList<darkeye::WorkSummary> works;
    darkeye::WorkSummary summary; summary.id=*id; summary.serialNumber=work.serialNumber;
    works.append(summary);
    view.resize(1000,650);
    view.setWorks(works);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *scene=view.findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    auto *relation=view.findChild<QWidget *>("ShelfRelationGraphOverlay");
    QVERIFY(scene && relation);
    scene->renderWidget()->setAttribute(Qt::WA_TransparentForMouseEvents);
    QVERIFY(scene->focusItem(QString::number(*id)));
    QTRY_VERIFY(relation->isVisible());
    scene->toggleOpen();
    QVERIFY(relation->isHidden());
    QTRY_VERIFY(scene->isFullyExpanded());
    scene->toggleOpen();
    QVERIFY(!scene->isExpanded());
    QVERIFY(relation->isHidden()); // closing intent must not reveal the graph
    QTest::qWait(180);
    QVERIFY(relation->isHidden());
    QTRY_VERIFY_WITH_TIMEOUT(relation->isVisible(),1500);
    QVERIFY(scene->isNearlyClosed());
    // Reopening during a close must not leave a delayed graph reveal behind.
    scene->toggleOpen();
    QTRY_VERIFY(scene->isFullyExpanded());
    scene->toggleOpen();
    QTest::qWait(100);
    scene->toggleOpen();
    QTRY_VERIFY(scene->isFullyExpanded());
    QVERIFY(relation->isHidden());
}

void ShelfPageTest::scrollPerformanceProbe()
{
    const auto output=qEnvironmentVariable("DARKEYE_SHELF_PERF_OUTPUT");
    if(output.isEmpty()) QSKIP("Optional production-material shelf performance probe");
    darkeye::DvdShelfView view({}, {}, nullptr, {}, {});
    auto *scene=view.findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene);
    auto *canvas=scene->renderWidget();
    canvas->setAttribute(Qt::WA_TransparentForMouseEvents);
    view.resize(1280,800);
    QTemporaryDir covers;
    const bool uniqueCovers=qEnvironmentVariableIsSet("DARKEYE_SHELF_PERF_UNIQUE_COVERS");
    if(uniqueCovers) {
        QVERIFY(covers.isValid());
        QImage image(1024,2048,QImage::Format_RGB32);
        for(int i=0;i<96;++i) {
            image.fill(QColor::fromHsv((i*37)%360,150,220));
            QVERIFY(image.save(covers.filePath(QStringLiteral("cover-%1.jpg").arg(i)),"JPG",95));
        }
    }
    QList<darkeye::WorkSummary> works;
    for(int i=0;i<1000;++i) {
        darkeye::WorkSummary work; work.id=i+1;
        if(uniqueCovers) work.imageUrl=covers.filePath(QStringLiteral("cover-%1.jpg").arg(i%96));
        works.append(work);
    }
    view.setWorks(works);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(600);
    const auto fullLighting=canvas->lighting();
    QJsonArray measurements;
    for(const auto &name:QStringList{"full","no-ibl","no-ao","no-shadows","single-sample","half-resolution"}) {
        auto lighting=fullLighting;
        lighting.imageBasedLighting=name!="no-ibl" && fullLighting.imageBasedLighting;
        lighting.ambientOcclusion=name!="no-ao" && fullLighting.ambientOcclusion;
        lighting.shadows=name!="no-shadows" && fullLighting.shadows;
        canvas->setLighting(lighting);
        canvas->setSampleCount(name=="single-sample"?1:4);
        canvas->setFixedColorBufferSize(name=="half-resolution"?QSize(1920,1200):QSize());
        scene->setCameraX(1.0f,true);
        QTest::qWait(400);
        QElapsedTimer clock;
        QVector<double> cpu,intervals;
        qint64 previous=0;
        const auto connection=connect(canvas,&QRhiWidget::frameSubmitted,&view,[&] {
            const auto time=clock.nsecsElapsed();
            if(previous) intervals.append((time-previous)/1e6);
            previous=time;
            cpu.append(canvas->statistics().cpuFrameMs);
        });
        QTimer driver;
        driver.setTimerType(Qt::PreciseTimer);
        driver.setInterval(16);
        connect(&driver,&QTimer::timeout,&view,[&] { scene->setCameraX(1.0f+float(clock.elapsed())*0.0001f); });
        const auto firstFrame=canvas->statistics().submittedFrames;
        const auto firstUploads=canvas->statistics().textureUploads;
        const auto firstBytes=canvas->statistics().textureUploadBytes;
        const auto firstBindings=canvas->statistics().materialBindingBuilds;
        clock.start(); driver.start();
        QTest::qWait(2200);
        driver.stop(); disconnect(connection);
        const double seconds=clock.nsecsElapsed()/1e9;
        std::sort(cpu.begin(),cpu.end()); std::sort(intervals.begin(),intervals.end());
        const auto percentile=[](const QVector<double> &values) {
            return values.isEmpty()?0:values[std::min(values.size()-1,qsizetype(values.size()*0.95))];
        };
        const auto s=canvas->statistics();
        measurements.append(QJsonObject{{"variant",name},{"submissionFps",(s.submittedFrames-firstFrame)/seconds},
            {"p95CpuMs",percentile(cpu)},{"p95FrameIntervalMs",percentile(intervals)},
            {"drawCalls",s.drawCalls},{"visiblePrimitives",s.visibleItems},{"transmissionPasses",s.transmissionPasses},
            {"width",s.pixelSize.width()},{"height",s.pixelSize.height()},{"samples",s.samples},
            {"textureUploads",double(s.textureUploads-firstUploads)},
            {"uploadMiB",(s.textureUploadBytes-firstBytes)/1048576.0},
            {"materialBindingBuilds",double(s.materialBindingBuilds-firstBindings)},
            {"textureMiB",s.textureBytes/1048576.0}});
    }
    QFile file(output); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"measurements",measurements},
        {"uniqueCovers",uniqueCovers},
        {"note","Production shelf model/materials/HDR, 1000 items, 60 resident. Submission FPS, not presentation FPS."}}).toJson());
}

void ShelfPageTest::existingShelfVisualProbe()
{
    const auto database=qEnvironmentVariable("DARKEYE_SHELF_TEST_DATABASE");
    if(database.isEmpty()) QSKIP("Optional read-only check of an existing shelf");
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY2(connection.open(database,true,&error),qPrintable(error));
    darkeye::WorkSearch search; search.limit=0;
    const auto works=darkeye::WorkRepository(connection.database()).search(search,&error);
    QVERIFY2(error.isEmpty(),qPrintable(error)); QVERIFY(!works.isEmpty());
    darkeye::graph::GraphManager graph(connection.database());
    QVERIFY2(graph.initialize(&error),qPrintable(error));
    darkeye::DvdShelfView view(connection.database(),{},&graph,
        QFileInfo(database).dir().filePath("workcovers"),QFileInfo(database).dir().filePath("fanart"));
    view.resize(1032,730); view.show(); view.setWorks(works);
    auto *scene=view.findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QCOMPARE(scene->itemCount(),works.size());
    QVERIFY(view.openWork(works.first().id));
    QTRY_VERIFY(scene->isFullyExpanded());
    QTRY_COMPARE_WITH_TIMEOUT(scene->pendingCovers(),0,30000);
    auto *info=view.findChild<QScrollArea *>("ShelfCoverInfo");
    QVERIFY(info->isVisible());
    bool loaded=false;
    for(const auto &item:scene->renderWidget()->scene()->renderItems())
        if(item.material && item.material->baseColorTexture)
            loaded|=item.material->baseColorTexture->source.endsWith(works.first().imageUrl);
    QVERIFY(loaded);
    const auto artifacts=qEnvironmentVariable("DARKEYE_SHELF_ARTIFACTS");
    if(!artifacts.isEmpty()) QVERIFY(view.grab().save(QDir(artifacts).filePath("existing-shelf-open.png")));
    scene->toggleOpen(); QTest::qWait(550);
    if(!artifacts.isEmpty()) QVERIFY(view.grab().save(QDir(artifacts).filePath("existing-shelf-closed.png")));
}

void ShelfPageTest::filtersByPrivateScope()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString error;
    QVERIFY(publicConnection.open(temporaryDirectory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(privateConnection.open(temporaryDirectory.filePath(QStringLiteral("private.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(publicConnection, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateConnection, darkeye::DatabaseKind::Private, &error));
    darkeye::WorkRepository works(publicConnection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("FAVORITE-001");
    const auto id = works.insertComplete(work, {}, {}, {}, &error);
    QVERIFY(id.has_value());
    darkeye::PrivateRepository privateRepository(privateConnection.database());
    QVERIFY(privateRepository.addFavoriteWork(*id, work.serialNumber, &error));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::ShelfPage page(publicConnection.database(), privateConnection.database(), themes);
    page.initialize();
    page.resize(1280,800);
    page.show();
    auto *scope = page.findChild<QComboBox *>(QStringLiteral("ShelfScopeSelector"));
    auto *count = page.findChild<QLabel *>(QStringLiteral("ShelfCountLabel"));
    QVERIFY(scope != nullptr);
    QVERIFY(count != nullptr);
    page.findChild<darkeye::WorkTagSelector *>()->clearSelection();
    scope->setCurrentText(QStringLiteral("收藏库范围"));
    QTRY_COMPARE(count->text(), QStringLiteral("过滤总数:1"));
    scope->setCurrentText(QStringLiteral("已撸过"));
    QTRY_COMPARE(count->text(), QStringLiteral("没有查询到数据"));
}

void ShelfPageTest::heartTogglesPersistedFavorite_data()
{
    QTest::addColumn<int>("workCount");
    QTest::addColumn<bool>("initialFavorite");
    QTest::newRow("add-favorite") << 1 << false;
    QTest::newRow("remove-favorite") << 1 << true;
    QTest::newRow("virtualized-work") << 80 << false;
}

void ShelfPageTest::heartTogglesPersistedFavorite()
{
    QFETCH(int, workCount);
    QFETCH(bool, initialFavorite);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection, privateConnection;
    QString error;
    QVERIFY(publicConnection.open(temporaryDirectory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(privateConnection.open(temporaryDirectory.filePath(QStringLiteral("private.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(publicConnection, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateConnection, darkeye::DatabaseKind::Private, &error));
    darkeye::WorkRepository works(publicConnection.database());
    darkeye::PrivateRepository favorites(privateConnection.database());
    qint64 workId = 0;
    QString serial;
    for (int index = 0; index < workCount; ++index)
    {
        darkeye::Work work;
        work.serialNumber = QStringLiteral("HEART-%1").arg(index, 3, 10, QLatin1Char('0'));
        const auto id = works.insertComplete(work, {}, {}, {}, &error);
        QVERIFY(id.has_value());
        workId = *id;
        serial = work.serialNumber;
    }
    if (initialFavorite) QVERIFY(favorites.addFavoriteWork(workId, serial, &error));
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::ShelfPage page(publicConnection.database(), privateConnection.database(), themes);
    page.initialize();
    page.resize(1280,800);
    page.show();
    auto *view = page.findChild<darkeye::DvdShelfView *>();
    QVERIFY(view != nullptr);
    darkeye::WorkSearch search;
    search.limit = 0;
    search.order = darkeye::WorkSortOrder::SerialAscending;
    view->setWorks(works.search(search, &error));
    QVERIFY(view->openWork(workId));
    auto *scene=view->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene != nullptr);
    QTRY_VERIFY(scene->isExpanded());
    const int virtualIndex = workCount - 1;
    QCOMPARE(scene->selectedId(),QString::number(workId));
    QCOMPARE(virtualIndex, workCount - 1);
    QCOMPARE(view->expandedWorkFavorited(), initialFavorite);
    QSignalSpy stateChanged(view, &darkeye::DvdShelfView::expandedWorkFavoritedChanged);
    QSignalSpy favoriteChanged(&page, &darkeye::ShelfPage::favoriteChanged);
    auto *favoriteButton=view->findChild<QAction *>("ShelfFavorite");
    QVERIFY(favoriteButton);
    favoriteButton->trigger();
    QCOMPARE(favorites.isFavoriteWork(workId, &error), !initialFavorite);
    QCOMPARE(view->expandedWorkFavorited(), !initialFavorite);
    QCOMPARE(stateChanged.count(), 1);
    QCOMPARE(favoriteChanged.count(), 1);
    QCOMPARE(favoriteChanged.first().at(0).toLongLong(), workId);
    QCOMPARE(favoriteChanged.first().at(1).toBool(), !initialFavorite);
    QVERIFY(scene->isExpanded());
    view->on_heart_clicked(virtualIndex);
    QCOMPARE(favorites.isFavoriteWork(workId, &error), initialFavorite);
    QCOMPARE(view->expandedWorkFavorited(), initialFavorite);
    QCOMPARE(favoriteChanged.count(), 2);
    // Match Python: consult the database even when another page changed it.
    if (initialFavorite) QVERIFY(favorites.removeFavoriteWork(workId, &error));
    else QVERIFY(favorites.addFavoriteWork(workId, serial, &error));
    view->on_heart_clicked(virtualIndex);
    QCOMPARE(favorites.isFavoriteWork(workId, &error), initialFavorite);
    QCOMPARE(view->expandedWorkFavorited(), initialFavorite);
    QCOMPARE(favoriteChanged.count(), 3);
    view->refresh_expanded_favorite_state(virtualIndex);
    QCOMPARE(view->expandedWorkFavorited(), initialFavorite);
    view->on_heart_clicked(-1);
    view->on_heart_clicked(workCount);
    QCOMPARE(favoriteChanged.count(), 3);
}

void ShelfPageTest::deletingExpandedWorkResetsScene_data()
{
    QTest::addColumn<int>("workCount");
    QTest::newRow("first-work") << 3;
    QTest::newRow("scrolled-work") << 100;
    QTest::newRow("last-work") << 1;
}

void ShelfPageTest::deletingExpandedWorkResetsScene()
{
    QFETCH(int, workCount);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(temporaryDirectory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    darkeye::WorkRepository repository(connection.database());
    qint64 deletedId = 0;
    qint64 remainingId = 0;
    for (int index = 0; index < workCount; ++index)
    {
        darkeye::Work work;
        work.serialNumber = QStringLiteral("DELETE-%1").arg(index, 3, 10, QLatin1Char('0'));
        const auto id = repository.insertComplete(work, {}, {}, {}, &error);
        QVERIFY(id.has_value());
        if (index == 0) remainingId = *id;
        if (index == workCount - 1) deletedId = *id;
    }

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::ShelfPage page(connection.database(), {}, themes);
    page.initialize();
    page.resize(1280,800);
    page.show();
    auto *view = page.findChild<darkeye::DvdShelfView *>();
    auto *count = page.findChild<QLabel *>(QStringLiteral("ShelfCountLabel"));
    QVERIFY(view != nullptr);
    QVERIFY(count != nullptr);
    auto *scene=view->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene != nullptr);
    QVERIFY(page.showWork(deletedId));
    auto *sort = page.findChild<QComboBox *>(QStringLiteral("ShelfSortSelector"));
    QVERIFY(sort != nullptr);
    sort->setCurrentText(QStringLiteral("番号顺序"));
    page.refresh();
    QVERIFY(view->openWork(deletedId));
    QTRY_VERIFY(scene->isExpanded());
    if (workCount == 100) QVERIFY(view->cameraX() > 1.0);
    QSignalSpy cameraChanged(view, &darkeye::DvdShelfView::cameraXChanged);
    QSignalSpy deleted(&page, &darkeye::ShelfPage::workDeleted);
    // Confirm the actual delete dialog against a temporary database only.
    QTimer::singleShot(0, &page, [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QMessageBox *>(widget))
                dialog->button(QMessageBox::Yes)->click();
    });
    view->on_delete_clicked(workCount-1);
    QCOMPARE(deleted.count(), 1);
    QCOMPARE(deleted.first().first().toLongLong(), deletedId);
    const qreal shelfLength = qMax(0,scene->itemCount()-1)*0.0145;
    QVERIFY(view->cameraX() >= 0.0);
    QVERIFY(view->cameraX() <= shelfLength * 0.5);
    QCOMPARE(view->cameraX(), view->cameraTargetX());
    QVERIFY(!cameraChanged.isEmpty());
    QVERIFY(scene->selectedId().isEmpty());
    QVERIFY(!scene->isExpanded());
    QCOMPARE(view->currentWorkId(), qint64(0));
    QVERIFY(view->expandedWorkCode().isEmpty());
    QCOMPARE(count->text(), workCount == 1 ? QStringLiteral("没有查询到数据")
                                         : QStringLiteral("过滤总数:%1").arg(workCount - 1));
    page.refresh();
    QTest::qWait(700);
    QVERIFY(!scene->isExpanded());
    QCOMPARE(scene->itemCount(),workCount-1);
    QCOMPARE(scene->visibleItemCount()>0,workCount>1);
    QVERIFY(!view->openWork(deletedId));
    if (workCount > 1)
    {
        QVERIFY(view->openWork(remainingId));
        QTRY_COMPARE(view->currentWorkId(), remainingId);
    }
}

void ShelfPageTest::routeJumpReplacesAnAlreadyExpandedWork()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(temporaryDirectory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    darkeye::WorkRepository repository(connection.database());
    qint64 firstId = -1;
    qint64 secondId = -1;
    QString firstSerial;
    QString secondSerial;
    // These adjacent entries both occupy local delegate index 30 after
    // virtualization.  That is the case which previously retained the old
    // delegate's frozen cover when a route jump arrived.
    for (int index = 0; index < 100; ++index)
    {
        darkeye::Work work;
        work.serialNumber = QStringLiteral("ROUTE-%1").arg(index, 3, 10, QLatin1Char('0'));
        const auto id = repository.insertComplete(work, {}, {}, {}, &error);
        QVERIFY(id.has_value());
        if (index == 40) { firstId = *id; firstSerial = work.serialNumber; }
        if (index == 41) { secondId = *id; secondSerial = work.serialNumber; }
    }

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::ShelfPage page(connection.database(), {}, themes);
    page.initialize();
    page.resize(1280,800);
    page.show();
    auto *view = page.findChild<darkeye::DvdShelfView *>(QStringLiteral("DvdShelfView"));
    QVERIFY(view != nullptr);

    QVERIFY(page.showWork(firstId));
    QTRY_COMPARE(view->expandedWorkCode(), firstSerial);
    QTest::qWait(600);
    QVERIFY(page.showWork(secondId));
    QTRY_COMPARE(view->expandedWorkCode(), secondSerial);
    // Allow the first delayed expand callback to run; it must not restore the
    // previous work after a new route request has arrived.
    QTest::qWait(650);
    QCOMPARE(view->expandedWorkCode(), secondSerial);
    auto *coverInfo=view->findChild<QScrollArea *>("ShelfCoverInfo");
    QVERIFY(coverInfo);
    QCOMPARE(coverInfo->findChild<QToolButton *>("ShelfCode")->text(),secondSerial);

    auto updated = repository.findById(secondId, &error);
    QVERIFY(updated.has_value());
    updated->chineseTitle = QStringLiteral("采集后的新标题");
    QVERIFY(repository.updateDetails(*updated, &error));
    page.refreshAfterCapture();
    QTRY_COMPARE(view->expandedWorkTitle(), updated->chineseTitle);
    QTRY_COMPARE(view->currentWorkId(), secondId);
    QTRY_COMPARE(view->expandedWorkCode(), secondSerial);
}

void ShelfPageTest::routeJumpClearsFiltersAndKeepsWorkExpanded_data()
{
    QTest::addColumn<bool>("waitForFilter");
    QTest::newRow("pending-filter") << false;
    QTest::newRow("applied-filter") << true;
}

void ShelfPageTest::routeJumpClearsFiltersAndKeepsWorkExpanded()
{
    QFETCH(bool, waitForFilter);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    darkeye::SqliteConnection privateConnection;
    QString error;
    QVERIFY(connection.open(temporaryDirectory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(privateConnection.open(temporaryDirectory.filePath(QStringLiteral("private.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateConnection, darkeye::DatabaseKind::Private, &error));
    darkeye::WorkRepository repository(connection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("ROUTE-FILTER-001");
    const auto id = repository.insertComplete(work, {}, {}, {}, &error);
    QVERIFY(id.has_value());

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::graph::GraphManager graph(connection.database());
    QVERIFY2(graph.initialize(&error), qPrintable(error));
    darkeye::ShelfPage page(connection.database(), privateConnection.database(), themes, &graph);
    page.initialize();
    page.resize(1280,800);
    page.show();
    auto *serial = page.findChild<QLineEdit *>();
    auto *scope = page.findChild<QComboBox *>(QStringLiteral("ShelfScopeSelector"));
    auto *sort = page.findChild<QComboBox *>(QStringLiteral("ShelfSortSelector"));
    auto *count = page.findChild<QLabel *>(QStringLiteral("ShelfCountLabel"));
    auto *view = page.findChild<darkeye::DvdShelfView *>(QStringLiteral("DvdShelfView"));
    QVERIFY(serial != nullptr);
    QVERIFY(scope != nullptr);
    QVERIFY(sort != nullptr);
    QVERIFY(count != nullptr);
    QVERIFY(view != nullptr);
    scope->setCurrentIndex(1);
    sort->setCurrentIndex(3);
    serial->setText(QStringLiteral("no-match"));
    if (waitForFilter) QTest::qWait(100);

    QVERIFY(page.showWork(*id));
    QVERIFY(serial->text().isEmpty());
    QCOMPARE(scope->currentIndex(), 0);
    QCOMPARE(sort->currentIndex(), 0);
    QCOMPARE(count->text(), QStringLiteral("过滤总数:1"));
    QTRY_COMPARE(view->expandedWorkCode(), work.serialNumber);
    // Both the 50 ms filter debounce and the 550 ms route expansion must finish
    // without a stale filter reload discarding the destination selection.
    QTest::qWait(700);
    QCOMPARE(view->expandedWorkCode(), work.serialNumber);
    auto *scene=view->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene != nullptr);
    QTRY_VERIFY(scene->isExpanded());
    QCOMPARE(scene->selectedId(),QString::number(*id));
    scene->renderWidget()->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *relation = view->findChild<QWidget *>("ShelfRelationGraphOverlay");
    QVERIFY(relation != nullptr);
    QTRY_VERIFY(scene->isFullyExpanded());
    QVERIFY(relation->isHidden());
    scene->toggleOpen();
    QVERIFY(relation->isHidden());
    QTRY_VERIFY_WITH_TIMEOUT(relation->isVisible(), 5000);
    QVERIFY(scene->isNearlyClosed());
    // A route-opened DVD must follow the same graph lifecycle as a clicked DVD.
    scene->toggleOpen();
    QVERIFY(relation->isHidden());
    QTRY_VERIFY(scene->isFullyExpanded());
    scene->toggleOpen();
    QTRY_VERIFY_WITH_TIMEOUT(relation->isVisible(), 5000);
}

QTEST_MAIN(ShelfPageTest)
#include "ShelfPageTest.moc"
