function(darkeye_add_test_executable target)
    qt_add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE FALSE)
    darkeye_apply_compiler_options(${target})
endfunction()

# Each source provides run_<filename> so test cases still run in separate
# processes with their own Qt application, environment and timeout.
function(darkeye_add_test_suite target)
    cmake_parse_arguments(PARSE_ARGV 1 suite "" "" "SOURCES;LIBRARIES")
    set(test_declarations "")
    set(test_entries "")
    foreach(source IN LISTS suite_SOURCES)
        get_filename_component(test_class "${source}" NAME_WE)
        string(APPEND test_declarations "int run_${test_class}(int argc, char **argv);\n")
        string(APPEND test_entries "    {\"${test_class}\", run_${test_class}},\n")
    endforeach()
    set(main_source "${CMAKE_CURRENT_BINARY_DIR}/${target}_main.cpp")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/TestSuiteMain.cpp.in" "${main_source}" @ONLY)
    darkeye_add_test_executable(${target} "${main_source}" ${suite_SOURCES})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    target_link_libraries(${target} PRIVATE ${suite_LIBRARIES} Qt6::Test)
endfunction()

function(darkeye_register_test name target)
    add_test(NAME ${name} COMMAND ${target} ${ARGN})
    set_tests_properties(${name} PROPERTIES
        LABELS "${target}"
        ENVIRONMENT
            "${offscreen_environment};DARKEYE_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/runtime/${name};TEMP=${native_ui_runtime};TMP=${native_ui_runtime}"
        ENVIRONMENT_MODIFICATION "${test_qt_path}")
endfunction()
