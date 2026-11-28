#include "DvdSceneResources.h"
#include "dvd_scene/DvdModelAsset.h"

#include <QResource>

// Q_INIT_RESOURCE must resolve the generated symbol in the global namespace.
static void registerSceneResources()
{
    Q_INIT_RESOURCE(dvd_scene_defaults);
}

namespace darkeye::dvd_scene {

void initializeSceneResources()
{
    static const bool initialized = [] {
        registerSceneResources();
        return true;
    }();
    Q_UNUSED(initialized);
}

render3d::ModelLoadResult loadDvdModel(render3d::GltfLoader &loader)
{
    initializeSceneResources();
    return loadDvdModel(loader, QStringLiteral(":/dvd_scene/dvd.glb"));
}

} // namespace darkeye::dvd_scene
