cmake_minimum_required(VERSION 3.21)
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
