qt_add_library(darkeye_cover_maker STATIC
    include/cover_maker/CoverDocument.h
    include/cover_maker/CoverEditor.h
    include/cover_maker/SleeveDocument.h
    include/cover_maker/SleeveEditor.h
    src/SleeveDocument.cpp
    src/SleeveEditor.cpp
    src/CoverDocument.cpp
    src/ImageCropDialog.h
    src/CoverEditor.cpp
)
add_library(darkeye::cover_maker ALIAS darkeye_cover_maker)
target_include_directories(darkeye_cover_maker PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(darkeye_cover_maker PUBLIC Qt6::Widgets)
darkeye_apply_compiler_options(darkeye_cover_maker)
