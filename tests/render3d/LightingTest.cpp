#include "render3d/RhiSceneWidget.h"
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <cmath>

using namespace darkeye::render3d;
namespace {
float channel(const QImage &image,int component)
{ return reinterpret_cast<const float *>(image.constScanLine(0))[component]; }
std::shared_ptr<const EnvironmentMap> constantEnvironment(const QString &directory,QString *error)
{
    QFile file(directory+"/constant.hdr");
    if(!file.open(QIODevice::WriteOnly)) return {};
    file.write("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n");
    for(int y=0;y<4;++y) {
        file.write(QByteArray::fromHex("02020008"));
        for(int value:{128,64,32,130}) { file.putChar(char(-120)); file.putChar(char(value)); }
    }
    file.close();
    return EnvironmentMap::loadHdr(file.fileName(),error);
}
std::shared_ptr<const EnvironmentMap> environment()
{
    static const auto result=[] {
        QTemporaryDir temp(QDir::currentPath()+"/lighting-XXXXXX");
        QString error;
        return constantEnvironment(temp.path(),&error);
    }();
    return result;
}
int brightness(QColor color) { return (color.red()+color.green()+color.blue())/3; }
QColor at(const QImage &image,const RhiSceneWidget &view,QVector3D point)
{
    const auto projected=view.camera().project(point,view.size());
    if(!projected) return {};
    const QPoint pixel(qRound(projected->x()*image.width()/view.width()),qRound(projected->y()*image.height()/view.height()));
    return image.rect().contains(pixel)?image.pixelColor(pixel):QColor{};
}
}
class LightingTest final:public QObject {
    Q_OBJECT
private slots:
    void aoCullsOffscreenMeshesButShadowsKeepCasters()
    {
        RhiSceneWidget view;
        view.resize(320,240);
        auto scene=std::make_shared<Scene>();
        const auto mesh=std::make_shared<const MeshData>(coloredCube());
        QVERIFY(scene->setMesh(scene->createNode(),mesh));
        view.setScene(scene);
        LightingSettings settings;
        settings.ambientOcclusion=true;
        view.setLighting(settings);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(!view.grabFramebuffer().isNull());
        const auto visibleDraws=view.statistics().drawCalls;
        const auto offscreen=scene->createNode();
        QVERIFY(scene->setMesh(offscreen,mesh));
        Transform pose;
        pose.translation={100,0,0};
        QVERIFY(scene->setTransform(offscreen,pose));
        QVERIFY(!view.grabFramebuffer().isNull());
        QCOMPARE(view.statistics().visibleItems,1);
        QCOMPARE(view.statistics().drawCalls,visibleDraws);
        settings.shadows=true;
        view.setLighting(settings);
        QVERIFY(!view.grabFramebuffer().isNull());
        // Both meshes must still be submitted to the light's shadow pass.
        QCOMPARE(view.statistics().drawCalls,visibleDraws+2);
        QVERIFY(view.lastError().isEmpty());
    }

    void keyLightDirectionFollowsCamera()
    {
        RhiSceneWidget view; view.resize(320,240);
        auto scene=std::make_shared<Scene>();
        auto material=std::make_shared<Material>();
        material->shading=Material::Shading::MetallicRoughness;
        material->baseColor={0.25f,0.25f,0.25f,1}; material->roughness=0.9f;
        auto cube=coloredCube();
        for(auto &vertex:cube.vertices) vertex.color={1,1,1};
        scene->setMesh(scene->createNode(),std::make_shared<MeshData>(cube),material);
        view.setScene(scene);
        LightingSettings light; light.keyLightDirection={0,0,1};
        view.setLighting(light); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto center=[&] {const auto image=view.grabFramebuffer(); return brightness(image.pixelColor(image.width()/2,image.height()/2));};
        const int front=center();
        light.keyLightDirection={0,0,-1}; view.setLighting(light);
        const int back=center(); QVERIFY(front>back+25);
        auto camera=view.camera(); camera.yaw=180; view.setCamera(camera);
        light.keyLightDirection={0,0,1}; view.setLighting(light);
        const int fixed=center();
        light.keyLightCameraRelative=true; view.setLighting(light);
        QVERIFY(center()>fixed+25);
        QVERIFY(view.lastError().isEmpty());
    }

