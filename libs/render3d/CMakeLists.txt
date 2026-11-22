qt_add_library(darkeye_render3d STATIC
    include/render3d/Mesh.h
    include/render3d/RhiSceneWidget.h
    include/render3d/Transform.h
    include/render3d/Scene.h
    include/render3d/Camera.h
    include/render3d/Material.h
    include/render3d/ModelAsset.h
    include/render3d/Raycaster.h
    include/render3d/Animator.h
    include/render3d/GltfLoader.h
    include/render3d/Lighting.h
    src/assets/EnvironmentMap.cpp
    src/renderer/LightingResources.cpp
    src/renderer/LightingResources.h
    src/Mesh.cpp
    src/scene/Transform.cpp
    src/scene/Scene.cpp
    src/scene/Camera.cpp
    src/assets/ModelAsset.cpp
    src/interaction/Raycaster.cpp
    src/animation/Animator.cpp
    src/assets/Cgltf.cpp
    src/assets/GltfLoader.cpp
    src/renderer/Renderer.h
    src/renderer/Renderer.cpp
    src/renderer/RhiSceneWidget.cpp
)
add_library(darkeye::render3d ALIAS darkeye_render3d)
target_include_directories(darkeye_render3d PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_include_directories(darkeye_render3d SYSTEM PRIVATE third_party/cgltf)
target_link_libraries(darkeye_render3d
    PUBLIC Qt6::Gui Qt6::Widgets
    PRIVATE Qt6::GuiPrivate)
qt_add_shaders(darkeye_render3d render3d_shaders
    PREFIX "/render3d"
    GLSL "300es,330"
    DEPENDS shaders/lighting.glsl
    FILES shaders/unlit.vert shaders/unlit.frag shaders/present.vert shaders/present.frag shaders/copylinear.frag
        shaders/shadow.vert shaders/shadow.frag shaders/depthnormal.vert shaders/depthnormal.frag
        shaders/lightingfullscreen.vert shaders/environment.frag shaders/ssao.frag)
darkeye_apply_compiler_options(darkeye_render3d)
