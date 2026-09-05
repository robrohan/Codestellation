add_executable(codemap-build
    common/pathutil.c
    common/symtab.c
    graph/graph.c
    graph/graph_json.c
    pipeline/walk.c
    pipeline/parse.c
    pipeline/resolve.c
    pipeline/build_graph_cli.c
    lang/registry.c
    lang/c/c_adapter.c
    lang/csharp/csharp_adapter.c
)

target_include_directories(codemap-build PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/json
)
target_link_libraries(codemap-build PRIVATE tree-sitter tree_sitter_c tree_sitter_csharp)

add_executable(codemap-view
    common/pathutil.c
    graph/graph.c
    graph/graph_json.c
    render/main.c
    render/camera.c
    render/layout3d.c
    render/gl_scene.c
    render/picking.c
    render/ui_panel.c
)
target_include_directories(codemap-view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/render
    ${CMAKE_SOURCE_DIR}/vendor/nuklear
    ${CMAKE_SOURCE_DIR}/vendor/json
)
target_link_libraries(codemap-view PRIVATE glfw OpenGL::GL)
