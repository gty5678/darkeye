#include "render3d/RhiSceneWidget.h"

#include <QImage>
#include <QAction>
#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <QTest>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>
#include <limits>

using namespace darkeye::render3d;

namespace {
bool colorNear(const QColor &a, const QColor &b, int tolerance = 4)
{
    return std::abs(a.red() - b.red()) <= tolerance
        && std::abs(a.green() - b.green()) <= tolerance
        && std::abs(a.blue() - b.blue()) <= tolerance;
}

QRect foregroundBounds(const QImage &image)
{
    if (image.isNull())
        return {};
    const auto background = image.pixelColor(0, 0);
    QRect result;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (!colorNear(background, image.pixelColor(x, y)))
                result = result.united(QRect(x, y, 1, 1));
    return result;
}

MeshHandle texturedQuad()
{
    auto mesh = std::make_shared<MeshData>();
    mesh->vertexColorsSrgb = false;
    mesh->vertices = {
        {{-1,1,0},{1,1,1},{0,0,1},{0,0}},
        {{1,1,0},{1,1,1},{0,0,1},{1,0}},
        {{1,-1,0},{1,1,1},{0,0,1},{1,1}},
        {{-1,-1,0},{1,1,1},{0,0,1},{0,1}}};
    mesh->indices = {0,2,1,0,3,2};
    return mesh;
}
}

class RhiSceneWidgetTest final : public QObject {
    Q_OBJECT
private slots:
    void coverLodChangesDoNotResizeTheEntireTextureCache()
    {
        RhiSceneWidget view;
        view.resize(400,300);
        constexpr quint64 budget=2*1024*1024;
        view.setTextureBudget(budget);
        const auto scene=std::make_shared<Scene>();
        const auto mesh=texturedQuad();
        std::vector<std::shared_ptr<Material>> materials;
        for(int i=0;i<12;++i) {
            auto texture=std::make_shared<TextureData>();
            texture->image=QImage(256,512,QImage::Format_RGBA8888);
            texture->image.fill(QColor::fromHsv(i*29,160,220));
            auto material=std::make_shared<Material>();
            material->baseColorTexture=texture;
            materials.push_back(material);
            QVERIFY(scene->setMesh(scene->createNode(),mesh,material));
        }
        view.setScene(scene); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(!view.grabFramebuffer().isNull());
        QVERIFY(view.statistics().sourceTextureBytes<=budget);
        const auto uploads=view.statistics().textureUploads;
        auto promoted=std::make_shared<TextureData>();
        promoted->image=QImage(256,544,QImage::Format_RGBA8888);
        promoted->image.fill(Qt::blue);
        materials.front()->baseColorTexture=promoted;
        view.update();
        QVERIFY(!view.grabFramebuffer().isNull());
        // A small source-resolution change previously resized all 12 images.
        // Only the replacement and the minimum budget reclamation may upload.
        QVERIFY(view.statistics().textureUploads-uploads<=3);
        QVERIFY(view.statistics().sourceTextureBytes<=budget);
        const auto stableUploads=view.statistics().textureUploads;
        auto camera=view.camera(); camera.target.setX(0.1f); view.setCamera(camera);
        QVERIFY(!view.grabFramebuffer().isNull());
        QCOMPARE(view.statistics().textureUploads,stableUploads);
        view.setTextureBudget(8*1024*1024);
        QVERIFY(!view.grabFramebuffer().isNull());
        QVERIFY(view.statistics().sourceTextureBytes>budget);
        QVERIFY(view.statistics().sourceTextureBytes<=8*1024*1024);
        view.setTextureBudget(budget/4);
        QVERIFY(!view.grabFramebuffer().isNull());
        QVERIFY(view.statistics().sourceTextureBytes<=budget/4);
        QVERIFY(view.lastError().isEmpty());
    }

