# One binary: the viewer builds project graphs in-process (see
# render/project_build.c), so the tree-sitter pipeline is linked straight in.
add_executable(Codestellation
    common/pathutil.c
    common/symtab.c
    common/thread.c
    graph/graph.c
    graph/graph_json.c
    pipeline/walk.c
    pipeline/parse.c
    pipeline/resolve.c
    pipeline/build_graph.c
    lang/registry.c
    lang/c/c_adapter.c
    lang/csharp/csharp_adapter.c
    lang/lisp/lisp_adapter.c
    lang/python/python_adapter.c
    lang/go/go_adapter.c
    lang/php/php_adapter.c
    lang/vbnet/vbnet_adapter.c
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
    render/project_build.c
    render/labels.c
    render/panel_rect.c
    render/theme.c
    ${CMAKE_SOURCE_DIR}/vendor/tinyfiledialogs/tinyfiledialogs.c
)
target_include_directories(Codestellation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/render
    ${CMAKE_SOURCE_DIR}/vendor/nuklear
    ${CMAKE_SOURCE_DIR}/vendor/json
    ${CMAKE_SOURCE_DIR}/vendor/tinyfiledialogs
)
find_package(Threads REQUIRED)
target_link_libraries(Codestellation PRIVATE glfw OpenGL::GL Threads::Threads
    tree-sitter tree_sitter_c tree_sitter_csharp tree_sitter_lisp tree_sitter_python
    tree_sitter_go tree_sitter_php tree_sitter_vbnet)

# The CMake *target* is named Codestellation (so the Xcode generator's
# auto-created scheme/product picker shows that instead of a stale
# "codemap-view" left over from when the target itself was named that),
# but the everyday `cmake -B build` (Unix Makefiles/Ninja) dev build should
# keep producing the same plain "codemap-view" executable it always has --
# nothing else references the target name, only this OUTPUT_NAME, and nothing
# in this codebase's runtime logic depends on it either way. The Xcode-gated
# block below
# overrides this back to "Codestellation" for that build specifically.
set_target_properties(Codestellation PROPERTIES OUTPUT_NAME "codemap-view")

if(NOT APPLE)
    # macOS links GL 3.2+ entry points directly from OpenGL.framework (see
    # gl_compat.h); everywhere else needs a runtime loader. Written but not
    # run on an actual Windows/Linux machine -- none exists in this
    # environment -- so this is "should work," not verified.
    add_library(glad STATIC ${CMAKE_SOURCE_DIR}/vendor/glad/src/gl.c)
    target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/vendor/glad/include)
    target_link_libraries(Codestellation PRIVATE glad)
    target_include_directories(Codestellation PRIVATE ${CMAKE_SOURCE_DIR}/vendor/glad/include)
endif()

# macOS Xcode build: `cmake -S . -B build-xcode -G Xcode` turns Codestellation
# into a real .app bundle.
# Gated on the Xcode generator specifically so the everyday
# `cmake -S . -B build` (Unix Makefiles/Ninja) dev build keeps producing a
# bare, ad-hoc-signed executable exactly as it always has.
#
# By default this bundle is ad-hoc signed with hardened runtime OFF, so it
# runs straight from Xcode's Run button (hardened runtime refuses the
# DYLD_* injection Xcode's debugger relies on).
#
# Pass -DCODESTELLATION_DIST=ON for a Developer-ID-signed, hardened-runtime
# .app you can hand to another Mac. Don't Run that one from Xcode -- build it,
# then launch the .app directly (Finder / `open`), where no DYLD_* injection
# happens.
option(CODESTELLATION_DIST "macOS: Developer-ID sign + hardened runtime for distribution" OFF)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(Codestellation PROPERTIES
        OUTPUT_NAME "Codestellation"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Codestellation"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.robrohan.codestellation.codemap-view"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
        # install(BUNDLE DESTINATION ".") below made CMake's Xcode generator
        # emit an *empty* INSTALL_PATH build setting rather than a real
        # path (confirmed directly by inspecting the generated .pbxproj),
        # which is why Product > Archive built and even code-signed
        # Codestellation.app in its intermediate location but left the
        # archive's own Products/ folder empty -- Xcode had nowhere to put
        # it. $(LOCAL_APPS_DIR) is the same Installation Directory value
        # Xcode gives any normal macOS app target (resolves to
        # /Applications), which is what an Archive needs regardless of
        # whether anyone ever actually runs `cmake --install` locally.
        XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    )

    if(CODESTELLATION_DIST)
        set_target_properties(Codestellation PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "96MW2MVPWL"
            XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
        )
    else()
        # Local dev: sign to run locally, no hardened runtime.
        set_target_properties(Codestellation PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
            XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "NO"
        )
    endif()

    # Xcode's Archive action only copies a target's product into the
    # resulting .xcarchive's Products/ folder for targets with an
    # install() rule -- CMake's Xcode generator otherwise marks a target
    # "Skip Install = YES" by default, since this project had no install()
    # rules anywhere. Without this, `xcodebuild archive`/Product > Archive
    # reports success and even code-signs Codestellation.app in its
    # intermediate build location (confirmed directly: an archive attempt
    # without this line left Products/ completely empty), but Xcode's
    # Organizer then has no actual .app to sign for export or distribute --
    # which is exactly "Archive doesn't make a .app" / "can't sign the
    # binary".
    install(TARGETS Codestellation BUNDLE DESTINATION .)
endif()
