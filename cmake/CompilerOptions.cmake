set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# With the Chinese-only MSVC language pack, cl can emit UTF-8 while CMake's
# compiler probe decodes it as CP936. Repair that specific mojibake prefix;
# otherwise Ninja records zero header dependencies and mixes stale ABI layouts.
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^娉")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件: ")
endif()

function(darkeye_apply_compiler_options target)
    target_compile_features(${target} PUBLIC cxx_std_20)

    # Executables and tests read editable configuration beside the binary.
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "EXECUTABLE" AND EXISTS "${PROJECT_SOURCE_DIR}/config")
        # Many tests share one output directory. Copy once before linking,
        # instead of racing concurrent POST_BUILD copies into the same files.
        get_target_property(config_output_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
        if(NOT config_output_dir)
            if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
                set(config_output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
            else()
                set(config_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
            endif()
        endif()
        string(SHA256 config_output_id "${config_output_dir}")
        set(config_copy_target "darkeye_config_${config_output_id}")
        if(NOT TARGET ${config_copy_target})
            add_custom_target(${config_copy_target}
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${PROJECT_SOURCE_DIR}/config" "${config_output_dir}/config"
                VERBATIM)
        endif()
        add_dependencies(${target} ${config_copy_target})
    endif()

    if(MSVC)
        # Qt's imported targets already expose their include directories through
        # /external:I and lower normal compiler diagnostics with /external:W0.
        # MSVC code analysis is controlled separately, so keep it out of all
        # third-party headers as well.  This avoids C26495/C26813/C26478 reports
        # originating in Qt while preserving analysis for Darkeye sources.
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
            /analyze:external-
        )
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
