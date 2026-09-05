cmake_minimum_required(VERSION 3.21)
project(codemap3d C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

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
