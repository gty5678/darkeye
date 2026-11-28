include(support/TestTargets.cmake)

# Native UI tests require a desktop session.
if(WIN32)
    set(DARKEYE_TEST_NATIVE_PLATFORM windows)
elseif(APPLE)
    set(DARKEYE_TEST_NATIVE_PLATFORM cocoa)
endif()

set(test_qt_path "PATH=path_list_prepend:${DARKEYE_QT_BIN_DIR}")
set(test_qt_plugins "QT_QPA_PLATFORM_PLUGIN_PATH=${DARKEYE_QT_PLUGIN_DIR}/platforms")
set(offscreen_environment "QT_QPA_PLATFORM=offscreen;${test_qt_plugins}")
set(native_ui_environment
    "QT_QPA_PLATFORM=${DARKEYE_TEST_NATIVE_PLATFORM};${test_qt_plugins}")
set(native_ui_runtime "${CMAKE_CURRENT_BINARY_DIR}/native-ui-runtime")
file(MAKE_DIRECTORY "${native_ui_runtime}")

if(DARKEYE_BUILD_DEMOS)
    add_subdirectory(demos)
endif()
add_subdirectory(render3d)
add_subdirectory(dvd_scene)

darkeye_add_test_suite(darkeye_infra_domain_tests
    LIBRARIES darkeye::infra darkeye::domain
    SOURCES unit/AppPathsTest.cpp compatibility/SerialNumberCompatibilityTest.cpp)
