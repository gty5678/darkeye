cmake_minimum_required(VERSION 3.25)

project(Darkeye VERSION 1.2.5 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(DARKEYE_BUILD_TESTS "Build Darkeye tests" OFF)
option(DARKEYE_BUILD_GRAPH_VIEW_DEMO "Build the standalone force-directed graph demo" ON)
option(DARKEYE_BUILD_RENDER3D_DEMO "Build the standalone QRhi 3D demos" ON)
option(DARKEYE_DEPLOY_QT_RUNTIME
       "Deploy Qt runtime beside Darkeye after each Windows build" ON)

include(CompilerOptions)
include(Dependencies)
include(Packaging)

add_subdirectory(libs)
add_subdirectory(apps)

if(DARKEYE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
