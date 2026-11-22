add_subdirectory(theme)
add_subdirectory(layouts)
add_subdirectory(components)

# Stable public facade for the reusable component library. Consumers should
# prefer this target instead of depending on the implementation targets.
add_library(darkeye_ui INTERFACE)
add_library(darkeye::darkeye_ui ALIAS darkeye_ui)
target_link_libraries(darkeye_ui INTERFACE
    darkeye::theme
    darkeye::layouts
    darkeye::widgets
)
