file(GLOB_RECURSE DARKEYE_SERVICE_HEADERS CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE DARKEYE_SERVICE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

set(DARKEYE_SERVICE_QOBJECT_HEADERS
    include/services/ActressSyncService.h
    include/services/BatchTranslationService.h
    include/services/LlamaRuntime.h
    include/services/UpdateService.h
)
set_source_files_properties(${DARKEYE_SERVICE_QOBJECT_HEADERS} PROPERTIES SKIP_AUTOMOC ON)
qt_wrap_cpp(DARKEYE_SERVICE_MOC_SOURCES ${DARKEYE_SERVICE_QOBJECT_HEADERS})

qt_add_library(darkeye_services STATIC
    ${DARKEYE_SERVICE_HEADERS}
    ${DARKEYE_SERVICE_SOURCES}
    ${DARKEYE_SERVICE_MOC_SOURCES}
)
add_library(darkeye::services ALIAS darkeye_services)
set_property(TARGET darkeye_services PROPERTY AUTOMOC ON)
target_include_directories(darkeye_services PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_services PUBLIC darkeye::core darkeye::database darkeye::settings Qt6::Network)
darkeye_apply_compiler_options(darkeye_services)