target_compile_definitions(darkeye_infra_domain_tests PRIVATE DARKEYE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
darkeye_register_test(darkeye_app_tests darkeye_infra_domain_tests AppPathsTest)
darkeye_register_test(darkeye_serial_number_tests darkeye_infra_domain_tests SerialNumberCompatibilityTest)

# Keep the reusable widget tests linked only to the reusable UI library.
darkeye_add_test_suite(darkeye_widgets_tests
    LIBRARIES darkeye::darkeye_ui
    SOURCES unit/DarkeyeUiBoundaryTest.cpp unit/ThemeServiceTest.cpp unit/LayoutTest.cpp)
darkeye_register_test(darkeye_ui_boundary_tests darkeye_widgets_tests DarkeyeUiBoundaryTest)
set_tests_properties(darkeye_ui_boundary_tests PROPERTIES ENVIRONMENT "${offscreen_environment}")
darkeye_register_test(darkeye_theme_tests darkeye_widgets_tests ThemeServiceTest)
darkeye_register_test(darkeye_layout_tests darkeye_widgets_tests LayoutTest)

darkeye_add_test_executable(darkeye_utils_compatibility_tests unit/UtilsCompatibilityTest.cpp)
target_link_libraries(darkeye_utils_compatibility_tests PRIVATE
    darkeye::database darkeye::gui_utils Qt6::Test)
darkeye_register_test(darkeye_utils_compatibility_tests darkeye_utils_compatibility_tests)

darkeye_add_test_suite(darkeye_database_tests
    LIBRARIES darkeye::database Qt6::CorePrivate Qt6::Gui Qt6::Network
    SOURCES
        unit/SqliteConnectionTest.cpp
        integration/SchemaManagerTest.cpp
        integration/SchemaSnapshotTest.cpp
        compatibility/DatabaseFixtureTest.cpp
        integration/DatabaseBackupServiceTest.cpp
        integration/DatabaseOperationsTest.cpp
        integration/WebDavCredentialStoreTest.cpp
        integration/SnapshotUploadTest.cpp
        integration/WebDavTimeoutTest.cpp
        integration/WorkRepositoryTest.cpp
        integration/PrivateRepositoryTest.cpp)
target_sources(darkeye_database_tests PRIVATE support/LegacySchemaFixtures.qrc)
if(WIN32)
    target_link_libraries(darkeye_database_tests PRIVATE Advapi32)
endif()
darkeye_register_test(darkeye_database_tests darkeye_database_tests SqliteConnectionTest)
darkeye_register_test(darkeye_schema_tests darkeye_database_tests SchemaManagerTest)
darkeye_register_test(darkeye_schema_snapshot_tests darkeye_database_tests SchemaSnapshotTest)
darkeye_register_test(darkeye_database_fixture_tests darkeye_database_tests DatabaseFixtureTest)
darkeye_register_test(darkeye_database_backup_tests darkeye_database_tests DatabaseBackupServiceTest)
darkeye_register_test(darkeye_database_operations_tests darkeye_database_tests DatabaseOperationsTest)
darkeye_register_test(darkeye_webdav_credential_tests darkeye_database_tests WebDavCredentialStoreTest)
darkeye_register_test(darkeye_snapshot_upload_tests darkeye_database_tests SnapshotUploadTest -o -,txt)
darkeye_register_test(darkeye_webdav_timeout_tests darkeye_database_tests WebDavTimeoutTest)
darkeye_register_test(darkeye_work_repository_tests darkeye_database_tests WorkRepositoryTest)
darkeye_register_test(darkeye_private_repository_tests darkeye_database_tests PrivateRepositoryTest)
set_tests_properties(darkeye_webdav_credential_tests darkeye_snapshot_upload_tests
    PROPERTIES ENVIRONMENT "")
set_tests_properties(darkeye_snapshot_upload_tests PROPERTIES TIMEOUT 30)
set_tests_properties(darkeye_webdav_timeout_tests PROPERTIES TIMEOUT 60)

darkeye_add_test_executable(darkeye_schema_snapshot_dump support/SchemaSnapshotDump.cpp)
target_link_libraries(darkeye_schema_snapshot_dump PRIVATE darkeye::database)
darkeye_add_test_executable(darkeye_fixture_database_builder
    support/FixtureDatabaseBuilder.cpp support/LegacySchemaFixtures.qrc)
target_compile_definitions(darkeye_fixture_database_builder PRIVATE DARKEYE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(darkeye_fixture_database_builder PRIVATE darkeye::database)
set(database_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixtures/databases")
target_compile_definitions(darkeye_database_tests PRIVATE
    DARKEYE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DARKEYE_TEST_DATABASE_DIR="${database_fixture_dir}")
add_test(NAME darkeye_prepare_database_fixtures
    COMMAND darkeye_fixture_database_builder "${database_fixture_dir}")
set_tests_properties(darkeye_prepare_database_fixtures PROPERTIES
    FIXTURES_SETUP database_fixtures TIMEOUT 30 ENVIRONMENT_MODIFICATION "${test_qt_path}")
set_tests_properties(darkeye_schema_tests darkeye_database_fixture_tests PROPERTIES
    FIXTURES_REQUIRED database_fixtures)

darkeye_add_test_suite(darkeye_services_tests
    LIBRARIES darkeye::services Qt6::Gui
    SOURCES
        integration/VideoLibraryServiceTest.cpp
        integration/WorkMaintenanceServiceTest.cpp
        integration/CollectorClientTest.cpp
        integration/PublicRepositoryTest.cpp
        integration/LlmTranslationServiceTest.cpp
        integration/TranslationSettingsTimingTest.cpp)
darkeye_register_test(darkeye_video_library_service_tests darkeye_services_tests VideoLibraryServiceTest)
darkeye_register_test(darkeye_work_maintenance_tests darkeye_services_tests WorkMaintenanceServiceTest)
darkeye_register_test(darkeye_collector_client_tests darkeye_services_tests CollectorClientTest)
darkeye_register_test(darkeye_public_repository_tests darkeye_services_tests PublicRepositoryTest)
darkeye_register_test(darkeye_llm_translation_tests darkeye_services_tests LlmTranslationServiceTest)
darkeye_register_test(darkeye_translation_settings_timing_tests darkeye_services_tests TranslationSettingsTimingTest)

darkeye_add_test_executable(darkeye_local_api_tests integration/LocalApiServerTest.cpp)
target_link_libraries(darkeye_local_api_tests PRIVATE darkeye::bridge darkeye::database Qt6::Network Qt6::Test)
darkeye_register_test(darkeye_local_api_tests darkeye_local_api_tests)

darkeye_add_test_executable(darkeye_workspace_tests unit/MyAdsTest.cpp)
target_link_libraries(darkeye_workspace_tests PRIVATE darkeye::docking Qt6::Test)
darkeye_configure_packaging(darkeye_workspace_tests NO_INSTALL)
darkeye_register_test(darkeye_myads_tests darkeye_workspace_tests
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/myads-results.txt,txt")
darkeye_register_test(darkeye_myads_drag_tests darkeye_workspace_tests
    cancelledTabDragReturnsToOriginalPosition -platform offscreen)
set_tests_properties(darkeye_myads_tests PROPERTIES RUN_SERIAL TRUE ENVIRONMENT "${native_ui_environment}")
set_tests_properties(darkeye_myads_drag_tests PROPERTIES
    TIMEOUT 30 ENVIRONMENT "${test_qt_plugins}")

darkeye_add_test_suite(darkeye_integration_tests
    LIBRARIES darkeye::ui MdEdit::Editor
    SOURCES
        unit/ComponentLibraryTest.cpp
        unit/MediaComponentTest.cpp
        unit/TextWidgetTest.cpp
        integration/ImageFetchServiceTest.cpp
        integration/CrawlerRefreshTest.cpp
        integration/WorkPageTest.cpp
        integration/ShelfPageTest.cpp
        integration/AvPageTest.cpp
        integration/PersonPageTest.cpp
        integration/PersonalRecordDialogTest.cpp
        integration/PersonalRecordManagementWidgetTest.cpp
        integration/StatisticsRepositoryTest.cpp
        integration/NfoImportTest.cpp
        ../libs/graph_view/tests/GraphPageRenderingTest.cpp)
darkeye_register_test(darkeye_component_library_tests darkeye_integration_tests ComponentLibraryTest)
darkeye_register_test(darkeye_media_component_tests darkeye_integration_tests MediaComponentTest)
darkeye_register_test(darkeye_text_widget_tests darkeye_integration_tests TextWidgetTest)
darkeye_register_test(darkeye_image_fetch_tests darkeye_integration_tests ImageFetchServiceTest)
darkeye_register_test(darkeye_crawler_refresh_tests darkeye_integration_tests CrawlerRefreshTest)
darkeye_register_test(darkeye_work_page_tests darkeye_integration_tests WorkPageTest)
darkeye_register_test(darkeye_shelf_page_tests darkeye_integration_tests ShelfPageTest
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/shelf-page-results.txt,txt")
darkeye_register_test(darkeye_av_page_tests darkeye_integration_tests AvPageTest)
darkeye_register_test(darkeye_person_page_tests darkeye_integration_tests PersonPageTest)
darkeye_register_test(darkeye_personal_record_dialog_tests darkeye_integration_tests PersonalRecordDialogTest)
darkeye_register_test(darkeye_personal_record_management_tests darkeye_integration_tests PersonalRecordManagementWidgetTest)
darkeye_register_test(darkeye_statistics_repository_tests darkeye_integration_tests StatisticsRepositoryTest)
darkeye_register_test(darkeye_nfo_import_tests darkeye_integration_tests NfoImportTest)
darkeye_register_test(darkeye_graph_page_rendering_tests darkeye_integration_tests GraphPageRenderingTest)
set_tests_properties(darkeye_crawler_refresh_tests PROPERTIES TIMEOUT 30)
set_tests_properties(darkeye_graph_page_rendering_tests PROPERTIES
    TIMEOUT 60 ENVIRONMENT "${native_ui_environment}")
set(shelf_runtime "${CMAKE_CURRENT_BINARY_DIR}/shelf-runtime")
file(MAKE_DIRECTORY "${shelf_runtime}")
set_tests_properties(darkeye_shelf_page_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
    ENVIRONMENT "${native_ui_environment};TEMP=${shelf_runtime};TMP=${shelf_runtime}")

# This test launches its own executable as a mock server.
darkeye_add_test_executable(darkeye_llama_runtime_tests integration/LlamaRuntimeTest.cpp)
target_link_libraries(darkeye_llama_runtime_tests PRIVATE darkeye::ui Qt6::Test)
darkeye_register_test(darkeye_llama_runtime_tests darkeye_llama_runtime_tests)

darkeye_add_test_executable(darkeye_graph_tests ../libs/graph/tests/GraphViewSessionTest.cpp)
target_link_libraries(darkeye_graph_tests PRIVATE darkeye::graph)
darkeye_register_test(darkeye_graph_tests darkeye_graph_tests)

darkeye_add_test_executable(darkeye_graph_schema_tests
    integration/GraphSchemaTest.cpp support/LegacySchemaFixtures.qrc)
target_link_libraries(darkeye_graph_schema_tests PRIVATE darkeye::database darkeye::graph Qt6::Test)
darkeye_register_test(darkeye_graph_schema_tests darkeye_graph_schema_tests)

darkeye_add_test_suite(darkeye_graph_view_tests
    LIBRARIES darkeye::graph_view Qt6::GuiPrivate
    SOURCES
        ../libs/graph_view/tests/physics_tests.cpp
        ../libs/graph_view/tests/MsdfFontAtlasTest.cpp
        ../libs/graph_view/tests/GpuSimulationTest.cpp)
darkeye_register_test(darkeye_graph_view_physics_tests darkeye_graph_view_tests physics_tests)
darkeye_register_test(darkeye_msdf_font_atlas_tests darkeye_graph_view_tests MsdfFontAtlasTest -o -,txt)
darkeye_register_test(darkeye_gpu_simulation_tests darkeye_graph_view_tests GpuSimulationTest
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/gpu-simulation-results.txt,txt")
set_tests_properties(darkeye_graph_tests darkeye_graph_view_physics_tests darkeye_msdf_font_atlas_tests
    PROPERTIES ENVIRONMENT "")
set_tests_properties(darkeye_msdf_font_atlas_tests PROPERTIES TIMEOUT 60)
set_tests_properties(darkeye_gpu_simulation_tests PROPERTIES
    TIMEOUT 120 ENVIRONMENT "${native_ui_environment}")

darkeye_add_test_executable(darkeye_cover_maker_tests unit/CoverMakerTest.cpp)
target_link_libraries(darkeye_cover_maker_tests PRIVATE darkeye::cover_maker Qt6::Test)
darkeye_register_test(darkeye_cover_maker_tests darkeye_cover_maker_tests -o -,txt)
set_tests_properties(darkeye_cover_maker_tests PROPERTIES ENVIRONMENT "${offscreen_environment}")
if(WIN32)
    set_property(TEST darkeye_cover_maker_tests APPEND PROPERTY
        ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
endif()

darkeye_add_test_executable(darkeye_ui_smoke_tests integration/MainWindowSmokeTest.cpp)
target_link_libraries(darkeye_ui_smoke_tests PRIVATE darkeye::desktop_ui Qt6::Test)
if(APPLE)
    target_sources(darkeye_ui_smoke_tests PRIVATE integration/MacShortcutEvents.mm)
endif()
darkeye_register_test(darkeye_ui_smoke_tests darkeye_ui_smoke_tests
    -v1 -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/ui-smoke-results.txt,txt")
set_tests_properties(darkeye_ui_smoke_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
    ENVIRONMENT "${native_ui_environment};TEMP=${native_ui_runtime};TMP=${native_ui_runtime}")

# Exercise the real entry point with isolated settings and data.
qt_add_executable(darkeye_startup_tests
    integration/ApplicationStartupTest.cpp
    ../apps/desktop/Application.cpp
    ../apps/desktop/Application.h)
target_link_libraries(darkeye_startup_tests PRIVATE
    darkeye::desktop_ui
    darkeye::ui
    darkeye::bridge
    darkeye::docking
    darkeye::dvd_scene
    darkeye::graph_view
    darkeye::services
    darkeye::darkeye_ui
    darkeye::database
    darkeye::infra
    darkeye::resources
    Qt6::Test
)
set_target_properties(darkeye_startup_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/startup")
darkeye_apply_compiler_options(darkeye_startup_tests)
foreach(mode IN ITEMS reject accept)
    darkeye_register_test(darkeye_startup_${mode} darkeye_startup_tests ${mode}
        --config-dir "${PROJECT_SOURCE_DIR}/config"
        --avwiki-dir "${CMAKE_CURRENT_BINARY_DIR}/startup/wiki-${mode}"
        --data-dir "${CMAKE_CURRENT_BINARY_DIR}/startup/data-${mode}")
    set_tests_properties(darkeye_startup_${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 30
        ENVIRONMENT "${native_ui_environment};DARKEYE_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/startup/data;TEMP=${native_ui_runtime};TMP=${native_ui_runtime}")
endforeach()
