qt_add_library(darkeye_dvd_scene STATIC
    include/dvd_scene/DvdItemViewModel.h
    include/dvd_scene/DvdShelfController.h
    include/dvd_scene/DvdCaseFactory.h
    include/dvd_scene/CoverTextureCache.h
    include/dvd_scene/DvdInteractionController.h
    include/dvd_scene/DvdSceneWidget.h
    include/dvd_scene/DvdSceneAdapter.h
    include/dvd_scene/DvdModelAsset.h
    src/DvdShelfController.cpp
    src/DvdCaseFactory.cpp
    src/CoverTextureCache.cpp
    src/DvdInteractionController.cpp
    src/DvdSceneWidget.cpp
    src/DvdSceneAdapter.cpp
)
add_library(darkeye::dvd_scene ALIAS darkeye_dvd_scene)
qt_add_resources(darkeye_dvd_scene dvd_scene_defaults
    PREFIX "/dvd_scene" BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../resources"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/maps/0.png")
target_include_directories(darkeye_dvd_scene PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(darkeye_dvd_scene PUBLIC darkeye::render3d)
darkeye_apply_compiler_options(darkeye_dvd_scene)