    void statisticsPanelCanToggleWithoutDrivingIdleRendering()
    {
        QWidget window;
        window.resize(480,360);
        RhiSceneWidget view(&window);
        view.setGeometry(window.rect());
        const auto scene=std::make_shared<Scene>();
        const auto node=scene->createNode();
        QVERIFY(scene->setMesh(node,texturedQuad(),std::make_shared<Material>()));
        view.setScene(scene);
        QVERIFY(!view.statisticsVisible());
        view.statisticsAction()->trigger();
        QVERIFY(view.statisticsVisible());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *panel=window.findChild<QFrame *>(QStringLiteral("RhiStatisticsPanel"));
        auto *values=window.findChild<QLabel *>(QStringLiteral("RhiStatisticsValues"));
        QVERIFY(panel && panel->isVisible());
        QVERIFY(values);
        QCOMPARE(panel->pos(),view.mapToGlobal(QPoint(12,12)));
        QTRY_VERIFY(!view.statistics().backend.isEmpty()
                    && values->text().contains(view.statistics().backend));
        const auto panelImage=panel->grab().toImage();
        const auto background=panelImage.pixelColor(panelImage.width()/2,qRound(4*panelImage.devicePixelRatio()));
        QVERIFY(background.alpha()>200 && background.red()<50);
        const auto screenshot=qEnvironmentVariable("DARKEYE_STATISTICS_SCREENSHOT");
        if(!screenshot.isEmpty()) QVERIFY(panel->grab().save(screenshot));
        // Updating the statistics UI must not keep an otherwise static scene
        // rendering or count its own repaints as a continuous FPS workload.
        QTest::qWait(1600);
        const auto frames=view.statistics().submittedFrames;
        QTest::qWait(600);
        QCOMPARE(view.statistics().submittedFrames,frames);
        QVERIFY(values->text().contains(QStringLiteral("FPS（提交）  0.0")));
        auto *close=window.findChild<QToolButton *>(QStringLiteral("RhiStatisticsClose"));
        QVERIFY(close);
        QTest::mouseClick(close,Qt::LeftButton);
        QVERIFY(!view.statisticsVisible());
        QVERIFY(!view.statisticsAction()->isChecked());
        QVERIFY(panel->isHidden());
        view.statisticsAction()->trigger();
        QVERIFY(panel->isVisible());
        view.hide();
        QTest::qWait(100);
        const auto hiddenFrames=view.statistics().submittedFrames;
        QTest::qWait(600);
        QCOMPARE(view.statistics().submittedFrames,hiddenFrames);
        view.setParent(nullptr);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(panel->isVisible());
        QCOMPARE(panel->pos(),view.mapToGlobal(QPoint(12,12)));
    }

    void replacingOneCoverPreservesOtherMaterialBindings()
    {
        RhiSceneWidget view;
        view.resize(400,300);
        const auto scene=std::make_shared<Scene>();
        const auto mesh=texturedQuad();
        const auto makeTexture=[](QColor color) {
            auto texture=std::make_shared<TextureData>();
            texture->image=QImage(8,8,QImage::Format_RGBA8888);
            texture->image.fill(color);
            return texture;
        };
        auto left=std::make_shared<Material>();
        left->shading=Material::Shading::Unlit;
        left->baseColorTexture=makeTexture(Qt::red);
        auto right=std::make_shared<Material>();
        right->shading=Material::Shading::Unlit;
        right->baseColorTexture=makeTexture(Qt::green);
        const auto leftNode=scene->createNode(),rightNode=scene->createNode();
        QVERIFY(scene->setMesh(leftNode,mesh,left));
        QVERIFY(scene->setMesh(rightNode,mesh,right));
        Transform pose; pose.scale={0.5f,0.5f,1}; pose.translation={-0.65f,0,0};
        QVERIFY(scene->setTransform(leftNode,pose));
        pose.translation.setX(0.65f);
        QVERIFY(scene->setTransform(rightNode,pose));
        view.setScene(scene);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(!view.grabFramebuffer().isNull());
        const auto builds=view.statistics().materialBindingBuilds;
        const auto uploads=view.statistics().geometryUploads;

        left->baseColorTexture=makeTexture(Qt::blue);
        view.update();
        const auto image=view.grabFramebuffer();
        QVERIFY(!image.isNull());
        QCOMPARE(view.statistics().materialBindingBuilds,builds+1);
        const auto sample=[&](QVector3D center) {
            const auto p=view.camera().project(center,view.size()).value();
            return image.pixelColor(qRound(p.x()*image.width()/view.width()),
                                    qRound(p.y()*image.height()/view.height()));
        };
        QVERIFY(colorNear(sample({-0.65f,0,0}),Qt::blue));
        QVERIFY(colorNear(sample({0.65f,0,0}),Qt::green));

        QVERIFY(scene->removeNode(leftNode));
        view.update();
        QVERIFY(!view.grabFramebuffer().isNull());
        QCOMPARE(view.statistics().materialBindingBuilds,builds+1);
        QCOMPARE(view.statistics().geometryUploads,uploads);
        QVERIFY(view.lastError().isEmpty());
    }

    void anisotropicCoverPreservesSpineDetail()
    {
        RhiSceneWidget view; view.resize(400,400);
        auto scene=std::make_shared<Scene>();
        const auto node=scene->createNode();
        auto texture=std::make_shared<TextureData>();
        texture->srgb=false;
        texture->mipmap=TextureData::Mipmap::Linear;
        texture->image=QImage(2048,512,QImage::Format_RGBA8888);
        for(int y=0;y<512;++y) {
            const auto color=(y/32)%2?QColor(Qt::white):QColor(Qt::black);
            for(int x=0;x<2048;++x) texture->image.setPixelColor(x,y,color);
        }
        auto material=std::make_shared<Material>();
        material->shading=Material::Shading::Unlit;
        material->baseColorTexture=texture;
        const auto mesh=texturedQuad();
        QVERIFY(scene->setMesh(node,mesh,material));
        Transform narrow; narrow.scale={0.06f,1.2f,1};
        QVERIFY(scene->setTransform(node,narrow));
        view.setScene(scene); view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto contrast=[&](const QImage &image) {
            int low=255,high=0;
            const auto top=view.camera().project({0,1.0f,0},view.size());
            const auto bottom=view.camera().project({0,-1.0f,0},view.size());
            if(!top || !bottom) return 0;
            for(int y=int(top->y()*view.devicePixelRatioF());y<int(bottom->y()*view.devicePixelRatioF());++y) {
                const int value=image.pixelColor(image.width()/2,y).red();
                low=std::min(low,value); high=std::max(high,value);
            }
            return high-low;
        };
        const int blurred=contrast(view.grabFramebuffer());
        texture->maxAnisotropy=8;
        view.update();
        const int sharp=contrast(view.grabFramebuffer());
        QVERIFY2(sharp>blurred+30,qPrintable(QStringLiteral("isotropic=%1 anisotropic=%2").arg(blurred).arg(sharp)));
        QVERIFY(view.lastError().isEmpty());
    }

