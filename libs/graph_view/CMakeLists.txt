file(GLOB_RECURSE DARKEYE_GRAPH_VIEW_HEADERS CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE DARKEYE_GRAPH_VIEW_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

qt_add_library(darkeye_graph_view STATIC
    ${DARKEYE_GRAPH_VIEW_HEADERS}
    ${DARKEYE_GRAPH_VIEW_SOURCES}
)
set_property(TARGET darkeye_graph_view PROPERTY AUTOMOC ON)
add_library(darkeye::graph_view ALIAS darkeye_graph_view)
target_include_directories(darkeye_graph_view PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/graph_view>"
)
qt_add_shaders(darkeye_graph_view forceview_rhi_shaders
    PREFIX "/forceview/shaders"
    FILES
        shaders/force_rhi_color.vert
        shaders/force_rhi_instance.vert
        shaders/force_rhi_color.frag
        shaders/force_rhi_text.vert
        shaders/force_rhi_text.frag
)
qt_add_shaders(darkeye_graph_view forceview_compute_shaders
    PREFIX "/forceview/shaders"
    GLSL "430,310es"
    HLSL 50
    MSL 12
    FILES shaders/force_simulation.comp shaders/force_instances.comp shaders/force_text_positions.comp
)

target_link_libraries(darkeye_graph_view
    PUBLIC
        darkeye::graph
        Qt6::Widgets
    PRIVATE
        Qt6::GuiPrivate
        msdfgen::msdfgen-core
        msdfgen::msdfgen-ext
        Freetype::Freetype
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(darkeye_graph_view PRIVATE OpenMP::OpenMP_CXX)
endif()
darkeye_apply_compiler_options(darkeye_graph_view)
