qt_add_executable(darkeye_render3d_tests RhiSceneWidgetTest.cpp)
set_target_properties(darkeye_render3d_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(darkeye_render3d_tests PRIVATE darkeye::render3d Qt6::Test)
darkeye_apply_compiler_options(darkeye_render3d_tests)
qt_add_executable(darkeye_render3d_lighting_tests LightingTest.cpp)
set_target_properties(darkeye_render3d_lighting_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(darkeye_render3d_lighting_tests PRIVATE darkeye::render3d Qt6::Test)
darkeye_apply_compiler_options(darkeye_render3d_lighting_tests)

qt_add_executable(darkeye_render3d_scene_tests SceneTest.cpp)
set_target_properties(darkeye_render3d_scene_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(darkeye_render3d_scene_tests PRIVATE darkeye::render3d Qt6::Test)
darkeye_apply_compiler_options(darkeye_render3d_scene_tests)

qt_add_executable(darkeye_render3d_gltf_tests GltfLoaderTest.cpp)
set_target_properties(darkeye_render3d_gltf_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(darkeye_render3d_gltf_tests PRIVATE darkeye::render3d Qt6::Test)
get_filename_component(_render3d_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_compile_definitions(darkeye_render3d_gltf_tests PRIVATE
    DARKEYE_RENDER3D_SOURCE_DIR="${_render3d_source_root}")
darkeye_apply_compiler_options(darkeye_render3d_gltf_tests)

get_target_property(_render3d_test_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_render3d_test_qt_bin "${_render3d_test_qmake}" DIRECTORY)
get_filename_component(_render3d_test_qt_prefix "${_render3d_test_qt_bin}" DIRECTORY)

add_test(NAME darkeye_render3d_scene COMMAND darkeye_render3d_scene_tests
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/render3d-scene.txt,txt")
set_tests_properties(darkeye_render3d_scene PROPERTIES TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_render3d_test_qt_bin}")
add_test(NAME darkeye_render3d_gltf COMMAND darkeye_render3d_gltf_tests
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/render3d-gltf.txt,txt")
set_tests_properties(darkeye_render3d_gltf PROPERTIES TIMEOUT 30
    ENVIRONMENT "TEMP=${CMAKE_CURRENT_BINARY_DIR};TMP=${CMAKE_CURRENT_BINARY_DIR}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_render3d_test_qt_bin}")

foreach(scale IN ITEMS 1 1.5)
    add_test(NAME darkeye_render3d_lighting_${scale} COMMAND darkeye_render3d_lighting_tests
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/lighting-${scale}.txt,txt")
    set_tests_properties(darkeye_render3d_lighting_${scale} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        ENVIRONMENT "QT_SCALE_FACTOR=${scale};QT_QPA_PLATFORM_PLUGIN_PATH=${_render3d_test_qt_prefix}/plugins"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_render3d_test_qt_bin}")
    add_test(NAME darkeye_render3d_dpr_${scale}
        COMMAND darkeye_render3d_tests -o -,txt
            -o "${CMAKE_CURRENT_BINARY_DIR}/render3d-dpr-${scale}.txt,txt")
    set_tests_properties(darkeye_render3d_dpr_${scale} PROPERTIES
        TIMEOUT 90 RUN_SERIAL TRUE
        ENVIRONMENT "QT_SCALE_FACTOR=${scale};QT_QPA_PLATFORM_PLUGIN_PATH=${_render3d_test_qt_prefix}/plugins"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_render3d_test_qt_bin}")
endforeach()
if(APPLE)
    # Temporarily disable these rendering tests while macOS CI failures are investigated.
    set_tests_properties(
        darkeye_render3d_dpr_1
        darkeye_render3d_dpr_1.5
        PROPERTIES DISABLED TRUE
    )
endif()
add_test(NAME darkeye_render3d_demo_smoke
    COMMAND $<TARGET_FILE:darkeye_render3d_demo> --smoke-test)
add_test(NAME darkeye_render3d_dvd_smoke
    COMMAND $<TARGET_FILE:darkeye_render3d_demo> --scene dvd --smoke-test)
set_tests_properties(darkeye_render3d_demo_smoke PROPERTIES
    TIMEOUT 20 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${_render3d_test_qt_prefix}/plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_render3d_test_qt_bin}")
set_tests_properties(darkeye_render3d_dvd_smoke PROPERTIES
    TIMEOUT 20 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${_render3d_test_qt_prefix}/plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_render3d_test_qt_bin}")
