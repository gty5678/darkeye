add_subdirectory(infra)
add_subdirectory(domain)
add_subdirectory(resources)
add_subdirectory(gui_utils)
add_subdirectory(bridge)
add_subdirectory(cover_maker)
add_subdirectory(database)
add_subdirectory(services)
add_subdirectory(graph)
add_subdirectory(graph_view)
add_subdirectory(render3d)
add_subdirectory(dvd_scene)
add_subdirectory(darkeye_ui)
set(MDEDIT_LITE ON)
add_subdirectory(mdedit)
add_subdirectory(ui)

# Complete application facade used by executables and integration tests.
add_library(darkeye_app INTERFACE)
add_library(darkeye::app ALIAS darkeye_app)
target_link_libraries(darkeye_app INTERFACE
    darkeye::infra
    darkeye::domain
    darkeye::resources
    darkeye::gui_utils
    darkeye::database
    darkeye::services
    darkeye::graph
    darkeye::graph_view
    darkeye::darkeye_ui
    darkeye::docking
    darkeye::ui
)
