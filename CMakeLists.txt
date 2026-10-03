cmake_minimum_required(VERSION 3.21)

# macOS: both of these must be set before project(). Without a deployment
# target the binary's minimum OS is whatever SDK built it (i.e. the build
# machine's macOS), so a signed .app wouldn't launch on anything older.
# 12.0 is the oldest current Xcode (27) will target. The Xcode build -- the one
# that gets signed and handed to other Macs -- is universal by default;
# the everyday Makefile/Ninja build stays native-only for speed. Both can
# be overridden with -D on the command line.
# (FORCE only when empty: a build folder configured before this existed
# has an empty entry cached, which a plain CACHE set wouldn't replace.)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
if(CMAKE_GENERATOR STREQUAL "Xcode" AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

project(Codestellation C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# tree-sitter's own CMakeLists.txt does `option(BUILD_SHARED_LIBS ... ON)`,
# which -- being a CACHE variable -- silently turns on shared-library
# building for every FetchContent dependency pulled in after it (GLFW
# included), not just tree-sitter itself. On Windows that's fatal: none of
# tree-sitter's public symbols carry __declspec(dllexport), so MSVC emits
# the .dll but no import .lib, and anything linking against it fails with
# LNK1181. Force static everywhere before any dependency gets a chance to
# flip this.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

# --- tree-sitter core runtime -------------------------------------------
set(TREE_SITTER_FEATURE_WASM OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter
    GIT_TAG v0.27.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tree_sitter)

# --- tree-sitter-c grammar (Phase 1 smoke-test language) -----------------
# No upstream CMakeLists.txt to rely on for this one; the checked-in
# parser.c is a plain C source we compile directly.
FetchContent_Declare(ts_c
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c
    GIT_TAG v0.24.1
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_c)
add_library(tree_sitter_c STATIC ${ts_c_SOURCE_DIR}/src/parser.c)
if(EXISTS ${ts_c_SOURCE_DIR}/src/scanner.c)
    target_sources(tree_sitter_c PRIVATE ${ts_c_SOURCE_DIR}/src/scanner.c)
endif()
target_include_directories(tree_sitter_c PUBLIC ${ts_c_SOURCE_DIR}/src)

# --- tree-sitter-c-sharp grammar (Phase 3) --------------------------------
# Large generated parser.c (tens of MB) -- shallow clone pinned to a tag
# keeps this cheap.
FetchContent_Declare(ts_csharp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c-sharp
    GIT_TAG v0.23.5
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_csharp)
add_library(tree_sitter_csharp STATIC ${ts_csharp_SOURCE_DIR}/src/parser.c)
if(EXISTS ${ts_csharp_SOURCE_DIR}/src/scanner.c)
    target_sources(tree_sitter_csharp PRIVATE ${ts_csharp_SOURCE_DIR}/src/scanner.c)
endif()
target_include_directories(tree_sitter_csharp PUBLIC ${ts_csharp_SOURCE_DIR}/src)

# --- tree-sitter-commonlisp grammar (Phase 6, Lisp adapter) --------------
# WIP grammar with no CL-specific node types (see lisp_adapter.c) -- the
# best available stand-in until the actual Lisp dialect is confirmed.
FetchContent_Declare(ts_lisp
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-commonlisp
    GIT_TAG v0.4.1
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_lisp)
add_library(tree_sitter_lisp STATIC ${ts_lisp_SOURCE_DIR}/src/parser.c)
if(EXISTS ${ts_lisp_SOURCE_DIR}/src/scanner.c)
    target_sources(tree_sitter_lisp PRIVATE ${ts_lisp_SOURCE_DIR}/src/scanner.c)
endif()
target_include_directories(tree_sitter_lisp PUBLIC ${ts_lisp_SOURCE_DIR}/src)

# --- tree-sitter-python grammar (Python adapter) ------------------------
# Generated parser.c + a small external scanner. Shallow clone pinned to a
# tag, same as the others.
FetchContent_Declare(ts_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python
    GIT_TAG v0.23.6
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_python)
add_library(tree_sitter_python STATIC ${ts_python_SOURCE_DIR}/src/parser.c)
if(EXISTS ${ts_python_SOURCE_DIR}/src/scanner.c)
    target_sources(tree_sitter_python PRIVATE ${ts_python_SOURCE_DIR}/src/scanner.c)
endif()
target_include_directories(tree_sitter_python PUBLIC ${ts_python_SOURCE_DIR}/src)

# --- tree-sitter-go grammar (Go adapter) -------------------------------
# Generated parser.c, no external scanner. Shallow clone pinned to a tag.
FetchContent_Declare(ts_go
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-go
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_go)
add_library(tree_sitter_go STATIC ${ts_go_SOURCE_DIR}/src/parser.c)
if(EXISTS ${ts_go_SOURCE_DIR}/src/scanner.c)
    target_sources(tree_sitter_go PRIVATE ${ts_go_SOURCE_DIR}/src/scanner.c)
endif()
target_include_directories(tree_sitter_go PUBLIC ${ts_go_SOURCE_DIR}/src)

# --- tree-sitter-php grammar (PHP adapter) -------------------------------
# Upstream repo hosts two grammars (HTML+embedded-PHP, and PHP-only) side by
# side; php_only is the one used here since this adapter only cares about
# code that's always inside <?php ?> tags (namespaces/classes/use/require)
# and never needs the HTML-embedding half. Has an external scanner.
FetchContent_Declare(ts_php
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-php
    GIT_TAG v0.24.2
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_php)
add_library(tree_sitter_php STATIC ${ts_php_SOURCE_DIR}/php_only/src/parser.c)
if(EXISTS ${ts_php_SOURCE_DIR}/php_only/src/scanner.c)
    target_sources(tree_sitter_php PRIVATE ${ts_php_SOURCE_DIR}/php_only/src/scanner.c)
endif()
target_include_directories(tree_sitter_php PUBLIC ${ts_php_SOURCE_DIR}/php_only/src)

# --- tree-sitter-vb-dotnet grammar (Visual Basic .NET adapter) -----------
# No tagged release exists yet (young, unofficial grammar -- the only
# actively maintained VB.NET grammar for tree-sitter at the time this was
# added) so this is pinned to a specific commit rather than a tag, and not
# shallow-cloned since GitHub's support for shallow-fetching an arbitrary
# commit (as opposed to a branch/tag ref) isn't guaranteed. No external
# scanner.
FetchContent_Declare(ts_vbnet
    GIT_REPOSITORY https://github.com/CodeAnt-AI/tree-sitter-vb-dotnet
    GIT_TAG cfca210ce8fdcb5245bd9cd5c47ce0a21a8488d5
)
FetchContent_Populate(ts_vbnet)
add_library(tree_sitter_vbnet STATIC ${ts_vbnet_SOURCE_DIR}/src/parser.c)
if(EXISTS ${ts_vbnet_SOURCE_DIR}/src/scanner.c)
    target_sources(tree_sitter_vbnet PRIVATE ${ts_vbnet_SOURCE_DIR}/src/scanner.c)
endif()
target_include_directories(tree_sitter_vbnet PUBLIC ${ts_vbnet_SOURCE_DIR}/src)

# --- tree-sitter-javascript grammar (JavaScript/JSX adapter) ----------
# One grammar covers plain JS and JSX. Has an external scanner.
FetchContent_Declare(ts_javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript
    GIT_TAG v0.25.0
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_javascript)
add_library(tree_sitter_javascript STATIC
    ${ts_javascript_SOURCE_DIR}/src/parser.c
    ${ts_javascript_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_javascript PUBLIC ${ts_javascript_SOURCE_DIR}/src)

# --- tree-sitter-typescript grammars (TypeScript + TSX adapters) --------
# The repo holds two grammars side by side: typescript/ (.ts) and tsx/
# (.tsx, TypeScript plus JSX). Both scanners include ../../common/scanner.h,
# which resolves in place, so they're compiled from where they sit.
FetchContent_Declare(ts_typescript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript
    GIT_TAG v0.23.2
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_typescript)
add_library(tree_sitter_typescript STATIC
    ${ts_typescript_SOURCE_DIR}/typescript/src/parser.c
    ${ts_typescript_SOURCE_DIR}/typescript/src/scanner.c)
target_include_directories(tree_sitter_typescript PUBLIC ${ts_typescript_SOURCE_DIR}/typescript/src)
add_library(tree_sitter_tsx STATIC
    ${ts_typescript_SOURCE_DIR}/tsx/src/parser.c
    ${ts_typescript_SOURCE_DIR}/tsx/src/scanner.c)
target_include_directories(tree_sitter_tsx PUBLIC ${ts_typescript_SOURCE_DIR}/tsx/src)

# --- tree-sitter-json grammar (JSON adapter) ---------------------------
# Generated parser.c, no external scanner.
FetchContent_Declare(ts_json
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-json
    GIT_TAG v0.24.8
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_json)
add_library(tree_sitter_json STATIC ${ts_json_SOURCE_DIR}/src/parser.c)
target_include_directories(tree_sitter_json PUBLIC ${ts_json_SOURCE_DIR}/src)

# --- tree-sitter-bash grammar (shell script adapter) -------------------
FetchContent_Declare(ts_bash
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-bash
    GIT_TAG v0.25.1
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_bash)
add_library(tree_sitter_bash STATIC
    ${ts_bash_SOURCE_DIR}/src/parser.c
    ${ts_bash_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_bash PUBLIC ${ts_bash_SOURCE_DIR}/src)

# --- tree-sitter-proto grammar (Protocol Buffers adapter) --------------
# Not in the tree-sitter org; coder3101's is the maintained one. Generated
# parser.c, no external scanner.
FetchContent_Declare(ts_proto
    GIT_REPOSITORY https://github.com/coder3101/tree-sitter-proto
    GIT_TAG 0.6.0
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_proto)
add_library(tree_sitter_proto STATIC ${ts_proto_SOURCE_DIR}/src/parser.c)
target_include_directories(tree_sitter_proto PUBLIC ${ts_proto_SOURCE_DIR}/src)

# --- tree-sitter-sql grammar (SQL adapter) ------------------------------
# DerekStride's grammar (the maintained general-purpose SQL one) doesn't
# commit its generated parser.c to main or its tags -- it's published to
# the gh-pages branch by CI. A moving branch isn't reproducible, so this
# pins one gh-pages commit (deploy of main@97614d0, 2026-09-19) and fetches
# it as a tarball with a checksum. To update: pick a newer gh-pages commit
# and replace both the SHA in the URL and the hash. Large parser.c (~40MB),
# like C#'s. Has an external scanner.
FetchContent_Declare(ts_sql
    URL https://github.com/DerekStride/tree-sitter-sql/archive/39fdb006403747241244326e8af3b3e96b85381c.tar.gz
    URL_HASH SHA256=3926262684353dff4979c451c9db80e81918cab94cbd3842117e3ea4fbaa4941
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Populate(ts_sql)
add_library(tree_sitter_sql STATIC
    ${ts_sql_SOURCE_DIR}/src/parser.c
    ${ts_sql_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_sql PUBLIC ${ts_sql_SOURCE_DIR}/src)

# --- tree-sitter-powershell grammar (PowerShell adapter) ----------------
# airbus-cert's is the maintained PowerShell grammar. Generated parser.c +
# an external scanner.
FetchContent_Declare(ts_powershell
    GIT_REPOSITORY https://github.com/airbus-cert/tree-sitter-powershell
    GIT_TAG v0.26.5
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_powershell)
add_library(tree_sitter_powershell STATIC
    ${ts_powershell_SOURCE_DIR}/src/parser.c
    ${ts_powershell_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_powershell PUBLIC ${ts_powershell_SOURCE_DIR}/src)

# --- tree-sitter-batch grammar (Windows batch / cmd adapter) -------------
# wharflab's grammar: handles %~dp0, call "...", ^ continuations, etc.
# Generated parser.c, no external scanner.
FetchContent_Declare(ts_batch
    GIT_REPOSITORY https://github.com/wharflab/tree-sitter-batch
    GIT_TAG v0.11.1
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_batch)
add_library(tree_sitter_batch STATIC ${ts_batch_SOURCE_DIR}/src/parser.c)
target_include_directories(tree_sitter_batch PUBLIC ${ts_batch_SOURCE_DIR}/src)

# --- tree-sitter-html grammar (HTML adapter) ----------------------------
# Generated parser.c + an external scanner (tag matching).
FetchContent_Declare(ts_html
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-html
    GIT_TAG v0.23.2
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_html)
add_library(tree_sitter_html STATIC
    ${ts_html_SOURCE_DIR}/src/parser.c
    ${ts_html_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_html PUBLIC ${ts_html_SOURCE_DIR}/src)

# --- tree-sitter-css grammar (CSS adapter) ------------------------------
# Generated parser.c + an external scanner.
FetchContent_Declare(ts_css
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-css
    GIT_TAG v0.25.0
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_css)
add_library(tree_sitter_css STATIC
    ${ts_css_SOURCE_DIR}/src/parser.c
    ${ts_css_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_css PUBLIC ${ts_css_SOURCE_DIR}/src)

# --- tree-sitter-markdown grammars (Markdown adapter) --------------------
# One repo, two grammars: the block grammar the pipeline parses each file
# with, and the inline grammar the adapter runs over the block tree's
# inline ranges (links live there). Each has its own external scanner.
FetchContent_Declare(ts_markdown
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-markdown
    GIT_TAG v0.5.3
    GIT_SHALLOW TRUE
)
FetchContent_Populate(ts_markdown)
add_library(tree_sitter_markdown STATIC
    ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown/src/parser.c
    ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown/src/scanner.c)
target_include_directories(tree_sitter_markdown PUBLIC ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown/src)
add_library(tree_sitter_markdown_inline STATIC
    ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown-inline/src/parser.c
    ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown-inline/src/scanner.c)
target_include_directories(tree_sitter_markdown_inline PUBLIC ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown-inline/src)

# --- GLFW (window/input/GL context, cross-platform) ----------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

add_subdirectory(src)
