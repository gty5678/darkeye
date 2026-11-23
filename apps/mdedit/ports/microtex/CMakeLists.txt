cmake_minimum_required(VERSION 3.24)
project(MicroTeX LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Qt6 6.5 CONFIG REQUIRED COMPONENTS Widgets)
find_package(tinyxml2 CONFIG REQUIRED)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS
  src/atom/*.cpp src/box/*.cpp src/core/*.cpp
  src/fonts/*.cpp src/utils/*.cpp src/res/*.cpp)
# Upstream does not export DLL symbols; MicroTeX itself is always static.
add_library(MicroTeX STATIC ${sources} src/latex.cpp src/render.cpp src/platform/qt/graphic_qt.cpp)
set_target_properties(MicroTeX PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(MicroTeX PUBLIC cxx_std_17)
target_include_directories(MicroTeX PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/microtex>)
target_compile_definitions(MicroTeX PUBLIC BUILD_QT PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_link_libraries(MicroTeX PUBLIC Qt6::Widgets PRIVATE tinyxml2::tinyxml2)
if(MSVC)
  target_compile_options(MicroTeX PRIVATE /utf-8)
endif()
install(TARGETS MicroTeX EXPORT MicroTeXTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/microtex FILES_MATCHING PATTERN "*.h")
install(DIRECTORY res DESTINATION ${CMAKE_INSTALL_DATADIR}/microtex)
install(EXPORT MicroTeXTargets NAMESPACE MicroTeX:: DESTINATION share/microtex)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/MicroTeXConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/MicroTeXConfig.cmake" INSTALL_DESTINATION share/microtex)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MicroTeXConfig.cmake" DESTINATION share/microtex)
