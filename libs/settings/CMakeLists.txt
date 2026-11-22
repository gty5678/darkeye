

# 这个里面主要处理settings.ini给软件简单的持久的化的读写，包括初始值的创建，
# 包括各种跨平台的路径，包括data的路径

file(GLOB DARKEYE_SETTINGS_HEADERS CONFIGURE_DEPENDS "*.h")
file(GLOB DARKEYE_SETTINGS_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(darkeye_settings STATIC
    ${DARKEYE_SETTINGS_HEADERS}
    ${DARKEYE_SETTINGS_SOURCES}
)
add_library(darkeye::settings ALIAS darkeye_settings)
target_include_directories(darkeye_settings PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
)
target_link_libraries(darkeye_settings PUBLIC Qt6::Core)
darkeye_apply_compiler_options(darkeye_settings)
