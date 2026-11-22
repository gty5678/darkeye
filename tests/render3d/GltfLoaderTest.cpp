#include "render3d/GltfLoader.h"
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace darkeye::render3d;

namespace {
struct Fixture {
    QJsonObject document;
    QByteArray binary;
};

bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

Fixture fixture()
{
    const float values[] = {-1,-1,0, 1,-1,0, 0,1,0, 0,0, 1,0, 0.5f,1};
    const quint16 indices[] = {0,1,2};
    Fixture result;
    result.binary.append(reinterpret_cast<const char *>(values), sizeof(values));
    result.binary.append(reinterpret_cast<const char *>(indices), sizeof(indices));
    result.document = {
        {"asset", QJsonObject{{"version", "2.0"}}},
        {"buffers", QJsonArray{QJsonObject{{"uri", "mesh.bin"}, {"byteLength", int(result.binary.size())}}}},
        {"bufferViews", QJsonArray{
            QJsonObject{{"buffer",0},{"byteOffset",0},{"byteLength",36}},
            QJsonObject{{"buffer",0},{"byteOffset",36},{"byteLength",24}},
            QJsonObject{{"buffer",0},{"byteOffset",60},{"byteLength",6}}}},
        {"accessors", QJsonArray{
            QJsonObject{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},
                        {"min",QJsonArray{-1,-1,0}}, {"max",QJsonArray{1,1,0}}},
            QJsonObject{{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC2"}},
            QJsonObject{{"bufferView",2},{"componentType",5123},{"count",3},{"type","SCALAR"}}}},
        {"meshes", QJsonArray{QJsonObject{{"primitives",QJsonArray{QJsonObject{
            {"attributes",QJsonObject{{"POSITION",0},{"TEXCOORD_0",1}}},{"indices",2},{"material",0}}}}}}},
        {"materials",QJsonArray{QJsonObject{{"name","test"},{"doubleSided",true},
            {"pbrMetallicRoughness",QJsonObject{{"baseColorFactor",QJsonArray{0.5,0.6,0.7,0.8}},
                {"metallicFactor",0.2},{"roughnessFactor",0.7},
                {"baseColorTexture",QJsonObject{{"index",0}}},
                {"metallicRoughnessTexture",QJsonObject{{"index",0}}}}}}}},
        {"textures",QJsonArray{QJsonObject{{"source",0},{"sampler",0}}}},
        {"samplers",QJsonArray{QJsonObject{{"magFilter",9728},{"minFilter",9729},{"wrapS",33071},{"wrapT",33648}}}},
        {"images",QJsonArray{QJsonObject{{"uri",QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("封面.png")))}}}},
        {"nodes",QJsonArray{QJsonObject{{"name","root"},{"translation",QJsonArray{1,2,3}},{"children",QJsonArray{1}}},
                            QJsonObject{{"name","mesh"},{"mesh",0},{"scale",QJsonArray{2,2,2}}}}},
        {"scenes",QJsonArray{QJsonObject{{"nodes",QJsonArray{0}}}}}, {"scene",0}
    };
    return result;
}

bool saveFixture(const QDir &directory, const Fixture &source, bool image = true)
{
    if (!write(directory.filePath("model.gltf"), QJsonDocument(source.document).toJson())
        || !write(directory.filePath("mesh.bin"), source.binary))
        return false;
    if (!image)
        return true;
    QImage cover(2,2,QImage::Format_RGBA8888);
    cover.setPixelColor(0,0,Qt::red);
    cover.setPixelColor(1,0,Qt::green);
    cover.setPixelColor(0,1,Qt::blue);
    cover.setPixelColor(1,1,Qt::yellow);
    return cover.save(directory.filePath(QStringLiteral("封面.png")));
}

