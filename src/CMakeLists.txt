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
    lang/lisp/lisp_adapter.c
)

target_include_directories(codemap-build PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/json
)
target_link_libraries(codemap-build PRIVATE tree-sitter tree_sitter_c tree_sitter_csharp tree_sitter_lisp)

add_executable(codemap-view
    common/pathutil.c
    graph/graph.c
    graph/graph_json.c
    notes/filehash.c
    notes/overlay.c
    notes/notes.c
    render/main.c
    render/camera.c
    render/layout3d.c
    render/gl_scene.c
    render/picking.c
    render/ui_panel.c
    render/labels.c
)
target_include_directories(codemap-view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/render
    ${CMAKE_SOURCE_DIR}/vendor/nuklear
    ${CMAKE_SOURCE_DIR}/vendor/json
)
target_link_libraries(codemap-view PRIVATE glfw OpenGL::GL)

if(NOT APPLE)
    # macOS links GL 3.2+ entry points directly from OpenGL.framework (see
    # gl_compat.h); everywhere else needs a runtime loader. Written but not
    # run on an actual Windows/Linux machine -- none exists in this
    # environment -- so this is "should work," not verified.
    add_library(glad STATIC ${CMAKE_SOURCE_DIR}/vendor/glad/src/gl.c)
    target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/vendor/glad/include)
    target_link_libraries(codemap-view PRIVATE glad)
    target_include_directories(codemap-view PRIVATE ${CMAKE_SOURCE_DIR}/vendor/glad/include)
endif()

# Distributable macOS build: `cmake -S . -B build-xcode -G Xcode` turns
# codemap-view into a real, Developer-ID-signed .app you can hand to another
# Mac (and signs codemap-build too, as a plain binary -- it has no window to
# bundle). Gated on the Xcode generator specifically so the everyday
# `cmake -S . -B build` (Unix Makefiles/Ninja) dev build keeps producing a
# bare, ad-hoc-signed executable exactly as it always has.
if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(codemap-view PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Codestellation"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.robrohan.codestellation.codemap-view"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
    )
    foreach(_codemap_target codemap-view codemap-build)
        set_target_properties(${_codemap_target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "96MW2MVPWL"
            XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
        )
    endforeach()
endif()
