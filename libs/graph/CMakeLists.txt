file(GLOB_RECURSE DARKEYE_GRAPH_HEADERS CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE DARKEYE_GRAPH_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

qt_add_library(darkeye_graph STATIC
    ${DARKEYE_GRAPH_HEADERS}
    ${DARKEYE_GRAPH_SOURCES}
)
set_property(TARGET darkeye_graph PROPERTY AUTOMOC ON)
add_library(darkeye::graph ALIAS darkeye_graph)
target_include_directories(darkeye_graph PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_graph PUBLIC Qt6::Core Qt6::Sql)
darkeye_apply_compiler_options(darkeye_graph)
