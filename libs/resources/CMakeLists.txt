add_library(darkeye_resources STATIC
    include/app/Resources.h
    src/Resources.cpp
    "${PROJECT_SOURCE_DIR}/resources/resources.qrc"
)
add_library(darkeye::resources ALIAS darkeye_resources)
target_include_directories(darkeye_resources PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_resources PRIVATE Qt6::Core)
darkeye_apply_compiler_options(darkeye_resources)
