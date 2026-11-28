add_library(darkeye_bridge STATIC
    include/http/LocalApiServer.h
    src/LocalApiServer.cpp
)
add_library(darkeye::bridge ALIAS darkeye_bridge)
target_include_directories(darkeye_bridge PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_bridge
    PUBLIC Qt6::Core
    PRIVATE Qt6::HttpServer Qt6::Network
)
darkeye_apply_compiler_options(darkeye_bridge)
