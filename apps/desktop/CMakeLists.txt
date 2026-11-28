qt_add_library(darkeye_desktop_ui STATIC
    MainWindow.cpp
    MainWindow.h
)
add_library(darkeye::desktop_ui ALIAS darkeye_desktop_ui)
target_include_directories(darkeye_desktop_ui PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)
target_link_libraries(darkeye_desktop_ui PUBLIC
    darkeye::darkeye_ui
    darkeye::domain
    darkeye::graph
    darkeye::infra
    darkeye::resources
    darkeye::services
    darkeye::ui
    PRIVATE
    darkeye::bridge
    darkeye::docking
)
if(WIN32)
    target_link_libraries(darkeye_desktop_ui PRIVATE dwmapi)
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
target_link_libraries(Darkeye PRIVATE
    darkeye::desktop_ui
    darkeye::ui
    darkeye::bridge
    darkeye::dvd_scene
    darkeye::graph_view
    darkeye::services
    darkeye::darkeye_ui
    darkeye::database
    darkeye::infra
    darkeye::resources
)
darkeye_apply_compiler_options(Darkeye)
darkeye_configure_packaging(Darkeye)

