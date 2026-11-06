#include "dvd_scene/DvdSceneWidget.h"
#include "dvd_scene/DvdSceneAdapter.h"
#include "dvd_scene/DvdShelfController.h"
#include "dvd_scene/CoverTextureCache.h"
#include "dvd_scene/DvdModelAsset.h"
#include "render3d/GltfLoader.h"
#include "render3d/Raycaster.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using namespace darkeye::dvd_scene;
using namespace darkeye::render3d;

namespace {
QList<DvdItemViewModel> sampleItems(int count,const QString &cover={})
{
    QList<DvdItemViewModel> items;
    for(int i=0;i<count;++i)
        items.push_back({QString("dvd-%1").arg(i),cover,QString("DVD %1").arg(i),i,false});
    return items;
}
std::shared_ptr<const ModelAsset> model()
{
    GltfLoader loader;
    return loader.load(QStringLiteral(DARKEYE_DVD_SOURCE_DIR)
        +QStringLiteral("/apps/render3d_demo/assets/dvd-case.glb")).asset;
}
}

class DvdSceneTest final:public QObject {
    Q_OBJECT
private slots:
    void localVideoSparkleTracksAvailability()
    {
        GltfLoader loader;
        const auto loaded=loadDvdModel(loader,QStringLiteral(DARKEYE_DVD_SOURCE_DIR)+"/libs/dvd/dvd.glb");
        QVERIFY2(loaded,qPrintable(loaded.error));
        Scene scene;
        DvdCaseFactory factory(loaded.asset);
        auto item=sampleItems(1).first();
        auto instance=factory.create(scene,item,0,{});
        QVERIFY(!instance.coverPrimitives.empty());
        QVERIFY(!instance.spineCoverPrimitives.empty());
        const auto part=instance.spineCoverPrimitives.front();
        factory.updateSparkle(scene,instance,0);
        QCOMPARE(scene.node(part)->material->sparkle.z(),0.0f);
        instance.hasLocalVideo=true;
        factory.updateSparkle(scene,instance,0);
        const auto first=scene.node(part)->material;
        QCOMPARE(first->sparkle.z(),1.0f);
        for(const auto id:instance.coverPrimitives) {
            const auto *node=scene.node(id);
            if(node->parent!=instance.spine) QCOMPARE(node->material->sparkle.z(),0.0f);
        }
        factory.updateSparkle(scene,instance,0.6);
        QVERIFY(scene.node(part)->material->sparkle.x()!=first->sparkle.x());
        auto cover=std::make_shared<TextureData>();
        factory.setCover(scene,instance,cover);
        QCOMPARE(scene.node(part)->material->sparkle.z(),1.0f);
        instance.hasLocalVideo=false;
        factory.updateSparkle(scene,instance,1);
        QCOMPARE(scene.node(part)->material->sparkle.z(),0.0f);
        QCOMPARE(scene.node(part)->material->baseColorTexture.get(),cover.get());
    }

