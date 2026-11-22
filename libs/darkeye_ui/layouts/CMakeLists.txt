qt_add_library(darkeye_layouts STATIC
    FlowLayout.h
    Layouts.h
    VerticalFlowLayout.h
    VerticalTextLayout.h
    WaterfallLayout.h
    FlowLayout.cpp
    VerticalFlowLayout.cpp
    VerticalTextLayout.cpp
    WaterfallLayout.cpp
)
add_library(darkeye::layouts ALIAS darkeye_layouts)
target_include_directories(darkeye_layouts PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
)
target_link_libraries(darkeye_layouts PUBLIC Qt6::Widgets)
darkeye_apply_compiler_options(darkeye_layouts)
