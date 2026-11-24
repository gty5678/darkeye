set(_darkeye_qt_components Core Gui GuiPrivate HttpServer Network ShaderTools Sql Svg Widgets)
if(DARKEYE_BUILD_TESTS)
    list(APPEND _darkeye_qt_components Test)
endif()

# QRhi in the graph and 3D renderers requires GuiPrivate. Deployment uses the
# same Qt kit that supplies the build headers and libraries.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.12.0 REQUIRED COMPONENTS ${_darkeye_qt_components})
qt_standard_project_setup(REQUIRES 6.12.0)
find_package(OpenMP COMPONENTS CXX)
find_package(Freetype CONFIG REQUIRED)
find_package(msdfgen CONFIG REQUIRED)

get_filename_component(DARKEYE_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
set(DARKEYE_QT_BIN_DIR "${DARKEYE_QT_PREFIX}/bin")
set(DARKEYE_QT_PLUGIN_DIR "${DARKEYE_QT_PREFIX}/plugins")

unset(_darkeye_qt_components)