    void layeredTransmissionComposition()
    {
        RhiSceneWidget view; view.resize(320,320);
        auto scene=std::make_shared<Scene>(); const auto mesh=texturedQuad();
        auto white=std::make_shared<Material>(); white->baseColor={1,1,1,1};
        const auto background=scene->createNode(); scene->setMesh(background,mesh,white);
        Transform large; large.scale={2,2,2}; scene->setTransform(background,large);
        const auto pane=[&](float z,QVector4D tint) {
            auto material=std::make_shared<Material>(); material->baseColor=tint;
            material->shading=Material::Shading::MetallicRoughness;
            material->transmission=1; material->ior=1; material->roughness=0;
            const auto id=scene->createNode(); scene->setMesh(id,mesh,material);
            Transform transform; transform.translation={0,0,z}; scene->setTransform(id,transform);
            return id;
        };
        const auto front=pane(0.4f,{1,0.5f,1,1}); // deliberately insert front before back
        const auto back=pane(0.2f,{0.5f,1,1,1});
        view.setScene(scene); view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto center=[&] {
            view.update(); const auto image=view.grabFramebuffer();
            return image.pixelColor(image.width()/2,image.height()/2);
        };
        QVERIFY(colorNear(center(),QColor(188,188,255),3));
        QCOMPARE(view.statistics().transmissionPasses,2);
        QCOMPARE(view.statistics().transmissionFallbackItems,0);
        const auto third=pane(0.6f,{1,1,0.5f,1});
        QVERIFY(colorNear(center(),QColor(188,188,188),3)); // linear tint accumulates across three layers
        QCOMPARE(view.statistics().transmissionPasses,3);
        view.setSampleCount(4); QVERIFY(colorNear(center(),QColor(188,188,188),3));
        view.setSampleCount(1);
        auto settings=view.lighting(); settings.toneMapping=true; view.setLighting(settings);
        QVERIFY(colorNear(center(),QColor(206,206,206),3)); // ACES is applied once after all layers
        settings.exposure=2; view.setLighting(settings);
        QVERIFY(colorNear(center(),QColor(232,232,232),3));
        settings.toneMapping=false; settings.exposure=1;
        settings.transmissionPassLimit=1; view.setLighting(settings);
        QVERIFY(colorNear(center(),QColor(255,255,188),3)); // explicit bounded approximation
        QCOMPARE(view.statistics().transmissionPasses,1);
        QCOMPARE(view.statistics().transmissionFallbackItems,2);
        settings.transmissionPassLimit=2; view.setLighting(settings);
        QVERIFY(colorNear(center(),QColor(255,188,188),3)); // preserve the nearest layer when distant batches merge
        QCOMPARE(view.statistics().transmissionPasses,2);
        QCOMPARE(view.statistics().transmissionFallbackItems,1);
        settings.transmissionPassLimit=8; view.setLighting(settings);
        QVERIFY(colorNear(center(),QColor(188,188,188),3));
        // Alpha blend behind the glass must be included in its sampled color.
        scene->setVisible(third,false); scene->setVisible(back,false);
        auto alpha=std::make_shared<Material>(); alpha->baseColor={1,0,0,0.5f}; alpha->alphaMode=Material::AlphaMode::Blend;
        scene->setMesh(back,mesh,alpha); scene->setVisible(back,true);
        QVERIFY(colorNear(center(),QColor(255,137,188),3));
        QCOMPARE(view.statistics().transmissionPasses,2);
        // Opaque paper in front occludes both surfaces, including in later passes.
        const auto paper=scene->createNode(); auto blue=std::make_shared<Material>(); blue->baseColor={0,0,1,1};
        scene->setMesh(paper,mesh,blue); Transform paperTransform; paperTransform.translation={0,0,0.8f}; scene->setTransform(paper,paperTransform);
        QVERIFY(colorNear(center(),Qt::blue,2));
        scene->setVisible(paper,false);
        view.setSampleCount(4); QVERIFY(colorNear(center(),QColor(255,137,188),3));
        view.resize(400,260); QVERIFY(colorNear(center(),QColor(255,137,188),3));
        scene->setVisible(front,false); scene->setVisible(back,false);
        QVERIFY(colorNear(center(),Qt::white,2));
        QCOMPARE(view.statistics().transmissionPasses,0);
        QCOMPARE(view.statistics().transmissionFallbackItems,0);
        scene->removeNode(front); scene->removeNode(back);
        const auto left=pane(0.3f,{0.5f,1,1,1}); const auto right=pane(0.3f,{1,0.5f,1,1});
        Transform leftPose; leftPose.translation={-0.8f,0,0.3f}; leftPose.scale={0.3f,0.3f,0.3f};
        auto rightPose=leftPose; rightPose.translation.setX(0.8f);
        scene->setTransform(left,leftPose); scene->setTransform(right,rightPose);
        center(); QCOMPARE(view.statistics().transmissionPasses,1); // disjoint panes share a snapshot
        QCOMPARE(view.statistics().transmissionFallbackItems,0);
        QVERIFY2(view.lastError().isEmpty(),qPrintable(view.lastError()));
    }

