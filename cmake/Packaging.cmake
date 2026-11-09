include(GNUInstallDirs)

function(darkeye_configure_packaging target)
    get_target_property(is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND is_bundle)
        set(resource_dir "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources")
        set(resource_install_dir "${target}.app/Contents/Resources")
    else()
        set(resource_dir "$<TARGET_FILE_DIR:${target}>")
        set(resource_install_dir "${CMAKE_INSTALL_BINDIR}")
    endif()
    if(NOT "${ARGV1}" STREQUAL "NO_INSTALL")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/config" "${resource_dir}/config"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/avwiki" "${resource_dir}/avwiki"
            VERBATIM
        )
    endif()

    # --------------------------------
    # 开发阶段：让 build 目录中的程序可以直接运行
    # --------------------------------
    if(WIN32 AND DARKEYE_DEPLOY_QT_RUNTIME)
        unset(DARKEYE_WINDEPLOYQT_EXECUTABLE CACHE)
        find_program(DARKEYE_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt windeployqt6
            HINTS "${DARKEYE_QT_BIN_DIR}"
            NO_DEFAULT_PATH
            NO_CACHE
            REQUIRED
        )

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${DARKEYE_WINDEPLOYQT_EXECUTABLE}"
                "$<$<CONFIG:Debug>:--debug>"
                "$<$<NOT:$<CONFIG:Debug>>:--release>"
                --no-translations
                --no-quick-import
                --dir "$<TARGET_FILE_DIR:${target}>"
                "$<TARGET_FILE:${target}>"
            COMMENT "Deploying Qt runtime beside ${target}"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()

    # Development tools need build-tree runtime deployment, but no install payload.
    if("${ARGV1}" STREQUAL "NO_INSTALL")
        return()
    endif()

    # --------------------------------
    # install：安装程序本身
    # --------------------------------
    install(
        TARGETS ${target}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # Each platform supplies one native executable in resources.
    # macOS supplies its native updater without an extension.
    if(WIN32)
        set(updater_name "updater.exe")
    else()
        set(updater_name "updater")
    endif()
    if(EXISTS "${PROJECT_SOURCE_DIR}/resources/${updater_name}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${PROJECT_SOURCE_DIR}/resources/${updater_name}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PROJECT_SOURCE_DIR}/resources/${updater_name}"
                "$<TARGET_FILE_DIR:${target}>/${updater_name}"
            VERBATIM)
    endif()
    if(APPLE AND is_bundle)
        set(updater_install_dir "${target}.app/Contents/MacOS")
    else()
        set(updater_install_dir "${CMAKE_INSTALL_BINDIR}")
    endif()
    # Missing artifacts intentionally fail at install time, not during normal
    # development builds. PROGRAMS also sets executable permissions on Unix.
    install(PROGRAMS
        "${PROJECT_SOURCE_DIR}/resources/${updater_name}"
        DESTINATION "${updater_install_dir}")

    # Qt's deployment script only installs Qt runtime files. msdfgen is a
    # shared vcpkg dependency, so install both imported DLLs and recursively
    # collect their non-system dependencies (FreeType, PNG, zlib, and so on).
    if(WIN32)
        install(
            IMPORTED_RUNTIME_ARTIFACTS
                msdfgen::msdfgen-core
                msdfgen::msdfgen-ext
            RUNTIME_DEPENDENCY_SET darkeye_msdfgen_runtime_dependencies
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        install(
            RUNTIME_DEPENDENCY_SET darkeye_msdfgen_runtime_dependencies
            DIRECTORIES
                "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin>"
                "$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
            PRE_EXCLUDE_REGEXES
                "^api-ms-"
                "^ext-ms-"
            POST_EXCLUDE_REGEXES
                "^[A-Za-z]:[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    # Read-only seeds: runtime edits and downloads live in user data.
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/avwiki" "${PROJECT_SOURCE_DIR}/config"
        DESTINATION "${resource_install_dir}")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${resource_install_dir}")
    install(FILES "${PROJECT_SOURCE_DIR}/libs/render3d/third_party/cgltf/LICENSE"
        DESTINATION "${resource_install_dir}/licenses/cgltf")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/"
            DESTINATION "${resource_install_dir}/licenses"
            FILES_MATCHING PATTERN copyright)
    endif()
    find_path(DARKEYE_QT_LICENSE_DIR NAMES LICENSE LICENSE.LGPL3 LGPL-3.0-only.txt
        HINTS "${DARKEYE_QT_PREFIX}/licenses" "${DARKEYE_QT_PREFIX}/../../Licenses"
        NO_DEFAULT_PATH)
    if(DARKEYE_QT_LICENSE_DIR)
        install(DIRECTORY "${DARKEYE_QT_LICENSE_DIR}/"
            DESTINATION "${resource_install_dir}/licenses/qt")
    endif()
    install(DIRECTORY "${DARKEYE_QT_PREFIX}/licenses/"
        DESTINATION "${resource_install_dir}/licenses/qt" OPTIONAL)

    # --------------------------------
    # install：部署 Qt DLL / plugins
    # --------------------------------
    qt_generate_deploy_app_script(
        TARGET ${target}
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
    )

    install(SCRIPT "${deploy_script}")

endfunction()
