qt_add_library(darkeye_desktop_ui STATIC
    MainWindow.cpp
    MainWindow.h
)
add_library(darkeye::desktop_ui ALIAS darkeye_desktop_ui)
target_include_directories(darkeye_desktop_ui PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)
target_link_libraries(darkeye_desktop_ui PUBLIC
    darkeye::app
    # MainWindow.h directly includes graph/GraphManager.h.  Keep the direct
    # usage requirement visible to CMake generators and Visual Studio's
    # IntelliSense target model instead of relying only on the app facade.
    darkeye::graph
)
if(WIN32)
    target_link_libraries(darkeye_desktop_ui PRIVATE dwmapi Psapi)
elseif(APPLE)
    find_library(DARKEYE_APPKIT AppKit REQUIRED)
    target_sources(darkeye_desktop_ui PRIVATE
        MacWindowAppearance.mm MacWindowAppearance.h
        MacWorkspaceShortcuts.mm MacWorkspaceShortcuts.h
    )
    target_link_libraries(darkeye_desktop_ui PRIVATE "${DARKEYE_APPKIT}")
endif()
darkeye_apply_compiler_options(darkeye_desktop_ui)

qt_add_executable(Darkeye WIN32
    Application.cpp
    Application.h
    main.cpp
)
if(APPLE)
    set_target_properties(Darkeye PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "win.darkeye.desktop"
        MACOSX_BUNDLE_BUNDLE_NAME "Darkeye"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()
if(WIN32)
    target_sources(Darkeye PRIVATE "${PROJECT_SOURCE_DIR}/resources/logo.rc")
endif()
target_link_libraries(Darkeye PRIVATE darkeye::desktop_ui)
darkeye_apply_compiler_options(Darkeye)
darkeye_configure_packaging(Darkeye)

# Visual Studio project generators: arguments apply to the build executable only.
set_property(TARGET Darkeye PROPERTY VS_DEBUGGER_COMMAND_ARGUMENTS
    "--config-dir \"${PROJECT_SOURCE_DIR}/config\" --avwiki-dir \"${PROJECT_SOURCE_DIR}/avwiki\" --data-dir \"${PROJECT_SOURCE_DIR}/data\"")
set_property(TARGET Darkeye PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

# Visual Studio's Open Folder workflow uses launch.vs.json with Ninja presets.
# Bootstrap a fresh checkout from the shared template; preserve local overrides.
if(WIN32 AND MSVC AND NOT EXISTS "${PROJECT_SOURCE_DIR}/.vs/launch.vs.json")
    file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/.vs")
    configure_file("${PROJECT_SOURCE_DIR}/tools/launch.vs.json"
        "${PROJECT_SOURCE_DIR}/.vs/launch.vs.json" COPYONLY)
endif()