    void transmissionRefractionAndResourceLifetime()
    {
        QWidget host;
        auto *view=new RhiSceneWidget(&host); view->resize(320,320); host.resize(320,320);
        auto scene=std::make_shared<Scene>(); const auto mesh=texturedQuad();
        auto background=std::make_shared<Material>(); background->baseColor={0.1f,0.6f,0.2f,1};
        const auto behind=scene->createNode(); scene->setMesh(behind,mesh,background);
        Transform enlarged; enlarged.scale={2,2,2}; scene->setTransform(behind,enlarged);
        const auto shell=scene->createNode(); Transform front; front.translation={0,0,0.2f}; scene->setTransform(shell,front);
        auto clear=std::make_shared<Material>(); clear->shading=Material::Shading::MetallicRoughness;
        clear->doubleSided=true; clear->transmission=1; clear->ior=1; clear->roughness=0;
        scene->setMesh(shell,mesh,clear); view->setScene(scene); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        const auto center=[&] {
            const auto image=view->grabFramebuffer();
            return image.pixelColor(image.width()/2,image.height()/2);
        };
        const auto draw=[&](std::shared_ptr<Material> material) {
            scene->setMesh(shell,mesh,std::move(material)); view->update(); return center();
        };
        const auto texture=[](QColor color) {
            auto image=std::make_shared<TextureData>(); image->srgb=false;
            image->image=QImage(4,4,QImage::Format_RGBA8888); image->image.fill(color); return image;
        };
        const auto transmitted=center();
        QVERIFY(colorNear(transmitted,QColor(89,203,124),3)); // alpha remains one, background is transmitted
        auto opaque=std::make_shared<Material>(*clear); opaque->transmission=0;
        const auto diffuse=draw(opaque);
        auto blocked=std::make_shared<Material>(*clear); blocked->transmissionTexture=texture(QColor(0,255,255));
        QVERIFY(colorNear(draw(blocked),diffuse,3)); // transmission uses red only
        auto absorbed=std::make_shared<Material>(*clear); absorbed->thickness=1; absorbed->attenuationDistance=1;
        absorbed->attenuationColor={0.25f,1,1}; absorbed->thicknessTexture=texture(QColor(0,255,0));
        const auto tinted=draw(absorbed);
        QVERIFY(tinted.red()<transmitted.red()-35); QVERIFY(std::abs(tinted.green()-transmitted.green())<=3);
        auto noThickness=std::make_shared<Material>(*absorbed); noThickness->thicknessTexture=texture(QColor(255,0,255));
        QVERIFY(colorNear(draw(noThickness),transmitted,3)); // thickness uses green only
        auto reflection=std::make_shared<Material>(*clear); reflection->ior=1.49f;
        reflection->roughness=0.5f;
        auto dark=std::make_shared<Material>(); dark->baseColor={0,0,0,1}; scene->setMesh(behind,mesh,dark);
        const auto noReflection=draw(clear);
        QVERIFY(draw(reflection).red()>noReflection.red()+8); // reflection survives transmission=1
        auto gradient=std::make_shared<TextureData>(); gradient->srgb=false;
        gradient->image=QImage(64,64,QImage::Format_RGBA8888);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) gradient->image.setPixelColor(x,y,QColor(x*4,0,252-x*4));
        auto pattern=std::make_shared<Material>(); pattern->baseColorTexture=gradient;
        scene->setMesh(behind,mesh,pattern);
        const auto flat=draw(reflection);
        auto refracting=std::make_shared<Material>(*reflection); refracting->thickness=0.8f;
        refracting->normalTexture=texture(QColor(218,128,218));
        const auto bent=draw(refracting);
        QVERIFY2(bent.red()<flat.red()-8,qPrintable(QString("flat=%1 bent=%2").arg(flat.red()).arg(bent.red())));
        auto thin=std::make_shared<Material>(*refracting); thin->thickness=0;
        QVERIFY(draw(thin).red()>bent.red()+8); // thin sheet has no macroscopic refraction
        view->resize(400,260); QVERIFY(!view->grabFramebuffer().isNull());
        view->setSampleCount(4); QVERIFY(!view->grabFramebuffer().isNull());
        QWidget secondHost; secondHost.resize(400,260); view->setParent(&secondHost); view->show(); secondHost.show();
        QVERIFY(QTest::qWaitForWindowExposed(&secondHost)); QVERIFY(!view->grabFramebuffer().isNull());
        const auto targetBytes=[&] { return view->statistics().textureBytes-view->statistics().sourceTextureBytes; };
        const auto withSnapshot=targetBytes();
        draw(opaque); QVERIFY(targetBytes()<withSnapshot);
        draw(clear); QCOMPARE(targetBytes(),withSnapshot);
        QVERIFY2(view->lastError().isEmpty(),qPrintable(view->lastError()));
        view->setParent(&host); view->show();
        QVERIFY(!view->grabFramebuffer().isNull()); // initialize on the surviving top-level QRhi
    }

    void normalOcclusionAndEmissiveMaterials()
    {
        RhiSceneWidget view; view.resize(320,320);
        auto scene=std::make_shared<Scene>();
        auto mesh=std::make_shared<MeshData>(*texturedQuad());
        auto base=std::make_shared<Material>(); base->doubleSided=true;
        base->shading=Material::Shading::MetallicRoughness; base->baseColor={0.5f,0.5f,0.5f,1};
        const auto node=scene->createNode(); scene->setMesh(node,mesh,base); view.setScene(scene);
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto draw=[&](std::shared_ptr<Material> material) {
            scene->setMesh(node,mesh,std::move(material)); view.update();
            const auto image=view.grabFramebuffer();
            return image.pixelColor(image.width()/2,image.height()/2);
        };
        const auto texture=[](QColor color,bool srgb=false) {
            auto result=std::make_shared<TextureData>(); result->srgb=srgb;
            result->image=QImage(4,4,QImage::Format_RGBA8888); result->image.fill(color);
            return result;
        };
        const auto original=draw(base);
        auto normal=std::make_shared<Material>(*base);
        normal->normalTexture=texture(QColor(128,218,218));
        const auto derivative=draw(normal);
        QVERIFY(derivative.red()<original.red()-20); // UV +V points down on this quad
        normal->normalScale=0;
        QVERIFY(colorNear(draw(normal),original,3));
        normal=std::make_shared<Material>(*normal); normal->normalScale=1;
        mesh=std::make_shared<MeshData>(*mesh); mesh->hasTangents=true;
        for(auto &vertex:mesh->vertices) vertex.tangent={1,0,0,1};
        const auto authored=draw(normal);
        QVERIFY(authored.red()>original.red()+15); // imported +Y tangent frame
        mesh=std::make_shared<MeshData>(*mesh);
        for(auto &vertex:mesh->vertices) vertex.tangent[3]=-1;
        QVERIFY(colorNear(draw(normal),derivative,3));
        Transform mirrored; mirrored.scale={-1,1,1}; scene->setTransform(node,mirrored);
        QVERIFY(colorNear(draw(normal),derivative,3));
        scene->setTransform(node,{});
          auto backface=std::make_shared<Material>(*normal);
          backface->normalTexture=texture(QColor(218,128,218));
          const auto frontNormal=draw(backface);
          Transform turned; turned.rotation=QQuaternion::fromAxisAndAngle(0,1,0,180);
          scene->setTransform(node,turned);
          QVERIFY(colorNear(draw(backface),frontNormal,3)); // flip the entire mapped normal on the back
          scene->setTransform(node,{});
        auto ao=std::make_shared<Material>(*base); ao->occlusionTexture=texture(QColor(0,255,255));
        const auto occluded=draw(ao);
        QVERIFY(occluded.red()<original.red()-10);
        ao->occlusionStrength=0; QVERIFY(colorNear(draw(ao),original,3));
        auto emissive=std::make_shared<Material>(*base);
        emissive->baseColor={0,0,0,1}; emissive->metallic=1;
        emissive->emissiveFactor={0.5f,0.25f,0}; emissive->emissiveTexture=texture(QColor(128,255,0),true);
        QVERIFY(colorNear(draw(emissive),QColor(92,137,0),3));
        emissive->emissiveStrength=2; QVERIFY(colorNear(draw(emissive),QColor(128,188,0),3));
        emissive->occlusionTexture=texture(Qt::black); QVERIFY(colorNear(draw(emissive),QColor(128,188,0),3));
        emissive->shading=Material::Shading::Unlit; QVERIFY(colorNear(draw(emissive),Qt::black,2));
        QVERIFY2(view.lastError().isEmpty(),qPrintable(view.lastError()));
    }

    void largeFramebufferAndMinimumWidget()
    {
        RhiSceneWidget view;
        view.resize(240,160);
        QVERIFY(view.setMesh(coloredCube()));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames>0);
        const auto buffers=view.statistics().bufferBytes;
        view.setFixedColorBufferSize(3840,2160);
        auto large=view.grabFramebuffer();
        QCOMPARE(large.size(),QSize(3840,2160));
        QVERIFY(colorNear(large.pixelColor(1920,1080),QColor::fromRgbF(0.20f,0.72f,0.95f)));
        const auto textures=view.statistics().textureBytes;
        view.setFixedColorBufferSize({});
        view.resize(80,60);
        const auto small=view.grabFramebuffer();
        QCOMPARE(small.size(),QSize(qRound(80*view.devicePixelRatioF()),qRound(60*view.devicePixelRatioF())));
        QCOMPARE(view.statistics().bufferBytes,buffers);
        QVERIFY(view.statistics().textureBytes<textures/4);
        QVERIFY2(view.lastError().isEmpty(),qPrintable(view.lastError()));
    }

    void textureBudgetAndEviction()
    {
        RhiSceneWidget view;
        view.resize(400,320);
        auto scene=std::make_shared<Scene>();
        const auto mesh=texturedQuad();
        std::vector<std::shared_ptr<TextureData>> textures;
        for(int i=0;i<16;++i) {
            auto texture=std::make_shared<TextureData>();
            texture->image=QImage(256,256,QImage::Format_RGBA8888);
            texture->image.fill(QColor::fromHsv(i*20,150,200));
            textures.push_back(texture);
            auto material=std::make_shared<Material>();
            material->doubleSided=true;
            material->baseColorTexture=texture;
            scene->setMesh(scene->createNode(),mesh,material);
        }
        view.setScene(scene);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames>0);
        const auto originalBytes=view.statistics().sourceTextureBytes;
        QVERIFY(originalBytes>=4*1024*1024);
        const auto uploads=view.statistics().geometryUploads;
        view.setTextureBudget(65536);
        QVERIFY(!view.grabFramebuffer().isNull());
        QVERIFY(view.statistics().sourceTextureBytes<=65536);
        QCOMPARE(view.statistics().geometryUploads,uploads);
        view.setTextureBudget(32*1024*1024);
        QVERIFY(!view.grabFramebuffer().isNull());
        auto reduced=std::make_shared<Scene>();
        auto material=std::make_shared<Material>();
        material->doubleSided=true;
        material->baseColorTexture=textures.front();
        reduced->setMesh(reduced->createNode(),mesh,material);
        view.setScene(reduced);
        QVERIFY(!view.grabFramebuffer().isNull());
        QCOMPARE(view.statistics().textureCount,2); // one source and layout white
        QVERIFY(view.statistics().sourceTextureBytes<originalBytes/4);
        QCOMPARE(view.statistics().geometryUploads,uploads);
        QVERIFY2(view.lastError().isEmpty(),qPrintable(view.lastError()));
    }

    void textureUvAndSharedGeometry()
    {
        RhiSceneWidget view;
        view.resize(480, 400);
        auto scene = std::make_shared<Scene>();
        const auto mesh = texturedQuad();
        auto texture = std::make_shared<TextureData>();
        texture->image = QImage(2,2,QImage::Format_RGBA8888);
        texture->image.setPixelColor(0,0,Qt::red);
        texture->image.setPixelColor(1,0,Qt::green);
        texture->image.setPixelColor(0,1,Qt::blue);
        texture->image.setPixelColor(1,1,Qt::yellow);
        texture->magFilter = texture->minFilter = TextureData::Filter::Nearest;
        auto material = std::make_shared<Material>();
        material->baseColorTexture = texture;
        material->doubleSided = true;
        const auto node = scene->createNode();
        QVERIFY(scene->setMesh(node, mesh, material));
        view.setScene(scene);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames > 0);
        const auto image = view.grabFramebuffer();
        const auto sample = [&](const QVector3D &world) {
            const auto logical = view.camera().project(world, view.size());
            return image.pixelColor(qRound(logical->x() * view.devicePixelRatioF()),
                                    qRound(logical->y() * view.devicePixelRatioF()));
        };
        QVERIFY(colorNear(sample({-0.5f,0.5f,0}), QColor(Qt::red)));
        QVERIFY(colorNear(sample({0.5f,0.5f,0}), QColor(Qt::green)));
        QVERIFY(colorNear(sample({-0.5f,-0.5f,0}), QColor(Qt::blue)));
        QVERIFY(colorNear(sample({0.5f,-0.5f,0}), QColor(Qt::yellow)));
        const auto uploads = view.statistics().geometryUploads;
        for (int i = 0; i < 31; ++i) {
            const auto copy = scene->createNode();
            QVERIFY(scene->setMesh(copy, mesh, material));
            Transform transform;
            transform.translation = {float(i + 5),0,0};
            QVERIFY(scene->setTransform(copy, transform));
        }
        view.update();
        QVERIFY(!view.grabFramebuffer().isNull());
        QCOMPARE(view.statistics().geometryCount, 1);
        QCOMPARE(view.statistics().geometryUploads, uploads);
        QCOMPARE(scene->renderItems().size(), std::size_t(32));
        QCOMPARE(view.statistics().visibleItems, 1); // other instances are offscreen
        QCOMPARE(view.statistics().drawCalls, 2); // one surface plus display encoding
        auto gray=std::make_shared<TextureData>(*texture);
        gray->image.fill(QColor(128,128,128));
        gray->mipmap=TextureData::Mipmap::Linear;
        auto grayMaterial=std::make_shared<Material>(*material);
        grayMaterial->baseColorTexture=gray;
        scene->setMesh(node,mesh,grayMaterial);
        view.update();
        auto grayImage=view.grabFramebuffer();
        QVERIFY(colorNear(grayImage.pixelColor(grayImage.width()/2,grayImage.height()/2),QColor(128,128,128)));
        auto linear=std::make_shared<TextureData>(*gray);
        linear->srgb=false;
        auto linearMaterial=std::make_shared<Material>(*material);
        linearMaterial->baseColorTexture=linear;
        scene->setMesh(node,mesh,linearMaterial);
        view.update();
        const auto linearImage=view.grabFramebuffer();
        QVERIFY(colorNear(linearImage.pixelColor(linearImage.width()/2,linearImage.height()/2),QColor(188,188,188)));
        QVERIFY2(view.lastError().isEmpty(), qPrintable(view.lastError()));
    }

    void transparentOrderingAndLinearBlending()
    {
        RhiSceneWidget view;
        view.resize(320,320);
        view.setClearColor(Qt::black);
        auto scene = std::make_shared<Scene>();
        const auto mesh = texturedQuad();
        auto red = std::make_shared<Material>();
        red->doubleSided = true;
        red->baseColor = {1,0,0,0.5f};
        red->alphaMode = Material::AlphaMode::Blend;
        auto green = std::make_shared<Material>(*red);
        green->baseColor = {0,1,0,0.5f};
        const auto front = scene->createNode();
        const auto back = scene->createNode();
        QVERIFY(scene->setMesh(front, mesh, red));
        QVERIFY(scene->setMesh(back, mesh, green));
        Transform transform;
        transform.translation = {0,0,1};
        QVERIFY(scene->setTransform(front, transform));
        view.setScene(scene);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames > 0);
        const auto image = view.grabFramebuffer();
        // Far green then near red gives linear RGB (0.5, 0.25, 0), encoded
        // as sRGB (188,137,0). Gamma-space blending would produce (128,64,0).
        const auto center = image.pixelColor(image.width()/2,image.height()/2);
        QVERIFY2(colorNear(center, QColor(188,137,0), 4), qPrintable(center.name()));
        auto mask = std::make_shared<Material>(*red);
        mask->alphaMode = Material::AlphaMode::Mask;
        mask->alphaCutoff = 0.75f;
        scene->setMesh(front, mesh, mask);
        scene->setVisible(back, false);
        view.update();
        const auto discarded = view.grabFramebuffer();
        QVERIFY(colorNear(discarded.pixelColor(discarded.width()/2,discarded.height()/2),QColor(Qt::black)));
        QVERIFY(view.lastError().isEmpty());
    }

    void cpuValidation()
    {
        QVERIFY(MeshData{}.validationError().isEmpty());
        QVERIFY(coloredTriangle().validationError().isEmpty());
        QVERIFY(coloredCube().validationError().isEmpty());
        auto mesh = coloredCube();
        mesh.indices[0] = 999;
        QVERIFY(!mesh.validationError().isEmpty());
        mesh = coloredTriangle();
        mesh.indices.pop_back();
        QVERIFY(!mesh.validationError().isEmpty());
        mesh = coloredTriangle();
        mesh.vertices[0].position[1] = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!mesh.validationError().isEmpty());
    }

    void clearTriangleDepthAndRotation()
    {
        RhiSceneWidget view;
        view.resize(480, 360);
        view.setClearColor(QColor("#151c2b"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames > 0);
        QVERIFY2(view.lastError().isEmpty(), qPrintable(view.lastError()));
#ifdef Q_OS_WIN
        QCOMPARE(view.statistics().backend, QStringLiteral("D3D11"));
#endif
        auto empty = view.grabFramebuffer();
        QVERIFY(!empty.isNull());
        QVERIFY(foregroundBounds(empty).isEmpty());
        QVERIFY(colorNear(empty.pixelColor(0, 0), QColor("#151c2b")));
        QCOMPARE(view.statistics().geometryCount, 0);

        QVERIFY(view.setMesh(coloredTriangle()));
        const auto triangle = view.grabFramebuffer();
        const auto bounds = foregroundBounds(triangle);
        QVERIFY(bounds.width() > triangle.width() / 5);
        // Center interpolation must contain all three RGB contributions.
        const auto center = triangle.pixelColor(triangle.width() / 2, triangle.height() / 2);
        QVERIFY(center.red() > 50 && center.green() > 50 && center.blue() > 50);

        QVERIFY(view.setMesh(coloredCube()));
        view.setRotation(QQuaternion());
        const auto front = view.grabFramebuffer();
        const auto frontPixel = front.pixelColor(front.width() / 2, front.height() / 2);
        // Back is submitted later with culling OFF, so this fails without depth.
        QVERIFY2(colorNear(frontPixel, QColor::fromRgbF(0.20f, 0.72f, 0.95f)),
                 qPrintable(frontPixel.name()));
        view.setRotation(QQuaternion::fromEulerAngles(0, 180, 0));
        const auto back = view.grabFramebuffer();
        QVERIFY(colorNear(back.pixelColor(back.width() / 2, back.height() / 2),
                          QColor::fromRgbF(0.95f, 0.30f, 0.40f)));
        QVERIFY(view.lastError().isEmpty());
    }

    void resizeDprAndIdleCache()
    {
        RhiSceneWidget view;
        QVERIFY(view.setMesh(coloredCube()));
        view.resize(640, 320);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames > 0);
        const auto first = view.grabFramebuffer();
        const auto initial = view.statistics();
        QCOMPARE(first.size(), QSize(qRound(view.width() * view.devicePixelRatioF()),
                                     qRound(view.height() * view.devicePixelRatioF())));
        auto bounds = foregroundBounds(first);
        QVERIFY(std::abs(bounds.width() - bounds.height()) <= 2);
        for (const QSize size : {QSize(320, 640), QSize(650, 310), QSize(400, 400)}) {
            view.resize(size);
            const auto image = view.grabFramebuffer();
            bounds = foregroundBounds(image);
            QVERIFY(bounds.width() > 50);
            QVERIFY(std::abs(bounds.width() - bounds.height()) <= 2);
            QCOMPARE(view.statistics().resourceGenerations, initial.resourceGenerations);
            QCOMPARE(view.statistics().pipelineGenerations, initial.pipelineGenerations);
            QCOMPARE(view.statistics().geometryUploads, initial.geometryUploads);
            QCOMPARE(view.statistics().bufferBytes, initial.bufferBytes);
        }
        view.setFixedColorBufferSize(333, 257);
        QCOMPARE(view.grabFramebuffer().size(), QSize(333, 257));
        view.setFixedColorBufferSize({});
        const auto automatic = view.grabFramebuffer();
        QCOMPARE(automatic.size(), QSize(qRound(view.width() * view.devicePixelRatioF()),
                                         qRound(view.height() * view.devicePixelRatioF())));
        // Allow pending resize/paint events to drain before measuring idleness.
        QTest::qWait(150);
        const auto frames = view.statistics().submittedFrames;
        QTest::qWait(200);
        QCOMPARE(view.statistics().submittedFrames, frames);
        QVERIFY2(view.lastError().isEmpty(), qPrintable(view.lastError()));
    }

    void hideReparentRecreateAndPipelineChanges()
    {
        QWidget first, second;
        first.resize(420, 340);
        second.resize(460, 380);
        QVBoxLayout firstLayout(&first), secondLayout(&second);
        auto *view = new RhiSceneWidget(&first);
        QVERIFY(view->setMesh(coloredCube()));
        view->setRotation(QQuaternion::fromEulerAngles(20, 30, 0));
        firstLayout.addWidget(view);
        first.show();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(view->statistics().submittedFrames > 0);
        const auto initial = view->statistics();
        first.showMinimized();
        QTRY_VERIFY(first.isMinimized());
        // Wait for Cocoa's native minimize transition before requesting restoration.
        QTRY_VERIFY(!first.windowHandle()->isExposed());
        first.showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(!foregroundBounds(view->grabFramebuffer()).isEmpty());
        for (int i = 0; i < 6; ++i) {
            view->hide();
            QTest::qWait(20);
            view->show();
            auto before = view->grabFramebuffer();
            QVERIFY(!foregroundBounds(before).isEmpty());
            const auto generation = view->statistics().resourceGenerations;
            const bool toSecond = i % 2 == 0;
            view->setParent(toSecond ? &second : &first);
            (toSecond ? secondLayout : firstLayout).addWidget(view);
            view->show();
            QTRY_VERIFY(view->statistics().resourceGenerations > generation);
            const auto after = view->grabFramebuffer();
            QVERIFY(foregroundBounds(after).width() > 50);
            QCOMPARE(view->statistics().bufferBytes, initial.bufferBytes);
            QVERIFY2(view->lastError().isEmpty(), qPrintable(view->lastError()));
        }
        const auto pipeline = view->statistics().pipelineGenerations;
        view->setSampleCount(4);
        QVERIFY(!foregroundBounds(view->grabFramebuffer()).isEmpty());
        QCOMPARE(view->statistics().samples, 4);
        QVERIFY(view->statistics().pipelineGenerations > pipeline);
        view->setColorBufferFormat(QRhiWidget::TextureFormat::RGBA16F);
        QVERIFY(!foregroundBounds(view->grabFramebuffer()).isEmpty());
        view->setSampleCount(1);
        view->setColorBufferFormat(QRhiWidget::TextureFormat::RGBA8);
        QVERIFY(!foregroundBounds(view->grabFramebuffer()).isEmpty());
        QVERIFY(view->lastError().isEmpty());
        delete view;
        // A new widget after both windows already exist must still initialize.
        // Ensure the native window is restored before checking submitted frames.
        first.showNormal();
        first.raise();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        for (int i = 0; i < 3; ++i) {
            auto *fresh = new RhiSceneWidget(&first);
            firstLayout.addWidget(fresh);
            QVERIFY(fresh->setMesh(coloredCube()));
            fresh->show();
            QTRY_VERIFY(fresh->statistics().submittedFrames > 0);
            QVERIFY(!foregroundBounds(fresh->grabFramebuffer()).isEmpty());
            QVERIFY(fresh->lastError().isEmpty());
            delete fresh;
        }
    }
};

QTEST_MAIN(RhiSceneWidgetTest)
#include "RhiSceneWidgetTest.moc"
