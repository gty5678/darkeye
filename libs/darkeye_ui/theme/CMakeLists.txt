qt_add_library(darkeye_theme STATIC
    ChamferStyle.h
    ChamferStyle.cpp
    IconProvider.h
    StylesheetLoader.h
    ThemeContext.h
    ThemeService.h
    IconProvider.cpp
    StylesheetLoader.cpp
    ThemeContext.cpp
    ThemeService.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../darkeye_ui_resources.qrc"
)
add_library(darkeye::theme ALIAS darkeye_theme)
target_include_directories(darkeye_theme PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
)
target_link_libraries(darkeye_theme PUBLIC
    Qt6::Svg
    Qt6::Widgets
)
darkeye_apply_compiler_options(darkeye_theme)
