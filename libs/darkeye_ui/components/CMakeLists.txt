set(DARKEYE_COLORWHEEL_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/ColorWheel.h")
set(DARKEYE_COLORWHEEL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ColorWheel.cpp")

qt_add_library(darkeye_colorwheel STATIC
    "${DARKEYE_COLORWHEEL_HEADER}"
    "${DARKEYE_COLORWHEEL_SOURCE}"
)
add_library(darkeye::colorwheel ALIAS darkeye_colorwheel)
target_include_directories(darkeye_colorwheel PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
)
target_link_libraries(darkeye_colorwheel PUBLIC Qt6::Widgets)
darkeye_apply_compiler_options(darkeye_colorwheel)

file(GLOB DARKEYE_WIDGET_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../base/*.h"
)
file(GLOB DARKEYE_WIDGET_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../base/*.cpp"
)
list(REMOVE_ITEM DARKEYE_WIDGET_HEADERS "${DARKEYE_COLORWHEEL_HEADER}")
list(REMOVE_ITEM DARKEYE_WIDGET_SOURCES "${DARKEYE_COLORWHEEL_SOURCE}")

qt_add_library(darkeye_widgets STATIC
    ${DARKEYE_WIDGET_HEADERS}
    ${DARKEYE_WIDGET_SOURCES}
)
add_library(darkeye::widgets ALIAS darkeye_widgets)
target_include_directories(darkeye_widgets PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
)
target_link_libraries(darkeye_widgets PUBLIC
    darkeye::colorwheel
    darkeye::layouts
    darkeye::theme
    Qt6::Svg
    Qt6::Widgets
)
darkeye_apply_compiler_options(darkeye_widgets)
