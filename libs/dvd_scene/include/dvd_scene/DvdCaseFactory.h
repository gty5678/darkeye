#pragma once
#include "DvdItemViewModel.h"
#include "render3d/ModelAsset.h"
#include <map>

namespace darkeye::dvd_scene {
struct DvdCaseInstance {
    QString stableId;
    render3d::NodeId root=0, spine=0, front=0, disc=0;
    render3d::Transform home;
    render3d::Transform discHome;
    bool hasLocalVideo=false;
    bool expanded=false;
    std::vector<render3d::NodeId> primitives;
    std::vector<render3d::NodeId> coverPrimitives;
    std::vector<render3d::NodeId> spineCoverPrimitives;
    std::vector<render3d::NodeId> discCoverPrimitives;
};

class DvdCaseFactory {
public:
    explicit DvdCaseFactory(std::shared_ptr<const render3d::ModelAsset> asset);
    [[nodiscard]] static render3d::Transform shelfTransform(int index);
    [[nodiscard]] DvdCaseInstance create(render3d::Scene &scene,
        const DvdItemViewModel &item,int index,std::shared_ptr<const render3d::TextureData> cover) const;
    void setCover(render3d::Scene &scene,const DvdCaseInstance &instance,
                  std::shared_ptr<const render3d::TextureData> cover) const;
    void updateSparkle(render3d::Scene &scene,const DvdCaseInstance &instance,double time) const;
private:
    std::shared_ptr<const render3d::ModelAsset> m_asset;
};
} // namespace darkeye::dvd_scene
