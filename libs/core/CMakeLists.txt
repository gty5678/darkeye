file(GLOB_RECURSE DARKEYE_CORE_HEADERS CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE DARKEYE_CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_library(darkeye_core STATIC
    ${DARKEYE_CORE_HEADERS}
    ${DARKEYE_CORE_SOURCES}
    "${PROJECT_SOURCE_DIR}/resources/resources.qrc"
)
add_library(darkeye::core ALIAS darkeye_core)
set_property(TARGET darkeye_core PROPERTY AUTOMOC ON)
target_include_directories(darkeye_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::HttpServer
    Qt6::Network
    Qt6::Sql
    Qt6::Widgets
)
target_compile_definitions(darkeye_core PUBLIC
    DARKEYE_VERSION="${PROJECT_VERSION}"
)
darkeye_apply_compiler_options(darkeye_core)
