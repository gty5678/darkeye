file(GLOB_RECURSE DARKEYE_DOCKING_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/myads/*.h"
)
file(GLOB_RECURSE DARKEYE_DOCKING_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/myads/*.cpp"
)

qt_add_library(darkeye_docking STATIC
    ${DARKEYE_DOCKING_HEADERS}
    ${DARKEYE_DOCKING_SOURCES}
)
add_library(darkeye::docking ALIAS darkeye_docking)
target_include_directories(darkeye_docking PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
)
target_link_libraries(darkeye_docking PUBLIC
    darkeye::core
    darkeye::layouts
    darkeye::widgets
    Qt6::Widgets
)
darkeye_apply_compiler_options(darkeye_docking)
