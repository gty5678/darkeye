add_library(darkeye_domain STATIC
    include/domain/Person.h
    include/domain/SerialNumber.h
    include/domain/Work.h
    src/SerialNumber.cpp
)
add_library(darkeye::domain ALIAS darkeye_domain)
target_include_directories(darkeye_domain PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(darkeye_domain PUBLIC Qt6::Core)
darkeye_apply_compiler_options(darkeye_domain)
