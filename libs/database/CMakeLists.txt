file(GLOB_RECURSE DARKEYE_DATABASE_HEADERS CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE DARKEYE_DATABASE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

qt_add_library(darkeye_database STATIC
    ${DARKEYE_DATABASE_HEADERS}
    ${DARKEYE_DATABASE_SOURCES}
)
add_library(darkeye::database ALIAS darkeye_database)
target_include_directories(darkeye_database PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_database
    PUBLIC darkeye::core darkeye::domain Qt6::Sql
    PRIVATE darkeye::resources Qt6::CorePrivate Qt6::Gui Qt6::Network
)
if(WIN32)
    target_link_libraries(darkeye_database PRIVATE Advapi32)
elseif(APPLE)
    find_library(DARKEYE_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(DARKEYE_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(darkeye_database PRIVATE
        ${DARKEYE_SECURITY_FRAMEWORK} ${DARKEYE_COREFOUNDATION_FRAMEWORK})
endif()
darkeye_apply_compiler_options(darkeye_database)
