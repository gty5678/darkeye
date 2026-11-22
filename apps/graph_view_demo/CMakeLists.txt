qt_add_executable(darkeye_graph_view_demo WIN32
    main.cpp
    GraphScenarios.cpp
    GraphScenarios.h
    GraphTestControls.cpp
    GraphTestControls.h
)
set_target_properties(darkeye_graph_view_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(darkeye_graph_view_demo PRIVATE darkeye::graph_view)
darkeye_apply_compiler_options(darkeye_graph_view_demo)
darkeye_configure_packaging(darkeye_graph_view_demo NO_INSTALL)
