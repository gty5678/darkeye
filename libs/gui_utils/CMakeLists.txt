add_library(darkeye_gui_utils STATIC
    include/utils/GeneralUtils.h
    include/utils/MediaUtils.h
    src/GeneralUtils.cpp
    src/MediaUtils.cpp
)
add_library(darkeye::gui_utils ALIAS darkeye_gui_utils)
target_include_directories(darkeye_gui_utils PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_gui_utils
    PUBLIC Qt6::Gui
    PRIVATE darkeye::domain
)
darkeye_apply_compiler_options(darkeye_gui_utils)
