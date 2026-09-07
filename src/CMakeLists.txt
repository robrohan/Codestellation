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
    render/dirgroup.c
    render/dircolor.c
    render/gl_scene.c
    render/picking.c
    render/ui_panel.c
    render/note_compose.c
    render/properties_panel.c
    render/project_launcher.c
    render/labels.c
    ${CMAKE_SOURCE_DIR}/vendor/tinyfiledialogs/tinyfiledialogs.c
)
target_include_directories(codemap-view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/render
    ${CMAKE_SOURCE_DIR}/vendor/nuklear
    ${CMAKE_SOURCE_DIR}/vendor/json
    ${CMAKE_SOURCE_DIR}/vendor/tinyfiledialogs
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

# macOS Xcode build: `cmake -S . -B build-xcode -G Xcode` turns codemap-view
# into a real .app bundle (and embeds codemap-build next to it -- see below).
# Gated on the Xcode generator specifically so the everyday
# `cmake -S . -B build` (Unix Makefiles/Ninja) dev build keeps producing a
# bare, ad-hoc-signed executable exactly as it always has.
#
# By default this bundle is ad-hoc signed with hardened runtime OFF, so it
# runs straight from Xcode's Run button. Hardened runtime + Xcode's injected
# DYLD_* environment is fatal to the Properties > Open flow: that path
# execv()s a fresh copy of codemap-view (project_launcher.c), the child
# inherits Xcode's DYLD_INSERT_LIBRARIES etc., and dyld aborts it on launch
# because hardened runtime forbids those insertions.
#
# Pass -DCODESTELLATION_DIST=ON for a Developer-ID-signed, hardened-runtime
# .app you can hand to another Mac. Don't Run that one from Xcode -- build it,
# then launch the .app directly (Finder / `open`), where no DYLD_* injection
# happens.
option(CODESTELLATION_DIST "macOS: Developer-ID sign + hardened runtime for distribution" OFF)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(codemap-view PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Codestellation"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.robrohan.codestellation.codemap-view"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
    )

    if(CODESTELLATION_DIST)
        foreach(_codemap_target codemap-view codemap-build)
            set_target_properties(${_codemap_target} PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "96MW2MVPWL"
                XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
            )
        endforeach()
    else()
        # Local dev: sign to run locally, no hardened runtime.
        foreach(_codemap_target codemap-view codemap-build)
            set_target_properties(${_codemap_target} PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
                XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "NO"
            )
        endforeach()
    endif()

    # codemap-view's Properties pane can build+open an arbitrary directory
    # at runtime (project_launcher.c), which shells out to a sibling
    # `codemap-build` found next to its own executable -- true for the
    # everyday dev build (both land in the same flat output directory),
    # but NOT for this bundle: codemap-view's real executable lives at
    # Contents/MacOS/codemap-view, disconnected from wherever
    # codemap-build was built. Embed a copy of codemap-build right next to
    # it so the same "look next to me" runtime logic keeps working
    # unmodified, then re-sign the whole bundle -- signing must happen
    # *after* embedding or the bundle's own seal won't cover the
    # copied-in binary.
    # For a distribution build, a literal "Developer ID Application"
    # identity string is ambiguous on a keychain holding more than one
    # certificate with that same display name (a stale/renewed duplicate,
    # e.g.) -- codesign then refuses to guess. $EXPANDED_CODE_SIGN_IDENTITY
    # is the specific identity Xcode itself already resolved
    # CODE_SIGN_IDENTITY to for this very target (exported into every Run
    # Script phase's environment), so reusing it here can't be ambiguous.
    # For a local build that resolves to "-" (ad-hoc), which is equally
    # unambiguous.
    add_custom_command(TARGET codemap-view POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:codemap-build>
            $<TARGET_FILE_DIR:codemap-view>/codemap-build
        COMMAND /bin/sh -c "codesign --force --deep --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" '$<TARGET_BUNDLE_DIR:codemap-view>'"
        COMMENT "Embedding codemap-build into codemap-view.app and re-sealing the bundle"
    )
endif()
