file(GLOB DARKEYE_COMPONENT_SOURCES CONFIGURE_DEPENDS
    "base/*.h"
    "base/*.cpp"
    "components/*.h"
    "components/*.cpp"
)

qt_add_library(darkeye_ui STATIC
    DarkeyeUi.h
    ${DARKEYE_COMPONENT_SOURCES}
    layouts/FlowLayout.h layouts/FlowLayout.cpp
    layouts/Layouts.h
    layouts/VerticalFlowLayout.h layouts/VerticalFlowLayout.cpp
    layouts/VerticalTextLayout.h layouts/VerticalTextLayout.cpp
    layouts/WaterfallLayout.h layouts/WaterfallLayout.cpp
    theme/ChamferPath.h
    theme/ChamferStyle.h theme/ChamferStyle.cpp
    theme/IconProvider.h theme/IconProvider.cpp
    theme/StylesheetLoader.h theme/StylesheetLoader.cpp
    theme/ThemeService.h theme/ThemeService.cpp
    darkeye_ui_resources.qrc
)
add_library(darkeye::darkeye_ui ALIAS darkeye_ui)
target_include_directories(darkeye_ui PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
)
target_link_libraries(darkeye_ui PUBLIC Qt6::Svg Qt6::Widgets)
darkeye_apply_compiler_options(darkeye_ui)