QByteArray glb(QJsonObject document, QByteArray binary)
{
    document.insert("buffers",QJsonArray{QJsonObject{{"byteLength",int(binary.size())}}});
    auto json = QJsonDocument(document).toJson(QJsonDocument::Compact);
    while (json.size()%4) json.append(' ');
    while (binary.size()%4) binary.append(char(0));
    QByteArray result;
    QDataStream stream(&result,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x46546c67) << quint32(2) << quint32(12+8+json.size()+8+binary.size());
    stream << quint32(json.size()) << quint32(0x4e4f534a);
    stream.writeRawData(json.constData(),int(json.size()));
    stream << quint32(binary.size()) << quint32(0x004e4942);
    stream.writeRawData(binary.constData(),int(binary.size()));
    return result;
}
}

class GltfLoaderTest final : public QObject {
    Q_OBJECT
private slots:
    void transmissionIorAndVolume()
    {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QDir directory(temporary.path());
        auto source=fixture();
        auto material=source.document["materials"].toArray()[0].toObject();
        const QJsonObject extensions{
            {"KHR_materials_transmission",QJsonObject{{"transmissionFactor",0.95},{"transmissionTexture",QJsonObject{{"index",0}}}}},
            {"KHR_materials_ior",QJsonObject{{"ior",1.49}}},
            {"KHR_materials_volume",QJsonObject{{"thicknessFactor",0.0008},{"thicknessTexture",QJsonObject{{"index",0}}},
                {"attenuationColor",QJsonArray{0.8,0.9,1}},{"attenuationDistance",0.1}}}};
        material["extensions"]=extensions;
        source.document["materials"]=QJsonArray{material};
        source.document["extensionsRequired"]=QJsonArray{"KHR_materials_transmission","KHR_materials_ior","KHR_materials_volume"};
        QVERIFY(saveFixture(directory,source));
        GltfLoader loader;
        auto loaded=loader.load(directory.filePath("model.gltf"));
        QVERIFY2(loaded,qPrintable(loaded.error));
        const auto &m=*loaded.asset->nodes[1].primitives[0].material;
        QCOMPARE(m.transmission,0.95f); QCOMPARE(m.ior,1.49f); QCOMPARE(m.thickness,0.0008f);
        QCOMPARE(m.attenuationColor,QVector3D(0.8f,0.9f,1)); QCOMPARE(m.attenuationDistance,0.1f);
        QVERIFY(m.transmissionTexture && !m.transmissionTexture->srgb);
        QCOMPARE(m.transmissionTexture,m.thicknessTexture);
        QCOMPARE(m.thicknessTexture,m.metallicRoughnessTexture);
        for(const auto &warning:loaded.asset->warnings) QVERIFY(!warning.contains("material subset"));
        for(const auto &bad:QJsonArray{
                QJsonObject{{"KHR_materials_transmission",QJsonObject{{"transmissionFactor",1.1}}}},
                QJsonObject{{"KHR_materials_ior",QJsonObject{{"ior",0.9}}}},
                QJsonObject{{"KHR_materials_volume",QJsonObject{{"thicknessFactor",-1}}}},
                QJsonObject{{"KHR_materials_volume",QJsonObject{{"attenuationColor",QJsonArray{2,1,1}}}}}}) {
            material["extensions"]=bad;
            source.document["materials"]=QJsonArray{material};
            QVERIFY(saveFixture(directory,source)); loader.clearCache();
            QVERIFY(!loader.load(directory.filePath("model.gltf")));
        }
    }