    void hdrDecodeConvolutionAndInvalidInput()
    {
        QTemporaryDir temp(QDir::currentPath()+"/lighting-XXXXXX");
        QVERIFY(temp.isValid());
        QString error;
        const auto map=constantEnvironment(temp.path(),&error);
        QVERIFY2(map,qPrintable(error));
        QCOMPARE(map->radiance.size(),QSize(8,4));
        QCOMPARE(channel(map->radiance,0),2.0f);
        QCOMPARE(channel(map->radiance,1),1.0f);
        QCOMPARE(channel(map->radiance,2),0.5f);
        QVERIFY(std::abs(channel(map->irradiance,0)-2.0f)<0.001f);
        QCOMPARE(map->specular.size(),std::size_t(8));
        for(const auto &image:map->specular) QVERIFY(std::abs(channel(image,1)-1.0f)<0.001f);
        QVERIFY(channel(map->brdf,0)>=0 && channel(map->brdf,0)<=1);
        QFile corrupt(temp.filePath("corrupt.hdr"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("invalid"); corrupt.close();
        QVERIFY(!EnvironmentMap::loadHdr(corrupt.fileName(),&error)); QVERIFY(!error.isEmpty());
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n");
        corrupt.write(QByteArray::fromHex("0202000880")); corrupt.close();
        QVERIFY(!EnvironmentMap::loadHdr(corrupt.fileName(),&error));
        QVERIFY(error.contains("HDR"));
    }
    void environmentBackgroundIblAndExposure()
    {
        QVERIFY(environment());
        QWidget first,second;
        QVBoxLayout firstLayout(&first),secondLayout(&second);
        RhiSceneWidget view;
        view.resize(320,240); view.setEnvironment(environment());
        LightingSettings settings;
        settings.environmentBackground=settings.toneMapping=true;
        view.setLighting(settings); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames>0);
        const auto background=view.grabFramebuffer();
        const auto color=background.pixelColor(background.width()/2,background.height()/2);
        QVERIFY(color.red()>color.green() && color.green()>color.blue());
        QVERIFY(color.red()>240);
        settings.exposure=0.25f; view.setLighting(settings);
        const auto darker=view.grabFramebuffer();
        QVERIFY(brightness(darker.pixelColor(darker.width()/2,darker.height()/2))<brightness(color)-20);
        settings.toneMapping=false; settings.exposure=0.1f; view.setLighting(settings);
        const auto raw=view.grabFramebuffer();
        const auto rawColor=raw.pixelColor(raw.width()/2,raw.height()/2);
        QVERIFY(std::abs(rawColor.red()-124)<=3); // linear 2.0 * exposure .1, encoded as sRGB
        settings.toneMapping=true;
        auto scene=std::make_shared<Scene>();
        auto mesh=std::make_shared<MeshData>(coloredCube());
        for(auto &vertex:mesh->vertices) vertex.color={1,1,1};
        auto material=std::make_shared<Material>();
        material->shading=Material::Shading::MetallicRoughness; material->metallic=1;
        material->roughness=0.15f; material->doubleSided=true;
        scene->setMesh(scene->createNode(),mesh,material); view.setScene(scene);
        settings.environmentBackground=false; settings.exposure=1; settings.imageBasedLighting=false;
        view.setLighting(settings); const auto noIbl=view.grabFramebuffer();
        settings.imageBasedLighting=true; view.setLighting(settings); const auto ibl=view.grabFramebuffer();
        const auto before=noIbl.pixelColor(noIbl.width()/2,noIbl.height()/2);
        const auto after=ibl.pixelColor(ibl.width()/2,ibl.height()/2);
        QVERIFY2(after.red()>before.red()+30,qPrintable(QString("IBL %1 -> %2").arg(before.name(),after.name())));
        QVERIFY(after.red()>after.blue()+10);
        view.setFixedSize(320,240);
        view.setParent(&first); firstLayout.addWidget(&view); first.show(); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(!view.grabFramebuffer().isNull());
        const auto generation=view.statistics().resourceGenerations;
        view.setParent(&second); secondLayout.addWidget(&view); second.show(); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        const auto recreated=view.grabFramebuffer();
        QVERIFY(view.statistics().resourceGenerations>generation);
        const auto recreatedColor=recreated.pixelColor(recreated.width()/2,recreated.height()/2);
        QVERIFY(recreatedColor.red()>recreatedColor.blue()+10);
        view.setEnvironment({});
        QVERIFY(!view.grabFramebuffer().isNull());
        view.setEnvironment(environment());
        QVERIFY(!view.grabFramebuffer().isNull());
        QVERIFY(view.lastError().isEmpty());
    }
    void environmentRotationMovesBackgroundAndIbl()
    {
        QVERIFY(environment());
        auto map=std::make_shared<EnvironmentMap>(*environment());
        const auto hemispheres=[](QImage &image) {
            for(int y=0;y<image.height();++y) {
                auto *row=reinterpret_cast<float *>(image.scanLine(y));
                for(int x=0;x<image.width();++x) {
                    row[4*x]=x<image.width()/2?2.0f:0.05f;
                    row[4*x+1]=0.05f;
                    row[4*x+2]=x<image.width()/2?0.05f:2.0f;
                    row[4*x+3]=1;
                }
            }
        };
        hemispheres(map->radiance); hemispheres(map->irradiance);
        for(auto &image:map->specular) hemispheres(image);
        RhiSceneWidget view;
        view.resize(320,240); view.setEnvironment(map);
        LightingSettings settings; settings.environmentBackground=settings.toneMapping=true;
        view.setLighting(settings); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto center=[&] {
            const auto image=view.grabFramebuffer();
            return image.pixelColor(image.width()/2,image.height()/2);
        };
        const auto original=center();
        settings.environmentRotation=180; view.setLighting(settings);
        const auto rotated=center();
        QVERIFY(original.red()>original.blue()+80);
        QVERIFY(rotated.blue()>rotated.red()+80);
        settings.environmentRotation=360; view.setLighting(settings);
        const auto fullCircle=center();
        QVERIFY(std::abs(fullCircle.red()-original.red())<=2);
        QVERIFY(std::abs(fullCircle.blue()-original.blue())<=2);
        auto scene=std::make_shared<Scene>();
        auto mesh=std::make_shared<MeshData>(coloredCube());
        for(auto &vertex:mesh->vertices) vertex.color={1,1,1};
        auto material=std::make_shared<Material>();
        material->shading=Material::Shading::MetallicRoughness;
        material->metallic=1; material->roughness=0.1f;
        scene->setMesh(scene->createNode(),mesh,material); view.setScene(scene);
        settings.environmentBackground=false; settings.imageBasedLighting=true; settings.environmentRotation=0;
        view.setLighting(settings); const auto reflection=center();
        const auto uploads=view.statistics().geometryUploads;
        settings.environmentRotation=180; view.setLighting(settings);
        const auto rotatedReflection=center();
        QVERIFY(reflection.blue()>reflection.red()+50);
        QVERIFY(rotatedReflection.red()>rotatedReflection.blue()+50);
        QCOMPARE(view.statistics().geometryUploads,uploads);
        QVERIFY(view.lastError().isEmpty());
    }

    void aoAndDirectionalCastShadow()
    {
        RhiSceneWidget view; view.resize(640,480);
        auto scene=std::make_shared<Scene>();
        auto floor=std::make_shared<MeshData>(); floor->vertexColorsSrgb=false;
        floor->vertices={{{-1,0,1},{1,1,1},{0,1,0}},{{1,0,1},{1,1,1},{0,1,0}},
            {{1,0,-1},{1,1,1},{0,1,0}},{{-1,0,-1},{1,1,1},{0,1,0}}};
        floor->indices={0,1,2,0,2,3};
        auto material=std::make_shared<Material>(); material->shading=Material::Shading::MetallicRoughness;
        material->doubleSided=true; material->baseColor={0.6f,0.6f,0.6f,1};
        scene->setMesh(scene->createNode(),floor,material);
        auto cube=std::make_shared<MeshData>(coloredCube());
        for(auto &vertex:cube->vertices) vertex.color={1,1,1};
        const auto blocker=scene->createNode(); scene->setMesh(blocker,cube,material);
        Transform transform; transform.scale={0.18f,0.18f,0.18f}; transform.translation={0,0.18f,0};
        scene->setTransform(blocker,transform); view.setScene(scene);
        Camera camera; camera.target={0,0.12f,0}; camera.distance=2.3f; camera.pitch=-35; view.setCamera(camera);
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.statistics().submittedFrames>0);
        const auto unshadowed=view.grabFramebuffer();
        LightingSettings settings; settings.shadows=true; view.setLighting(settings);
        const auto shadowed=view.grabFramebuffer();
        const QVector3D shadowPoint(-0.28f,0,-0.25f);
        const auto before=at(unshadowed,view,shadowPoint),after=at(shadowed,view,shadowPoint);
        QVERIFY2(brightness(after)<brightness(before)-20,qPrintable(QString("shadow %1 -> %2").arg(before.name(),after.name())));
        // Moving the occluder must move/remove the shadow, not bake it into a material.
        transform.translation.setX(0.65f); scene->setTransform(blocker,transform); view.update();
        const auto moved=view.grabFramebuffer();
        QVERIFY(brightness(at(moved,view,shadowPoint))>brightness(after)+20);
        transform.translation.setX(0); scene->setTransform(blocker,transform); view.update();
        settings.shadows=false; settings.ambientOcclusion=true; settings.aoRadius=0.2f; settings.aoStrength=2; view.setLighting(settings);
        const auto ao=view.grabFramebuffer();
        int darkening=0;
        for(float x=0.20f;x<0.40f;x+=0.02f) for(float z=0.08f;z<0.3f;z+=0.02f)
            darkening=std::max(darkening,brightness(at(unshadowed,view,{x,0,z}))-brightness(at(ao,view,{x,0,z})));
        QVERIFY2(darkening>=8,qPrintable(QString("AO contact darkening %1").arg(darkening)));
        QVERIFY(std::abs(brightness(at(unshadowed,view,{0.7f,0,0.6f}))-brightness(at(ao,view,{0.7f,0,0.6f})))<=3);
        const auto uploads=view.statistics().geometryUploads;
        view.resize(500,360); view.setSampleCount(4); settings.shadows=true; view.setLighting(settings);
        QVERIFY(!view.grabFramebuffer().isNull());
        QCOMPARE(view.statistics().geometryUploads,uploads);
        const auto effectBytes=view.statistics().textureBytes;
        settings.shadows=settings.ambientOcclusion=false; view.setLighting(settings);
        QVERIFY(!view.grabFramebuffer().isNull());
        QVERIFY(view.statistics().textureBytes<effectBytes);
        QVERIFY(view.lastError().isEmpty());
    }
};
QTEST_MAIN(LightingTest)
#include "LightingTest.moc"
