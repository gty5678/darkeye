#include "dvd_scene/DvdCaseFactory.h"
#include "dvd_scene/DvdShelfController.h"

namespace darkeye::dvd_scene {
using namespace render3d;
DvdCaseFactory::DvdCaseFactory(std::shared_ptr<const ModelAsset> asset):m_asset(std::move(asset)) {}

void DvdCaseFactory::updateSparkle(Scene &scene,const DvdCaseInstance &instance,double time) const
{
    const float seed=float(qHash(instance.stableId)%65536);
    for(const auto id:instance.spineCoverPrimitives) {
        const auto *node=scene.node(id);
        if(!node || !node->material) continue;
        if(!instance.hasLocalVideo && node->material->sparkle.z()==0) continue;
        auto material=std::make_shared<Material>(*node->material);
        material->sparkle=instance.hasLocalVideo?QVector4D(float(time),seed,1,0):QVector4D();
        scene.setMesh(id,node->mesh,std::move(material));
    }
}

Transform DvdCaseFactory::shelfTransform(int index)
{
    // Preserve the QML model pivot for both creation and later layout updates.
    Transform transform;
    transform.translation={index*DvdShelfController::spacing+0.006707f,0,0.000293f};
    return transform;
}

DvdCaseInstance DvdCaseFactory::create(Scene &scene,const DvdItemViewModel &item,
    int index,std::shared_ptr<const TextureData> cover) const
{
    DvdCaseInstance result;
    if (!m_asset) return result;
    result.stableId=item.stableId;
    result.hasLocalVideo=item.hasLocalVideo;
    result.home=shelfTransform(index);
    result.root=scene.createNode(0,item.stableId);
    scene.setTransform(result.root,result.home);
    std::vector<NodeId> nodes;
    for (const auto &source:m_asset->nodes) {
        // GltfLoader outputs validated pre-order nodes.
        if (source.parent>=int(nodes.size())) {
            scene.removeNode(result.root);
            return {};
        }
        const auto id=scene.createNode(source.parent<0?result.root:nodes[source.parent],source.name);
        scene.setTransform(id,source.transform);
        nodes.push_back(id);
        if (source.name=="spine") result.spine=id;
        if (source.name=="front") result.front=id;
        if (source.name=="disc") {
            result.disc=id;
            result.discHome=source.transform;
            auto pose=result.discHome;
            pose.rotation=QQuaternion::fromAxisAndAngle(1,0,0,item.hasLocalVideo?0:30)*pose.rotation;
            scene.setTransform(id,pose);
            scene.setVisible(id,false);
        }
        for (const auto &primitive:source.primitives) {
            const auto part=scene.createNode(id);
            auto material=primitive.material;
            auto mesh=primitive.mesh;
            if (material && material->name=="cover") {
                auto copy=std::make_shared<Material>(*material);
                copy->baseColorTexture=cover;
                material=copy;
                result.coverPrimitives.push_back(part);
                if(source.name=="spine") result.spineCoverPrimitives.push_back(part);
            }
            if(material && material->name=="disc-cover") {
                auto copy=std::make_shared<Material>(*material);
                copy->baseColorTexture=cover;
                material=std::move(copy);
                result.discCoverPrimitives.push_back(part);
            }
            scene.setMesh(part,std::move(mesh),std::move(material));
            result.primitives.push_back(part);
        }
    }
    return result;
}

void DvdCaseFactory::setCover(Scene &scene,const DvdCaseInstance &instance,
                             std::shared_ptr<const TextureData> cover) const
{
    for (const auto id:instance.coverPrimitives) {
        const auto *node=scene.node(id);
        if (!node || !node->material) continue;
        auto material=std::make_shared<Material>(*node->material);
        material->baseColorTexture=cover;
        scene.setMesh(id,node->mesh,std::move(material));
    }
    for(const auto id:instance.discCoverPrimitives) {
        const auto *node=scene.node(id);
        if(!node || !node->material || !node->mesh) continue;
        auto material=std::make_shared<Material>(*node->material);
        material->baseColorTexture=cover;
        scene.setMesh(id,node->mesh,std::move(material));
    }
}
} // namespace darkeye::dvd_scene