    void wheelRefreshesHoverWithoutMouseMovement()
    {
        GltfLoader loader;
        const auto loaded=loadDvdModel(loader,QStringLiteral(DARKEYE_DVD_SOURCE_DIR)+"/libs/dvd/dvd.glb");
        QVERIFY2(loaded,qPrintable(loaded.error));
        DvdSceneWidget view;
        view.resize(900,650);
        struct SyntheticHoverOnly final:QObject {
            bool eventFilter(QObject *,QEvent *event) override {
                return event->spontaneous() && (event->type()==QEvent::MouseMove
                    || event->type()==QEvent::Enter || event->type()==QEvent::Leave);
            }
        } input;
        view.renderWidget()->installEventFilter(&input);
        QVERIFY(view.setModel(loaded.asset));
        QVERIFY(view.setItems(sampleItems(80)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setCameraX(25*DvdShelfController::spacing,true);
        auto scene=view.renderWidget()->scene();
        QPointF point;
        NodeId originalRoot=0;
        for(const auto &part:scene->renderItems()) {
            const auto *node=scene->node(part.node);
            const auto *parent=node?scene->node(node->parent):nullptr;
            if(!parent || parent->name!="spine") continue;
            auto ancestor=parent;
            while(ancestor && ancestor->name!="dvd-25") ancestor=scene->node(ancestor->parent);
            if(!ancestor) continue;
            originalRoot=ancestor->id;
            QVector3D center;
            for(const auto &v:part.mesh->vertices) center+=QVector3D(v.position[0],v.position[1],v.position[2]);
            center/=float(part.mesh->vertices.size());
            const auto projected=view.renderWidget()->camera().project(part.world.map(center),view.renderWidget()->size());
            QVERIFY(projected); point=*projected; break;
        }
        QVERIFY(originalRoot);
        // Send only wheel events: there is no mouse move before or during scroll.
        const auto wheel=[&] {
            QWheelEvent event(point,view.renderWidget()->mapToGlobal(point.toPoint()),{},QPoint(0,120),
                Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(view.renderWidget(),&event);
        };
        wheel();
        QTRY_VERIFY(std::abs(view.cameraX()-view.cameraTargetX())<1e-5f);
        QTest::qWait(300);
        const auto ray=view.renderWidget()->camera().ray(point,view.renderWidget()->size());
        QVERIFY(ray);
        const auto hit=raycast(*scene,*ray);
        QVERIFY(hit);
        auto root=scene->node(hit->node);
        while(root && !root->name.startsWith("dvd-")) root=scene->node(root->parent);
        QVERIFY(root);
        QVERIFY(root->id!=originalRoot);
        const auto home=DvdCaseFactory::shelfTransform(root->name.mid(4).toInt());
        QVERIFY((root->transform.translation-home.translation-QVector3D(0,0,0.03f)).length()<1e-5f);
        QCOMPARE(scene->node(originalRoot)->transform.translation,DvdCaseFactory::shelfTransform(25).translation);
        QCOMPARE(view.renderWidget()->cursor().shape(),Qt::ArrowCursor);
        wheel();
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(view.renderWidget(),&leave);
        QTRY_VERIFY(std::abs(view.cameraX()-view.cameraTargetX())<1e-5f);
        QTest::qWait(300);
        for(const auto &part:scene->renderItems()) {
            auto ancestor=scene->node(part.node);
            while(ancestor && !ancestor->name.startsWith("dvd-")) ancestor=scene->node(ancestor->parent);
            if(ancestor) QCOMPARE(ancestor->transform.translation,
                DvdCaseFactory::shelfTransform(ancestor->name.mid(4).toInt()).translation);
        }
        QCOMPARE(view.renderWidget()->cursor().shape(),Qt::ArrowCursor);
    }

    void tiltedDiscClicksKeepCaseOpen()
    {
        GltfLoader loader;
        const auto loaded=loadDvdModel(loader,QStringLiteral(DARKEYE_DVD_SOURCE_DIR)+"/libs/dvd/dvd.glb");
        QVERIFY2(loaded,qPrintable(loaded.error));
        DvdSceneWidget view;
        view.resize(1000,700);
        struct SyntheticInputOnly final:QObject {
            bool eventFilter(QObject *,QEvent *event) override {
                return event->spontaneous() && (event->type()==QEvent::MouseMove
                    || event->type()==QEvent::Enter || event->type()==QEvent::Leave);
            }
        } input;
        view.renderWidget()->installEventFilter(&input);
        QVERIFY(view.setModel(loaded.asset));
        auto items=sampleItems(1);
        QVERIFY(view.setItems(items));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(view.focusItem(items.first().stableId));
        QTRY_COMPARE(view.state(),DvdInteractionController::State::Focused);
        view.toggleOpen();
        QTRY_VERIFY(view.isFullyExpanded());
        auto scene=view.renderWidget()->scene();
        NodeId disc=0;
        for(const auto &part:scene->renderItems()) {
            const auto *node=scene->node(part.node);
            const auto *parent=node?scene->node(node->parent):nullptr;
            if(parent && parent->name=="disc") { disc=parent->id; break; }
        }
        QVERIFY(disc);
        QSignalSpy clicked(&view,&DvdSceneWidget::discClicked);
        QSignalSpy boxClicked(&view,&DvdSceneWidget::dvdClicked);
        int boundsMismatch=0;
        for(const bool hasVideo:{false,true}) {
            items[0].hasLocalVideo=hasVideo;
            QVERIFY(view.updateItem(items[0]));
            QTest::qWait(450);
            for(int i=0;i<8;++i) {
                const float angle=float(i)*3.14159265f/4;
                const auto world=scene->worldMatrix(disc).map({0,0.04f*std::cos(angle),0.04f*std::sin(angle)});
                const auto point=view.renderWidget()->camera().project(world,view.renderWidget()->size());
                QVERIFY(point);
                const auto ray=view.renderWidget()->camera().ray(*point,view.renderWidget()->size());
                QVERIFY(ray);
                const auto exact=raycast(*scene,*ray,RaycastMode::Triangles);
                QVERIFY(exact);
                QCOMPARE(scene->node(exact->node)->parent,disc);
                const auto bounds=raycast(*scene,*ray);
                if(bounds && scene->node(bounds->node)->parent!=disc) ++boundsMismatch;
                QMouseEvent move(QEvent::MouseMove,*point,view.renderWidget()->mapToGlobal(point->toPoint()),
                    Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view.renderWidget(),&move);
                QCOMPARE(view.renderWidget()->cursor().shape(),Qt::PointingHandCursor);
                const auto before=clicked.count();
                QTest::mouseClick(view.renderWidget(),Qt::LeftButton,Qt::NoModifier,point->toPoint());
                QCOMPARE(clicked.count(),before+1);
                QCOMPARE(clicked.last().first().toString(),items.first().stableId);
                QVERIFY(view.isFullyExpanded());
                QCOMPARE(boxClicked.count(),0);
            }
        }
        QVERIFY(boundsMismatch>0); // reproduce the old shell bounding-box interception
    }

    void discPoseTracksLocalVideo()
    {
        GltfLoader loader;
        const auto loaded=loadDvdModel(loader,QStringLiteral(DARKEYE_DVD_SOURCE_DIR)+"/libs/dvd/dvd.glb");
        QVERIFY2(loaded,qPrintable(loaded.error));
        Scene scene;
        DvdCaseFactory factory(loaded.asset);
        auto item=sampleItems(1).first();
        std::map<QString,DvdCaseInstance> cases;
        cases.emplace(item.stableId,factory.create(scene,item,0,{}));
        auto &instance=cases.begin()->second;
        DvdInteractionController controller(scene,cases);
        Camera camera;
        const auto checkPose=[&](float angle) {
            const auto pose=scene.node(instance.disc)->transform;
            QCOMPARE(pose.translation,instance.discHome.translation);
            QCOMPARE(pose.scale,instance.discHome.scale);
            const auto expected=QQuaternion::fromAxisAndAngle(1,0,0,angle)*instance.discHome.rotation;
            QVERIFY(std::abs(QQuaternion::dotProduct(pose.rotation,expected))>0.99999f);
        };
        checkPose(30);
        controller.setDiscHasLocalVideo(instance,true,0);
        controller.advance(camera,0.19);
        QVERIFY(controller.active());
        const auto mid=scene.node(instance.disc)->transform.rotation;
        QVERIFY(std::abs(QQuaternion::dotProduct(mid,instance.discHome.rotation))<0.99999f);
        controller.setDiscHasLocalVideo(instance,true,0.19); // refresh must not restart animation
        controller.advance(camera,0.4);
        QVERIFY(!controller.active());
        checkPose(0);
        controller.setDiscHasLocalVideo(instance,false,1);
        controller.advance(camera,1.4);
        checkPose(30);
        controller.setDiscHasLocalVideo(instance,true,2);
        controller.advance(camera,2.1);
        controller.reset();
        checkPose(0);
        item.stableId="with-video";
        item.hasLocalVideo=true;
        const auto withVideo=factory.create(scene,item,1,{});
        QCOMPARE(scene.node(withVideo.disc)->transform.rotation,withVideo.discHome.rotation);
    }

    void discArtworkPreservesAuthoredUvAndUpdates()
    {
        GltfLoader loader;
        const auto loaded=loadDvdModel(loader,QStringLiteral(DARKEYE_DVD_SOURCE_DIR)+"/libs/dvd/dvd.glb");
        QVERIFY2(loaded,qPrintable(loaded.error));
        DvdCaseFactory factory(loaded.asset); Scene scene;
        auto first=std::make_shared<TextureData>(); first->image=QImage(1600,1000,QImage::Format_RGBA8888);
        auto replacement=std::make_shared<TextureData>(); replacement->image=QImage(1000,1600,QImage::Format_RGBA8888);
        const auto instance=factory.create(scene,sampleItems(1).first(),0,first);
        const auto other=factory.create(scene,sampleItems(2).last(),1,first);
        QCOMPARE(instance.discCoverPrimitives.size(),std::size_t(1));
        const auto face=instance.discCoverPrimitives.front();
        MeshHandle authored;
        for(const auto &node:loaded.asset->nodes) for(const auto &primitive:node.primitives)
            if(primitive.material->name=="disc-cover") authored=primitive.mesh;
        QVERIFY(authored);
        const auto check=[&](const auto &texture) {
            const auto *node=scene.node(face);
            QCOMPARE(node->material->baseColorTexture,texture);
            // QML used the model's UV and repeated the full cover atlas.
            // Keep the same shared geometry, UV, center hole and tangents.
            QCOMPARE(node->mesh,authored);
        };
        check(first);
        factory.setCover(scene,instance,replacement);
        check(replacement);
        QCOMPARE(scene.node(other.discCoverPrimitives.front())->material->baseColorTexture,first);
        for(const auto id:instance.primitives) {
            const auto *node=scene.node(id);
            if(node->parent==instance.disc && id!=face) QVERIFY(!node->material->baseColorTexture);
        }
        for(const auto &node:loaded.asset->nodes) for(const auto &primitive:node.primitives)
            if(primitive.material->name=="disc-cover") QVERIFY(!primitive.material->baseColorTexture);
    }

    void hoverStaysPulledOutUntilPointerLeaves()
    {
        DvdSceneWidget view; view.resize(700,500);
        struct SyntheticHoverOnly final:QObject {
            bool eventFilter(QObject *,QEvent *event) override {
                return event->spontaneous() && (event->type()==QEvent::MouseMove
                    || event->type()==QEvent::Enter || event->type()==QEvent::Leave);
            }
        } input;
        // Keep desktop pointer movement separate from this synthetic hover test.
        view.renderWidget()->installEventFilter(&input);
        QVERIFY(view.setModel(model())); QVERIFY(view.setItems(sampleItems(1)));
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.renderWidget()->statistics().submittedFrames>0);
        const auto scene=view.renderWidget()->scene();
        NodeId root=0;
        QPointF point;
        for(const auto &item:scene->renderItems()) {
            const auto *part=scene->node(item.node);
            const auto *parent=part?scene->node(part->parent):nullptr;
            if(!parent || parent->name!="spine") continue;
            auto ancestor=parent;
            while(ancestor && ancestor->name!="dvd-0") ancestor=scene->node(ancestor->parent);
            if(!ancestor) continue;
            root=ancestor->id;
            QVector3D center;
            for(const auto &vertex:item.mesh->vertices)
                center+=QVector3D(vertex.position[0],vertex.position[1],vertex.position[2]);
            center/=float(item.mesh->vertices.size());
            const auto projected=view.renderWidget()->camera().project(item.world.map(center),view.renderWidget()->size());
            QVERIFY(projected); point=*projected; break;
        }
        QVERIFY(root);
        const auto ray=view.renderWidget()->camera().ray(point,view.renderWidget()->size());
        QVERIFY(ray);
        QVERIFY(raycast(*scene,*ray));
        const auto home=DvdCaseFactory::shelfTransform(0);
        const auto move=[&] {
            const auto global=view.renderWidget()->mapToGlobal(point.toPoint());
            QMouseEvent event(QEvent::MouseMove,point,QPointF(global),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view.renderWidget(),&event);
        };
        move();
        QTest::qWait(350); // includes the final layout refresh after the 200ms animation
        const auto pulled=home.translation+QVector3D(0,0,0.03f);
        QCOMPARE(scene->node(root)->transform.translation,pulled);
        for(int i=0;i<3;++i) { move(); QTest::qWait(100); }
        QVERIFY((scene->node(root)->transform.translation-pulled).length()<1e-5f);
        // Data/layout refresh while stationary must also preserve the hover pose.
        QVERIFY(view.setItems(sampleItems(1)));
        QCOMPARE(scene->node(root)->transform.translation,pulled);
        QEvent leave(QEvent::Leave); QApplication::sendEvent(view.renderWidget(),&leave);
        QTest::qWait(350);
        QCOMPARE(scene->node(root)->transform.translation,home.translation);
        QVERIFY(view.focusItem("dvd-0"));
        QTRY_COMPARE_WITH_TIMEOUT(view.state(),DvdInteractionController::State::Focused,3000);
        const auto focused=scene->node(root)->transform;
        move(); QApplication::sendEvent(view.renderWidget(),&leave); QTest::qWait(350);
        QCOMPARE(scene->node(root)->transform.translation,focused.translation);
        QCOMPARE(scene->node(root)->transform.rotation,focused.rotation);
        QCOMPARE(view.selectedId(),QStringLiteral("dvd-0"));
        QVERIFY(view.renderWidget()->lastError().isEmpty());
    }

    void coverReplacementPreservesSurfaceMaterials()
    {
        auto asset=std::make_shared<ModelAsset>(*model());
        auto surface=std::make_shared<Material>();
        surface->name="cover"; surface->roughness=0.7f;
        surface->normalTexture=std::make_shared<TextureData>();
        surface->occlusionTexture=std::make_shared<TextureData>();
        surface->emissiveTexture=std::make_shared<TextureData>();
        surface->normalScale=0.4f; surface->occlusionStrength=0.6f;
        surface->emissiveFactor={0.1f,0.2f,0.3f}; surface->emissiveStrength=2;
        surface->transmission=0.9f; surface->ior=1.49f; surface->thickness=0.0008f;
        surface->attenuationColor={0.8f,0.9f,1}; surface->attenuationDistance=0.1f;
        surface->transmissionTexture=std::make_shared<TextureData>();
        surface->thicknessTexture=std::make_shared<TextureData>();
        for(auto &node:asset->nodes)
            for(auto &primitive:node.primitives)
                if(primitive.material && primitive.material->name=="cover") primitive.material=surface;
        Scene scene; DvdCaseFactory factory(asset);
        const auto firstCover=std::make_shared<TextureData>();
        const auto secondCover=std::make_shared<TextureData>();
        const auto items=sampleItems(2);
        const auto first=factory.create(scene,items[0],0,firstCover);
        const auto second=factory.create(scene,items[1],1,firstCover);
        QVERIFY(!first.coverPrimitives.empty());
        factory.setCover(scene,first,secondCover);
        for(const auto id:first.coverPrimitives) {
            const auto &material=*scene.node(id)->material;
            QCOMPARE(material.baseColorTexture,secondCover);
            QCOMPARE(material.roughness,surface->roughness);
            QCOMPARE(material.normalTexture,surface->normalTexture);
            QCOMPARE(material.normalScale,surface->normalScale);
            QCOMPARE(material.occlusionTexture,surface->occlusionTexture);
            QCOMPARE(material.occlusionStrength,surface->occlusionStrength);
            QCOMPARE(material.emissiveTexture,surface->emissiveTexture);
            QCOMPARE(material.emissiveFactor,surface->emissiveFactor);
            QCOMPARE(material.emissiveStrength,surface->emissiveStrength);
            QCOMPARE(material.transmission,surface->transmission);
            QCOMPARE(material.ior,surface->ior);
            QCOMPARE(material.thickness,surface->thickness);
            QCOMPARE(material.attenuationColor,surface->attenuationColor);
            QCOMPARE(material.attenuationDistance,surface->attenuationDistance);
            QCOMPARE(material.transmissionTexture,surface->transmissionTexture);
            QCOMPARE(material.thicknessTexture,surface->thicknessTexture);
        }
        for(const auto id:second.coverPrimitives)
            QCOMPARE(scene.node(id)->material->baseColorTexture,firstCover);
        QVERIFY(!surface->baseColorTexture);
    }

    void dataContractAndJson()
    {
        DvdShelfController shelf;
        auto items=sampleItems(1000);
        std::reverse(items.begin(),items.end());
        QVERIFY(shelf.setItems(items));
        QCOMPARE(shelf.items().first().stableId,QStringLiteral("dvd-0"));
        QCOMPARE(shelf.visibleRange(999*DvdShelfController::spacing),std::make_pair(940,60));
        QCOMPARE(shelf.visibleRange(-100),std::make_pair(0,60));
        items[0].stableId=items[1].stableId;
        QVERIFY(!shelf.setItems(items));
        QCOMPARE(shelf.items().size(),1000);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile file(temporary.filePath("items.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"items",QJsonArray{QJsonObject{
            {"stableId","test"},{"title",QStringLiteral("测试")},{"coverPath",QStringLiteral("封面.png")}}}}}).toJson());
        file.close();
        const auto adapted=DvdSceneAdapter::loadJson(file.fileName());
        QVERIFY2(bool(adapted),qPrintable(adapted.error));
        QCOMPARE(adapted.items.size(),1);
        QCOMPARE(adapted.items[0].coverPath,temporary.filePath(QStringLiteral("封面.png")));
    }

    void asynchronousCoverBudgetAndInvalidPaths()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        CoverTextureCache cache;
        const auto legacyDefault=QImage(QStringLiteral(DARKEYE_DVD_SOURCE_DIR)+"/resources/maps/0.png")
            .convertToFormat(QImage::Format_RGBA8888);
        QVERIFY(!legacyDefault.isNull());
        QCOMPARE(cache.placeholder()->image,legacyDefault);
        QCOMPARE(cache.request({}).texture,cache.placeholder());
        cache.setBudget(2*256*128*4);
        QSignalSpy spy(&cache,&CoverTextureCache::textureReady);
        for(int i=0;i<8;++i) {
            QImage image(1024,512,QImage::Format_RGBA8888);
            image.fill(QColor::fromHsv(i*40,150,200));
            const auto path=temporary.filePath(QStringLiteral("封面-%1.png").arg(i));
            QVERIFY(image.save(path));
            cache.request(path,256);
        }
        QTRY_COMPARE_WITH_TIMEOUT(cache.pendingCount(),0,5000);
        QCOMPARE(spy.size(),8);
        QVERIFY(cache.bytes()<=2*256*128*4);
        const auto missing=cache.request(temporary.filePath("missing.png"));
        QTRY_COMPARE_WITH_TIMEOUT(cache.pendingCount(),0,5000);
        QVERIFY(!cache.error(missing.key).isEmpty());
        QCOMPARE(cache.find(missing.key).get(),cache.placeholder().get());
        QFile corrupt(temporary.filePath("corrupt.png"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("not a PNG"); corrupt.close();
        const auto broken=cache.request(corrupt.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(cache.pendingCount(),0,5000);
        QVERIFY(!cache.error(broken.key).isEmpty());
        auto *pending=new CoverTextureCache;
        pending->request(temporary.filePath(QStringLiteral("封面-0.png")),1024);
        delete pending; // queued completion cannot use the destroyed cache
    }

    void coverCacheDiscardsObsoleteRequests()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        CoverTextureCache cache;
        QSignalSpy ready(&cache, &CoverTextureCache::textureReady);
        for (int index = 0; index < 8; ++index)
            cache.request(temporary.filePath(QString::number(index)));
        const auto wanted = cache.request(temporary.filePath("wanted.png"));
        cache.retainRequests({wanted.key});

        QVERIFY(cache.pendingCount() <= 3);
        QTRY_COMPARE(cache.pendingCount(), 0);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first().first().toString(), wanted.key);
    }

    void focusedCoversKeepResolutionWithoutUpscaling()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        CoverTextureCache cache;
        QImage large(3072,1920,QImage::Format_RGBA8888);
        large.fill(Qt::red);
        const auto path=temporary.filePath("large.png");
        QVERIFY(large.save(path));
        const auto request=cache.request(path,2048);
        QTRY_COMPARE(cache.pendingCount(),0);
        QVERIFY(cache.find(request.key));
        QCOMPARE(cache.find(request.key)->image.size(),QSize(2048,1280));
        QImage small(320,200,QImage::Format_RGBA8888);
        small.fill(Qt::blue);
        const auto smallPath=temporary.filePath("small.png");
        QVERIFY(small.save(smallPath));
        const auto smallRequest=cache.request(smallPath,4096);
        QTRY_COMPARE(cache.pendingCount(),0);
        QVERIFY(cache.find(smallRequest.key));
        QCOMPARE(cache.find(smallRequest.key)->image.size(),small.size());
    }

    void returnAlignsBeforeInsertion_data()
    {
        QTest::addColumn<double>("openSeconds");
        QTest::addColumn<float>("closeSpeed");
        QTest::newRow("closed")<<0.0<<1.0f;
        QTest::newRow("open")<<0.6<<1.0f;
        QTest::newRow("interrupted-opening")<<0.1<<1.0f;
        QTest::newRow("wheel-fast-close")<<0.6<<3.0f;
    }

    void returnAlignsBeforeInsertion()
    {
        QFETCH(double,openSeconds);
        QFETCH(float,closeSpeed);
        Scene scene;
        std::map<QString,DvdCaseInstance> cases;
        const auto item=sampleItems(1).first();
        DvdCaseFactory factory(model());
        cases.emplace(item.stableId,factory.create(scene,item,0,{}));
        const auto &instance=cases.at(item.stableId);
        DvdInteractionController interaction(scene,cases);
        Camera camera;
        camera.distance=0.25f;
        camera.target={0,0.1f,0};
        QVERIFY(interaction.select(item.stableId,camera,0));
        interaction.advance(camera,0.2);
        interaction.advance(camera,0.6);
        double returnTime=1;
        if(openSeconds>0) {
            interaction.toggleOpen(0.7);
            returnTime=0.7+openSeconds;
            interaction.advance(camera,returnTime);
        }
        const auto focused=scene.node(instance.root)->transform;
        interaction.returnToShelf(returnTime,closeSpeed);
        const double closeEnd=returnTime+(openSeconds>0?0.5/closeSpeed:0);
        if(openSeconds>0) {
            interaction.advance(camera,closeEnd-0.01);
            QCOMPARE(scene.node(instance.root)->transform.translation,focused.translation);
        }
        interaction.advance(camera,closeEnd+0.32);
        const auto aligned=scene.node(instance.root)->transform;
        QVERIFY((aligned.translation-(instance.home.translation+QVector3D(0,0,0.14f))).length()<1e-5f);
        QVERIFY(std::abs(QQuaternion::dotProduct(aligned.rotation,instance.home.rotation))>0.99999f);
        QCOMPARE(interaction.state(),DvdInteractionController::State::Returning);
        interaction.advance(camera,closeEnd+0.41);
        const auto inserting=scene.node(instance.root)->transform;
        QCOMPARE(inserting.translation.x(),instance.home.translation.x());
        QCOMPARE(inserting.translation.y(),instance.home.translation.y());
        QVERIFY(inserting.translation.z()>instance.home.translation.z());
        interaction.advance(camera,closeEnd+0.51);
        QCOMPARE(interaction.state(),DvdInteractionController::State::Idle);
        QCOMPARE(scene.node(instance.root)->transform.translation,instance.home.translation);
    }

    void shelfPoseRemainsStableAcrossRefreshAndReturn()
    {
        DvdSceneWidget view;
        QVERIFY(view.setModel(model()));
        auto items=sampleItems(10);
        QVERIFY(view.setItems(items));
        const auto scene=view.renderWidget()->scene();
        const auto findRoot=[&](const QString &name) {
            for(const auto &item:scene->renderItems())
                for(auto *node=scene->node(item.node);node;node=scene->node(node->parent))
                    if(node->name==name) return node->id;
            return NodeId(0);
        };
        const auto root=findRoot("dvd-5");
        QVERIFY(root);
        const auto original=scene->node(root)->transform;
        // Explicit refresh used to overwrite the pivot-adjusted home pose.
        QVERIFY(view.setItems(items));
        QCOMPARE(scene->node(root)->transform.translation,original.translation);
        view.resize(800,600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::mouseMove(view.renderWidget(),QPoint(1,1));
        for(int i=0;i<2;++i) {
            QVERIFY(view.focusItem("dvd-5"));
            QTRY_COMPARE_WITH_TIMEOUT(view.state(),DvdInteractionController::State::Focused,3000);
            if(i==1) { view.toggleOpen(); QTest::qWait(550); }
            view.returnToShelf();
            QTRY_COMPARE_WITH_TIMEOUT(view.state(),DvdInteractionController::State::Idle,3000);
            QCOMPARE(scene->node(root)->transform.translation,original.translation);
            QCOMPARE(scene->node(root)->transform.rotation,original.rotation);
        }
    }

    void interruptionAndRestore()
    {
        const auto asset=model();
        QVERIFY(asset);
        Scene scene;
        std::map<QString,DvdCaseInstance> cases;
        DvdCaseFactory factory(asset);
        const auto items=sampleItems(2);
        cases.emplace(items[0].stableId,factory.create(scene,items[0],0,{}));
        cases.emplace(items[1].stableId,factory.create(scene,items[1],1,{}));
        DvdInteractionController interaction(scene,cases);
        Camera camera;
        camera.distance=0.25f;
        camera.target={0,0.1f,0};
        for(int i=0;i<20;++i) {
            const double start=i*3;
            QVERIFY(interaction.select(items[0].stableId,camera,start));
            interaction.advance(camera,start+0.2);
            QVERIFY(interaction.select(items[1].stableId,camera,start+0.25));
            interaction.advance(camera,start+0.5);
            interaction.advance(camera,start+0.9);
            QCOMPARE(interaction.state(),DvdInteractionController::State::Focused);
            // The original QML inner pivot is translated before rotation.
            const auto rootTransform=scene.node(cases.at(items[1].stableId).root)->transform;
            const auto pivotWorld=rootTransform.matrix().map(QVector3D(-0.006707f,0,-0.000293f));
            QVERIFY((pivotWorld-QVector3D(0.0075f,0.005f,0.05f)).length()<1e-5f);
            interaction.toggleOpen(start+1);
            interaction.advance(camera,start+1.15);
            interaction.returnToShelf(start+1.2);
            interaction.advance(camera,start+2.3);
            QCOMPARE(interaction.state(),DvdInteractionController::State::Idle);
            for(const auto &[_,instance]:cases) {
                QCOMPARE(scene.node(instance.root)->transform.translation,instance.home.translation);
                QCOMPARE(scene.node(instance.root)->transform.rotation,instance.home.rotation);
            }
        }
    }

    void actualShelfSelectionOpenReturnAndVirtualization()
    {
        DvdSceneWidget view;
        view.resize(1000,700);
        QVERIFY(view.setModel(model()));
        QVERIFY(view.setItems(sampleItems(1000)));
        QSignalSpy errors(&view,&DvdSceneWidget::sceneError);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::mouseMove(view.renderWidget(),QPoint(1,1));
        QTRY_VERIFY(view.renderWidget()->statistics().submittedFrames>0);
        QCOMPARE(view.visibleItemCount(),60);
        QCOMPARE(view.renderWidget()->statistics().geometryCount,6);
        const auto uploads=view.renderWidget()->statistics().geometryUploads;
        view.scrollToIndex(750);
        QTest::qWait(900);
        QCOMPARE(view.visibleItemCount(),60);
        QCOMPARE(view.renderWidget()->statistics().geometryUploads,uploads);
        QVERIFY(view.focusItem("dvd-999"));
        QTRY_COMPARE_WITH_TIMEOUT(view.state(),DvdInteractionController::State::Focused,3000);
        QCOMPARE(view.selectedId(),QStringLiteral("dvd-999"));
        const auto closed=view.renderWidget()->grabFramebuffer();
        view.toggleOpen();
        QTest::qWait(650);
        const auto opened=view.renderWidget()->grabFramebuffer();
        QVERIFY(!opened.isNull());
        QVERIFY(opened!=closed);
        QCOMPARE(view.renderWidget()->statistics().geometryCount,9);
        const auto artifacts=qEnvironmentVariable("DARKEYE_DVD_ARTIFACTS");
        if(!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(closed.save(QDir(artifacts).filePath("focused-closed.png")));
            QVERIFY(opened.save(QDir(artifacts).filePath("focused-open.png")));
        }
        view.returnToShelf();
        QTRY_COMPARE_WITH_TIMEOUT(view.state(),DvdInteractionController::State::Idle,3000);
        QVERIFY(view.selectedId().isEmpty());
        QCOMPARE(view.visibleItemCount(),60);
        // Return can expose a different case under the native cursor. Drain
        // its 200ms hover transition before testing an otherwise idle canvas.
        view.setEnabled(false);
        QTest::qWait(250);
        const auto frames=view.renderWidget()->statistics().submittedFrames;
        QTest::qWait(200);
        QCOMPARE(view.renderWidget()->statistics().submittedFrames,frames);
        view.setEnabled(true);
        QVERIFY(view.focusItem("dvd-500"));
        QVERIFY(view.setItems(sampleItems(10))); // removing selected data is safe
        QVERIFY(view.selectedId().isEmpty());
        QCOMPARE(view.itemCount(),10);
        auto invalid=sampleItems(2);
        invalid[0].stableId=invalid[1].stableId;
        QVERIFY(!view.setItems(invalid));
        QCOMPARE(view.itemCount(),10);
        QCOMPARE(errors.size(),1); // only the intentional duplicate-ID error
        QVERIFY(view.renderWidget()->lastError().isEmpty());
    }

    void pickingAndCameraInput()
    {
        DvdSceneWidget view;
        view.resize(800,600);
        QVERIFY(view.setModel(model()));
        QVERIFY(view.setItems(sampleItems(10)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.renderWidget()->statistics().submittedFrames>0);
        const auto projectedPart=[&](const QString &part) {
            const auto scene=view.renderWidget()->scene();
            for(const auto &item:scene->renderItems()) {
                const auto *node=scene->node(item.node);
                if(!node) continue;
                const auto *parent=scene->node(node->parent);
                if(!parent || parent->name!=part) continue;
                bool belongs=false;
                for(auto ancestor=node;ancestor;ancestor=scene->node(ancestor->parent))
                    if(ancestor->name=="dvd-5") { belongs=true; break; }
                if(!belongs) continue;
                QVector3D lower(1e30f,1e30f,1e30f),upper(-1e30f,-1e30f,-1e30f);
                for(auto index:item.mesh->indices) {
                    const auto &p=item.mesh->vertices[index].position;
                    for(int axis=0;axis<3;++axis) {
                        lower[axis]=std::min(lower[axis],p[axis]);
                        upper[axis]=std::max(upper[axis],p[axis]);
                    }
                }
                const auto screen=view.renderWidget()->camera().project(item.world.map((lower+upper)*0.5f),
                    view.renderWidget()->size());
                if(screen) return screen->toPoint();
            }
            return QPoint(-1,-1);
        };
        QSignalSpy clicked(&view,&DvdSceneWidget::dvdClicked);
        const auto spine=projectedPart("spine");
        QVERIFY(spine.x()>=0);
        QTest::mouseClick(view.renderWidget(),Qt::LeftButton,Qt::NoModifier,spine);
        QCOMPARE(view.selectedId(),QStringLiteral("dvd-5"));
        QCOMPARE(clicked.size(),1);
        QCOMPARE(clicked[0][0].toString(),QStringLiteral("dvd-5"));
        QTRY_COMPARE_WITH_TIMEOUT(view.state(),DvdInteractionController::State::Focused,3000);
        QSignalSpy doubled(&view,&DvdSceneWidget::dvdDoubleClicked);
        const auto front=projectedPart("front");
        QTest::mouseDClick(view.renderWidget(),Qt::LeftButton,Qt::NoModifier,front);
        QCOMPARE(doubled.size(),1);
        QCOMPARE(doubled[0][0].toString(),QStringLiteral("dvd-5"));
        QTest::keyClick(view.renderWidget(),Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(view.state(),DvdInteractionController::State::Idle,3000);
        const auto camera=view.renderWidget()->camera();
        QTest::mousePress(view.renderWidget(),Qt::RightButton,Qt::NoModifier,QPoint(100,100));
        QTest::mouseMove(view.renderWidget(),QPoint(150,120));
        QTest::mouseRelease(view.renderWidget(),Qt::RightButton,Qt::NoModifier,QPoint(150,120));
        const auto turned=view.renderWidget()->camera();
        QCOMPARE(turned.yaw,camera.yaw-7.5f);
        QCOMPARE(turned.pitch,camera.pitch-3.0f);
        QCOMPARE(turned.position(),camera.position());
        const auto drag=[&](QPoint from,QPoint to) {
            const auto send=[&](QEvent::Type type,QPoint point,Qt::MouseButton button,Qt::MouseButtons buttons) {
                QMouseEvent event(type,QPointF(point),QPointF(view.renderWidget()->mapToGlobal(point)),
                    button,buttons,Qt::NoModifier);
                QApplication::sendEvent(view.renderWidget(),&event);
            };
            send(QEvent::MouseButtonPress,from,Qt::RightButton,Qt::RightButton);
            send(QEvent::MouseMove,to,Qt::NoButton,Qt::RightButton);
            send(QEvent::MouseButtonRelease,to,Qt::RightButton,Qt::NoButton);
        };
        drag({100,100},{1100,1100});
        QCOMPARE(view.renderWidget()->camera().yaw,-15.0f);
        QCOMPARE(view.renderWidget()->camera().pitch,-10.0f);
        QCOMPARE(view.renderWidget()->camera().position(),camera.position());
        drag({1100,1100},{-1000,-1000});
        QCOMPARE(view.renderWidget()->camera().yaw,15.0f);
        QCOMPARE(view.renderWidget()->camera().pitch,10.0f);
        QCOMPARE(view.renderWidget()->camera().position(),camera.position());
        view.resetCamera();
        QCOMPARE(view.renderWidget()->camera().yaw,0.0f);
        QCOMPARE(view.renderWidget()->camera().pitch,0.0f);
        const auto beforePan=view.renderWidget()->camera();
        QTest::mousePress(view.renderWidget(),Qt::MiddleButton,Qt::NoModifier,QPoint(100,100));
        QTest::mouseMove(view.renderWidget(),QPoint(120,120));
        QTest::mouseRelease(view.renderWidget(),Qt::MiddleButton,Qt::NoModifier,QPoint(120,120));
        QVERIFY(view.renderWidget()->camera().target.y()!=beforePan.target.y());
        const auto distance=view.renderWidget()->camera().distance;
        QWheelEvent zoom(QPointF(200,200),view.renderWidget()->mapToGlobal(QPoint(200,200)),
            {},QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(view.renderWidget(),&zoom);
        QVERIFY(view.renderWidget()->camera().distance<distance);
        const auto x=view.renderWidget()->camera().target.x();
        QWheelEvent scroll(QPointF(200,200),view.renderWidget()->mapToGlobal(QPoint(200,200)),
            {},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(view.renderWidget(),&scroll);
        QTest::qWait(800);
        QVERIFY(view.renderWidget()->camera().target.x()<x);
    }

    void pendingCoverDataReplacement()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QImage image(512,320,QImage::Format_RGBA8888);
        image.fill(Qt::red);
        const auto red=temporary.filePath("red.png");
        const auto blue=temporary.filePath("blue.png");
        QVERIFY(image.save(red));
        image.fill(Qt::blue);
        QVERIFY(image.save(blue));
        DvdSceneWidget view;
        view.resize(600,500);
        QVERIFY(view.setModel(model()));
        QVERIFY(view.setItems(sampleItems(1,red)));
        QVERIFY(view.setItems(sampleItems(1,blue)));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_COMPARE_WITH_TIMEOUT(view.pendingCovers(),0,5000);
        bool checked=false;
        for(const auto &item:view.renderWidget()->scene()->renderItems())
            if(item.material && item.material->name=="cover") {
                QVERIFY(item.material->baseColorTexture);
                QCOMPARE(item.material->baseColorTexture->source,blue);
                QCOMPARE(item.material->baseColorTexture->image.pixelColor(0,0),QColor(Qt::blue));
                checked=true;
            }
        QVERIFY(checked);
        view.setDarkTheme(false);
        const auto light=view.renderWidget()->grabFramebuffer().pixelColor(0,0);
        QVERIFY(std::abs(light.red()-238)<=2 && std::abs(light.green()-241)<=2 && std::abs(light.blue()-247)<=2);
        view.setDarkTheme(true);
        const auto dark=view.renderWidget()->grabFramebuffer().pixelColor(0,0);
        QVERIFY(std::abs(dark.red()-21)<=2 && std::abs(dark.green()-28)<=2 && std::abs(dark.blue()-43)<=2);
    }

    void wholeWidgetReparentAndHiddenAnimation()
    {
        QWidget first,second;
        first.resize(700,500);
        second.resize(700,500);
        QVBoxLayout firstLayout(&first),secondLayout(&second);
        auto *view=new DvdSceneWidget(&first);
        QVERIFY(view->setModel(model()));
        QVERIFY(view->setItems(sampleItems(10)));
        firstLayout.addWidget(view);
        first.show(); second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(view->renderWidget()->statistics().submittedFrames>0);
        const auto generation=view->renderWidget()->statistics().resourceGenerations;
        QVERIFY(view->focusItem("dvd-5"));
        view->hide(); QTest::qWait(300); view->show();
        QTRY_COMPARE_WITH_TIMEOUT(view->state(),DvdInteractionController::State::Focused,3000);
        view->setParent(&second); secondLayout.addWidget(view); view->show();
        QTRY_VERIFY(view->renderWidget()->statistics().resourceGenerations>generation);
        QCOMPARE(view->selectedId(),QStringLiteral("dvd-5"));
        QVERIFY(!view->renderWidget()->grabFramebuffer().isNull());
        QVERIFY(view->renderWidget()->lastError().isEmpty());
        delete view;
    }
};
QTEST_MAIN(DvdSceneTest)
#include "DvdSceneTest.moc"
