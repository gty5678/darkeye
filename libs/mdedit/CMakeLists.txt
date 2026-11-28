add_library(MdEditEditor STATIC
  src/platform.h
  src/theme_palette.h
  src/markdown/scanner.h src/markdown/scanner.cpp
  src/markdown/renderer.h src/markdown/renderer.cpp
  src/editor/highlighter.h src/editor/highlighter.cpp
  src/editor/numbered_editor.h
  src/editor/tables/table_data.h src/editor/tables/table_data.cpp
  src/editor/live_editor.h src/editor/live_editor.cpp
  src/editor/live/live_editor_layout.cpp
  src/editor/live/live_editor_tables.cpp
  src/editor/live/live_editor_input.cpp
  src/editor/live/live_editor_paint.cpp
  src/editor/live/live_editor_source.cpp
  src/editor/live/live_editor_render.cpp
  src/editor/live/live_editor_resources.cpp
  src/editor/markdown_editor_widget.h src/editor/markdown_editor_widget.cpp
  src/editor/resources/image_drop.cpp
  src/editor/project_files_panel.h src/editor/project_files_panel.cpp
  src/editor/project_file_tree.h src/editor/project_file_tree.cpp
  src/editor/wiki_navigation.h src/editor/wiki_navigation.cpp
  src/editor/markdown/block_parser.h src/editor/markdown/block_parser.cpp
  src/editor/render/resource_painting.h src/editor/render/resource_painting.cpp
  src/editor/resources/resource_manager.h src/editor/resources/resource_manager.cpp)
add_library(MdEdit::Editor ALIAS MdEditEditor)
target_include_directories(MdEditEditor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(MdEditEditor PUBLIC Qt6::Network Qt6::Widgets)
target_compile_definitions(MdEditEditor PUBLIC MDEDIT_LITE=$<BOOL:${MDEDIT_LITE}>)
if(MSVC)
  target_compile_options(MdEditEditor PRIVATE /utf-8 /W4)
endif()

if(NOT MDEDIT_LITE)
  target_sources(MdEditEditor PRIVATE
    src/editor/view/markdown_preview_widget.h src/editor/view/markdown_preview_widget.cpp
    src/editor/view/pdf_preview_panel.h src/editor/view/pdf_preview_panel.cpp)
  target_link_libraries(MdEditEditor PUBLIC Qt6::Svg Qt6::Pdf Qt6::PdfWidgets MicroTeX::MicroTeX)
endif()
