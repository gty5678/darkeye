add_subdirectory(colorwheel_demo)
add_subdirectory(myads_demo)
add_subdirectory(ui_snapshot)

set(_demo_native_environment
    "QT_QPA_PLATFORM=${DARKEYE_TEST_NATIVE_PLATFORM};QT_QPA_PLATFORM_PLUGIN_PATH=${DARKEYE_QT_PLUGIN_DIR}/platforms")
set(_demo_qt_path "PATH=path_list_prepend:${DARKEYE_QT_BIN_DIR}")

add_test(NAME darkeye_myads_demo_smoke COMMAND darkeye_myads_demo --smoke-test)
set_tests_properties(darkeye_myads_demo_smoke PROPERTIES
    ENVIRONMENT "${_demo_native_environment}"
    ENVIRONMENT_MODIFICATION "${_demo_qt_path}")

add_test(NAME darkeye_graph_view_demo_smoke
    COMMAND darkeye_graph_view_demo --backend gpu --nodes 127 --seed 42 --smoke-test)
add_test(NAME darkeye_graph_view_demo_motion_smoke
    COMMAND darkeye_graph_view_demo --backend gpu --nodes 400 --text-motion-smoke)
set_tests_properties(darkeye_graph_view_demo_smoke darkeye_graph_view_demo_motion_smoke PROPERTIES
    ENVIRONMENT "${_demo_native_environment}"
    ENVIRONMENT_MODIFICATION "${_demo_qt_path}"
    TIMEOUT 10)
foreach(scenario IN ITEMS communities hubs lattice star islands overlap)
    add_test(NAME darkeye_graph_view_demo_scenario_${scenario}
        COMMAND darkeye_graph_view_demo --backend gpu --nodes 127 --preset ${scenario} --seed 42 --smoke-test)
    set_tests_properties(darkeye_graph_view_demo_scenario_${scenario} PROPERTIES
        ENVIRONMENT "${_demo_native_environment}"
        ENVIRONMENT_MODIFICATION "${_demo_qt_path}"
        TIMEOUT 15)
endforeach()

set(_graph_demo_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../apps/graph_view_demo")
qt_add_executable(darkeye_graph_demo_data_tests ../unit/GraphDemoDataTest.cpp "${_graph_demo_dir}/GraphScenarios.cpp")
target_include_directories(darkeye_graph_demo_data_tests PRIVATE "${_graph_demo_dir}")
target_link_libraries(darkeye_graph_demo_data_tests PRIVATE Qt6::Gui Qt6::Test)
darkeye_apply_compiler_options(darkeye_graph_demo_data_tests)
add_test(NAME darkeye_graph_demo_data_tests COMMAND darkeye_graph_demo_data_tests)
set_tests_properties(darkeye_graph_demo_data_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${_demo_qt_path}" TIMEOUT 30)

add_test(NAME darkeye_component_gallery_smoke COMMAND darkeye_component_gallery --smoke-test)
set_tests_properties(darkeye_component_gallery_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${DARKEYE_QT_PLUGIN_DIR}/platforms"
    ENVIRONMENT_MODIFICATION "${_demo_qt_path}")
