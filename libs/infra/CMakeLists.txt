file(GLOB_RECURSE DARKEYE_INFRA_HEADERS CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE DARKEYE_INFRA_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_library(darkeye_infra STATIC
    ${DARKEYE_INFRA_HEADERS}
    ${DARKEYE_INFRA_SOURCES}
)
add_library(darkeye::infra ALIAS darkeye_infra)
set_property(TARGET darkeye_infra PROPERTY AUTOMOC ON)
target_include_directories(darkeye_infra PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_infra PUBLIC Qt6::Core)
target_compile_definitions(darkeye_infra PUBLIC
    DARKEYE_VERSION="${PROJECT_VERSION}"
)
darkeye_apply_compiler_options(darkeye_infra)
