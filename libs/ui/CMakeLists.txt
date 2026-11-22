add_subdirectory(layouts)

file(GLOB_RECURSE DARKEYE_UI_HEADERS CONFIGURE_DEPENDS
    "pages/*.h"
    "dialogs/*.h"
    "components/*.h"
)
file(GLOB_RECURSE DARKEYE_UI_SOURCES CONFIGURE_DEPENDS
    "pages/*.cpp"
    "dialogs/*.cpp"
    "components/*.cpp"
)
qt_add_library(darkeye_business_ui STATIC
    ${DARKEYE_UI_HEADERS}
    ${DARKEYE_UI_SOURCES}
)

set_property(TARGET darkeye_business_ui PROPERTY AUTOMOC ON)
add_library(darkeye::ui ALIAS darkeye_business_ui)
target_include_directories(darkeye_business_ui PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
)
target_link_libraries(darkeye_business_ui PUBLIC
    darkeye::core
    darkeye::settings
    darkeye::database
    darkeye::services
    darkeye::graph_view
    darkeye::darkeye_ui
    darkeye::docking
    darkeye::dvd_scene
    Qt6::Widgets
)
darkeye_apply_compiler_options(darkeye_business_ui)
