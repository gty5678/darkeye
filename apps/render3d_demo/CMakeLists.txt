# This directory is also a standalone Qt-only entry point. It does not find
# database, Quick3D, FreeType or msdfgen packages and does not build the app.
cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(DarkeyeRender3dDemo LANGUAGES CXX)
    get_filename_component(DARKEYE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    include("${DARKEYE_SOURCE_ROOT}/cmake/CompilerOptions.cmake")
    find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Gui Widgets GuiPrivate ShaderTools)
    qt_standard_project_setup(REQUIRES 6.12.0)
    include(CTest)
    if(BUILD_TESTING)
        find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Test)
    endif()
    add_subdirectory("${DARKEYE_SOURCE_ROOT}/libs/render3d" render3d)
    add_subdirectory("${DARKEYE_SOURCE_ROOT}/libs/dvd_scene" dvd_scene)
endif()

qt_add_executable(darkeye_render3d_demo main.cpp)
get_filename_component(_dvd_demo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
qt_add_resources(darkeye_render3d_demo dvd_demo_assets
    PREFIX "/dvd_demo" BASE "${_dvd_demo_root}/libs/dvd"
    FILES "${_dvd_demo_root}/libs/dvd/dvd.glb")
set_target_properties(darkeye_render3d_demo PROPERTIES
    WIN32_EXECUTABLE FALSE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(darkeye_render3d_demo PRIVATE darkeye::dvd_scene)
darkeye_apply_compiler_options(darkeye_render3d_demo)

qt_add_executable(darkeye_dvd_scene_demo dvd_demo.cpp)
set_target_properties(darkeye_dvd_scene_demo PROPERTIES
    WIN32_EXECUTABLE FALSE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
qt_add_resources(darkeye_dvd_scene_demo dvd_scene_demo_assets
    PREFIX "/dvd_scene_demo" BASE "${_dvd_demo_root}/libs/dvd"
    FILES "${_dvd_demo_root}/libs/dvd/dvd.glb")
qt_add_resources(darkeye_dvd_scene_demo dvd_lighting_assets
    PREFIX "/dvd_scene_demo" BASE "${_dvd_demo_root}/resources/hdr"
    FILES "${_dvd_demo_root}/resources/hdr/lebombo_1k.hdr")
target_link_libraries(darkeye_dvd_scene_demo PRIVATE darkeye::dvd_scene)
darkeye_apply_compiler_options(darkeye_dvd_scene_demo)

# Only deploy modules actually imported by this demo; no QML scan or production
# data/install rules. The executable stays console-enabled for diagnostics.
if(WIN32)
    get_target_property(_render3d_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_render3d_qt_bin "${_render3d_qmake}" DIRECTORY)
    unset(_render3d_deploy CACHE)
    find_program(_render3d_deploy windeployqt HINTS "${_render3d_qt_bin}"
        NO_DEFAULT_PATH NO_CACHE REQUIRED)
    foreach(_demo_target IN ITEMS darkeye_render3d_demo darkeye_dvd_scene_demo)
    add_custom_command(TARGET ${_demo_target} POST_BUILD
        COMMAND "${_render3d_deploy}"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --no-translations --no-compiler-runtime
            "$<TARGET_FILE:${_demo_target}>"
        VERBATIM)
    endforeach()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND BUILD_TESTING)
    add_subdirectory("${DARKEYE_SOURCE_ROOT}/tests/render3d" render3d_tests)
    add_subdirectory("${DARKEYE_SOURCE_ROOT}/tests/dvd_scene" dvd_scene_tests)
endif()