    void normalOcclusionEmissiveAndTangents()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QDir directory(temporary.path());
        auto source=fixture();
        auto material=source.document["materials"].toArray()[0].toObject();
        material["normalTexture"]=QJsonObject{{"index",0},{"scale",0.4}};
        material["occlusionTexture"]=QJsonObject{{"index",0},{"strength",0.65}};
        material["emissiveTexture"]=QJsonObject{{"index",0}};
        material["emissiveFactor"]=QJsonArray{0.2,0.4,0.6};
        material["extensions"]=QJsonObject{{"KHR_materials_emissive_strength",QJsonObject{{"emissiveStrength",3}}}};
        source.document["materials"]=QJsonArray{material};
        source.document["extensionsUsed"]=QJsonArray{"KHR_materials_emissive_strength"};
        source.document["extensionsRequired"]=QJsonArray{"KHR_materials_emissive_strength"};
        while(source.binary.size()%4) source.binary.append(char(0));
        const int offset=int(source.binary.size());
        const float tangents[]{1,0,0,-1,1,0,0,-1,1,0,0,-1};
        source.binary.append(reinterpret_cast<const char *>(tangents),sizeof(tangents));
        auto views=source.document["bufferViews"].toArray();
        views.append(QJsonObject{{"buffer",0},{"byteOffset",offset},{"byteLength",int(sizeof(tangents))}});
        source.document["bufferViews"]=views;
        auto accessors=source.document["accessors"].toArray();
        accessors.append(QJsonObject{{"bufferView",3},{"componentType",5126},{"count",3},{"type","VEC4"}});
        source.document["accessors"]=accessors;
        auto mesh=source.document["meshes"].toArray()[0].toObject();
        auto primitive=mesh["primitives"].toArray()[0].toObject();
        auto attributes=primitive["attributes"].toObject(); attributes["TANGENT"]=3;
        primitive["attributes"]=attributes; mesh["primitives"]=QJsonArray{primitive};
        source.document["meshes"]=QJsonArray{mesh};
        auto buffer=source.document["buffers"].toArray()[0].toObject(); buffer["byteLength"]=int(source.binary.size());
        source.document["buffers"]=QJsonArray{buffer};
        QVERIFY(saveFixture(directory,source));
        GltfLoader loader;
        const auto loaded=loader.load(directory.filePath("model.gltf"));
        QVERIFY2(loaded,qPrintable(loaded.error));
        const auto &part=loaded.asset->nodes[1].primitives[0];
        const auto &m=*part.material;
        QCOMPARE(m.normalScale,0.4f); QCOMPARE(m.occlusionStrength,0.65f);
        QCOMPARE(m.emissiveFactor,QVector3D(0.2f,0.4f,0.6f)); QCOMPARE(m.emissiveStrength,3.0f);
        QVERIFY(m.normalTexture && !m.normalTexture->srgb);
        QCOMPARE(m.normalTexture.get(),m.metallicRoughnessTexture.get());
        QCOMPARE(m.occlusionTexture.get(),m.normalTexture.get());
        QCOMPARE(m.emissiveTexture.get(),m.baseColorTexture.get());
        QVERIFY(m.emissiveTexture->srgb);
        QVERIFY(part.mesh->hasTangents);
        QCOMPARE(part.mesh->vertices[0].tangent[3],-1.0f);
        for(const auto &warning:loaded.asset->warnings)
            QVERIFY(!warning.contains("material subset"));
        material["occlusionTexture"]=QJsonObject{{"index",0},{"strength",1.5}};
        source.document["materials"]=QJsonArray{material};
        QVERIFY(saveFixture(directory,source)); loader.clearCache();
        QVERIFY(!loader.load(directory.filePath("model.gltf")));
    }

    void nodeMatrixAndRequiredExtensions()
    {
        QTemporaryDir temporary;
        const QDir directory(temporary.path());
        QVERIFY(temporary.isValid());
        auto source=fixture();
        auto nodes=source.document["nodes"].toArray();
        QJsonArray matrix{2,0,0,0,0,3,0,0,0,0,4,0,1,2,3,1};
        nodes[0]=QJsonObject{{"name","root"},{"matrix",matrix},{"children",QJsonArray{1}}};
        source.document["nodes"]=nodes;
        QVERIFY(saveFixture(directory,source));
        GltfLoader loader;
        const auto loaded=loader.load(directory.filePath("model.gltf"));
        QVERIFY2(bool(loaded),qPrintable(loaded.error));
        QCOMPARE(loaded.asset->nodes[0].transform.scale,QVector3D(2,3,4));
        QCOMPARE(loaded.asset->nodes[0].transform.translation,QVector3D(1,2,3));
        matrix[4]=0.5;
        auto node=nodes[0].toObject(); node["matrix"]=matrix; nodes[0]=node;
        source.document["nodes"]=nodes;
        QVERIFY(saveFixture(directory,source));
        loader.clearCache();
        const auto shear=loader.load(directory.filePath("model.gltf"));
        QVERIFY(!shear);
        QVERIFY(shear.error.contains("Sheared"));
        source=fixture();
        source.document["extensionsRequired"]=QJsonArray{"KHR_draco_mesh_compression"};
        QVERIFY(saveFixture(directory,source));
        loader.clearCache();
        const auto compressed=loader.load(directory.filePath("model.gltf"));
        QVERIFY(!compressed);
        QVERIFY(compressed.error.contains("Unsupported required extension"));
    }

    void actualRepositoryDvd()
    {
        GltfLoader loader;
        const auto loaded = loader.load(QStringLiteral(DARKEYE_RENDER3D_SOURCE_DIR)
            + QStringLiteral("/apps/render3d_demo/assets/dvd-case.glb"));
        QVERIFY2(bool(loaded), qPrintable(loaded.error));
        QCOMPARE(loaded.asset->nodes.size(), std::size_t(5));
        std::size_t primitives = 0;
        for (const auto &node : loaded.asset->nodes) {
            primitives += node.primitives.size();
            for (const auto &primitive : node.primitives) {
                QVERIFY(primitive.mesh->validationError().isEmpty());
                QVERIFY(!primitive.mesh->vertices.empty());
            }
        }
        QCOMPARE(primitives, std::size_t(9));
        bool checkedDiscColor = false;
        for (const auto &node : loaded.asset->nodes) {
            for (const auto &primitive : node.primitives) {
                if (primitive.material->name != QStringLiteral("disc-transparent")) continue;
                const auto color = primitive.material->baseColor;
                QCOMPARE(color.x(), 1.0f);
                QCOMPARE(color.y(), 1.0f);
                QVERIFY(std::abs(color.z() - 0.603827f) < 0.002f);
                QCOMPARE(color.w(), 1.0f); // Original QML is ARGB, not RGBA.
                checkedDiscColor = true;
            }
        }
        QVERIFY(checkedDiscColor);
        QCOMPARE(loaded.asset->nodes[3].name, QStringLiteral("front"));
        QCOMPARE(loaded.asset->nodes[3].transform.translation.x(), 0.0134142f);
    }

    void externalUnicodeImagesHierarchyAndSharing()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QDir directory(temporary.filePath(QStringLiteral("模型 空间")));
        QVERIFY(QDir().mkpath(directory.path()));
        QVERIFY(saveFixture(directory,fixture()));
        GltfLoader loader;
        const auto loaded = loader.load(directory.filePath("model.gltf"));
        QVERIFY2(bool(loaded),qPrintable(loaded.error));
        QCOMPARE(loaded.asset->nodes.size(),std::size_t(2));
        const auto &primitive = loaded.asset->nodes[1].primitives[0];
        QCOMPARE(primitive.mesh->vertices.size(),std::size_t(3));
        QCOMPARE(primitive.mesh->vertices[0].normal[2],1.0f);
        QCOMPARE(primitive.mesh->vertices[0].uv[1],0.0f);
        QVERIFY(!primitive.mesh->vertexColorsSrgb);
        const auto &material = primitive.material;
        QCOMPARE(material->baseColor,QVector4D(0.5f,0.6f,0.7f,0.8f));
        QCOMPARE(material->roughness,0.7f);
        QVERIFY(material->baseColorTexture->srgb);
        QVERIFY(!material->metallicRoughnessTexture->srgb);
        QCOMPARE(material->baseColorTexture->image.pixelColor(0,0),QColor(Qt::red));
        QCOMPARE(material->baseColorTexture->image.pixelColor(0,1),QColor(Qt::blue));
        QCOMPARE(material->baseColorTexture->wrapU,TextureData::Wrap::ClampToEdge);
        QCOMPARE(material->baseColorTexture->wrapV,TextureData::Wrap::MirroredRepeat);
        Scene scene;
        const auto first = loaded.asset->instantiate(scene);
        const auto second = loaded.asset->instantiate(scene);
        QVERIFY(!first.empty() && !second.empty());
        QCOMPARE(scene.worldMatrix(first[1]).map(QVector3D()),QVector3D(1,2,3));
        const auto items = scene.renderItems();
        QCOMPARE(items[0].mesh.get(),items[1].mesh.get());
        const auto cached = loader.load(directory.filePath("./model.gltf"));
        QCOMPARE(cached.asset.get(),loaded.asset.get());
        QImage replacement(3,3,QImage::Format_RGBA8888);
        replacement.fill(Qt::cyan);
        QVERIFY(replacement.save(directory.filePath(QStringLiteral("封面.png"))));
        const auto refreshed = loader.load(directory.filePath("model.gltf"));
        QVERIFY2(bool(refreshed),qPrintable(refreshed.error));
        QVERIFY(refreshed.asset.get()!=loaded.asset.get());
        QCOMPARE(refreshed.asset->nodes[1].primitives[0].material->baseColorTexture->image.size(),QSize(3,3));
    }

    void embeddedGlbAndNonIndexedPrimitive()
    {
        QTemporaryDir temporary;
        auto source=fixture();
        auto meshes=source.document["meshes"].toArray();
        auto mesh=meshes[0].toObject();
        auto primitives=mesh["primitives"].toArray();
        auto primitive=primitives[0].toObject();
        primitive.remove("indices");
        primitives[0]=primitive;
        primitives.append(primitive);
        mesh["primitives"]=primitives;
        meshes[0]=mesh;
        source.document["meshes"]=meshes;
        QImage image(2,2,QImage::Format_RGBA8888);
        image.fill(Qt::green);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer,"PNG"));
        while(source.binary.size()%4)source.binary.append(char(0));
        auto views=source.document["bufferViews"].toArray();
        views.append(QJsonObject{{"buffer",0},{"byteOffset",int(source.binary.size())},{"byteLength",png.size()}});
        source.binary.append(png);
        source.document["bufferViews"]=views;
        source.document["images"]=QJsonArray{QJsonObject{{"bufferView",3},{"mimeType","image/png"}}};
        const auto path=temporary.filePath("embedded.glb");
        QVERIFY(write(path,glb(source.document,source.binary)));
        GltfLoader loader;
        const auto loaded=loader.load(path);
        QVERIFY2(bool(loaded),qPrintable(loaded.error));
        QCOMPARE(loaded.asset->nodes[1].primitives.size(),std::size_t(2));
        QCOMPARE(loaded.asset->nodes[1].primitives[0].mesh->indices,std::vector<std::uint32_t>({0,1,2}));
        QCOMPARE(loaded.asset->nodes[1].primitives[0].material->baseColorTexture->image.pixelColor(0,0),QColor(Qt::green));
    }

    void corruptDataAndMissingTexture()
    {
        QTemporaryDir temporary;
        const QDir directory(temporary.path());
        auto source=fixture();
        QVERIFY(saveFixture(directory,source,false));
        GltfLoader loader;
        const auto fallback=loader.load(directory.filePath("model.gltf"));
        QVERIFY2(bool(fallback),qPrintable(fallback.error));
        QVERIFY(!fallback.asset->warnings.empty());
        QCOMPARE(fallback.asset->nodes[1].primitives[0].material->baseColorTexture->image.pixelColor(0,0),QColor(Qt::magenta));
        source.binary[60]=char(99);
        QVERIFY(saveFixture(directory,source,false));
        loader.clearCache();
        const auto invalidIndex=loader.load(directory.filePath("model.gltf"));
        QVERIFY(!invalidIndex);
        QVERIFY(!invalidIndex.error.isEmpty());
        QVERIFY(write(directory.filePath("model.gltf"),"not glTF JSON"));
        QVERIFY(!loader.load(directory.filePath("model.gltf")));
        QVERIFY(!loader.load(directory.filePath("missing.glb")));
        const auto valid=fixture();
        auto truncated=glb(valid.document,valid.binary);
        truncated.chop(10);
        QVERIFY(write(directory.filePath("truncated.glb"),truncated));
        QVERIFY(!loader.load(directory.filePath("truncated.glb")));
    }
};
QTEST_GUILESS_MAIN(GltfLoaderTest)
#include "GltfLoaderTest.moc"
