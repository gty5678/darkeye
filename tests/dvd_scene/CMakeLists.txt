qt_add_executable(darkeye_dvd_scene_tests DvdSceneTest.cpp)
set_target_properties(darkeye_dvd_scene_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(darkeye_dvd_scene_tests PRIVATE darkeye::dvd_scene Qt6::Test)
get_filename_component(_dvd_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_compile_definitions(darkeye_dvd_scene_tests PRIVATE
    DARKEYE_DVD_SOURCE_DIR="${_dvd_source_root}")
darkeye_apply_compiler_options(darkeye_dvd_scene_tests)
get_target_property(_dvd_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_dvd_qt_bin "${_dvd_qmake}" DIRECTORY)
get_filename_component(_dvd_qt_prefix "${_dvd_qt_bin}" DIRECTORY)
foreach(scale IN ITEMS 1 1.5)
    add_test(NAME darkeye_dvd_scene_dpr_${scale} COMMAND darkeye_dvd_scene_tests
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/dvd-scene-${scale}.txt,txt")
    set_tests_properties(darkeye_dvd_scene_dpr_${scale} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
        ENVIRONMENT "TEMP=${CMAKE_CURRENT_BINARY_DIR};TMP=${CMAKE_CURRENT_BINARY_DIR};QT_SCALE_FACTOR=${scale};QT_QPA_PLATFORM_PLUGIN_PATH=${_dvd_qt_prefix}/plugins;DARKEYE_DVD_ARTIFACTS=${CMAKE_CURRENT_BINARY_DIR}/artifacts-${scale}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_dvd_qt_bin}")
endforeach()
add_test(NAME darkeye_dvd_scene_demo_smoke
    COMMAND $<TARGET_FILE:darkeye_dvd_scene_demo> --count 1000 --smoke-test)
set_tests_properties(darkeye_dvd_scene_demo_smoke PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${_dvd_qt_prefix}/plugins;QT_FORCE_STDERR_LOGGING=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_dvd_qt_bin}")
