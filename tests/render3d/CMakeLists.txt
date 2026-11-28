darkeye_add_test_suite(darkeye_render3d_tests
    LIBRARIES darkeye::render3d
    SOURCES RhiSceneWidgetTest.cpp LightingTest.cpp SceneTest.cpp GltfLoaderTest.cpp)
target_compile_definitions(darkeye_render3d_tests PRIVATE
    DARKEYE_RENDER3D_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_test(NAME darkeye_render3d_scene COMMAND darkeye_render3d_tests SceneTest
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/render3d-scene.txt,txt")
set_tests_properties(darkeye_render3d_scene PROPERTIES TIMEOUT 30
    LABELS darkeye_render3d_tests ENVIRONMENT_MODIFICATION "${test_qt_path}")
add_test(NAME darkeye_render3d_gltf COMMAND darkeye_render3d_tests GltfLoaderTest
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/render3d-gltf.txt,txt")
set_tests_properties(darkeye_render3d_gltf PROPERTIES TIMEOUT 30
    ENVIRONMENT "TEMP=${CMAKE_CURRENT_BINARY_DIR};TMP=${CMAKE_CURRENT_BINARY_DIR}"
    LABELS darkeye_render3d_tests ENVIRONMENT_MODIFICATION "${test_qt_path}")

foreach(scale IN ITEMS 1 1.5)
    add_test(NAME darkeye_render3d_lighting_${scale} COMMAND darkeye_render3d_tests LightingTest
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/lighting-${scale}.txt,txt")
    add_test(NAME darkeye_render3d_dpr_${scale} COMMAND darkeye_render3d_tests RhiSceneWidgetTest
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/render3d-dpr-${scale}.txt,txt")
    set_tests_properties(darkeye_render3d_lighting_${scale} darkeye_render3d_dpr_${scale} PROPERTIES
        TIMEOUT 90 RUN_SERIAL TRUE LABELS darkeye_render3d_tests
        ENVIRONMENT "QT_SCALE_FACTOR=${scale};QT_QPA_PLATFORM_PLUGIN_PATH=${DARKEYE_QT_PLUGIN_DIR}"
        ENVIRONMENT_MODIFICATION "${test_qt_path}")
endforeach()
if(TARGET darkeye_render3d_demo)
    add_test(NAME darkeye_render3d_demo_smoke COMMAND darkeye_render3d_demo --smoke-test)
    add_test(NAME darkeye_render3d_dvd_smoke COMMAND darkeye_render3d_demo --scene dvd --smoke-test)
    set_tests_properties(darkeye_render3d_demo_smoke darkeye_render3d_dvd_smoke PROPERTIES
        TIMEOUT 20 RUN_SERIAL TRUE
        ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${DARKEYE_QT_PLUGIN_DIR}"
        ENVIRONMENT_MODIFICATION "${test_qt_path}")
endif()
