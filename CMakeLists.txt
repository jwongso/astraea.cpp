cmake_minimum_required(VERSION 3.28)
project(astraea CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Warnings — common to all configs
# ---------------------------------------------------------------------------
add_compile_options(-Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Release: optimise for production (Ryzen AI 9 HX 370 / Zen 5 = znver5).
#   -march=znver5  full AVX-512 + VNNI + BF16 available on this CPU
#   -flto=thin     Clang thin-LTO; link time is reasonable even at this scale
#   Use -march=native only for local benchmarking, never in a committed config.
#
# ASTRAEA_NATIVE_OPTS gates these so CI runs (Intel / generic x86-64) and
# any other machine can build Release without znver5-specific codegen.
# ---------------------------------------------------------------------------
add_compile_options($<$<CONFIG:Release>:-O3>)

option(ASTRAEA_NATIVE_OPTS "Apply -march=znver5 and -flto=thin in Release builds" ON)
if(ASTRAEA_NATIVE_OPTS)
    add_compile_options($<$<CONFIG:Release>:-flto=thin>)
    add_compile_options($<$<CONFIG:Release>:-march=znver5>)
    add_link_options($<$<CONFIG:Release>:-flto=thin>)
endif()

# ---------------------------------------------------------------------------
# Debug: full sanitisers. -Werror is applied per-target (not globally) so
# it does not bleed into CPM-fetched third-party sources like Drogon.
# ---------------------------------------------------------------------------
add_compile_options($<$<CONFIG:Debug>:-O0>)
add_compile_options($<$<CONFIG:Debug>:-g>)
add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)

# Interface target: apply -Werror only to our own code.
add_library(astraea_warnings INTERFACE)
target_compile_options(astraea_warnings INTERFACE
    $<$<CONFIG:Debug>:-Werror>
)

find_package(spdlog REQUIRED)

# Tests are opt-out so the CI parity job and any other "core only" build can
# skip Catch2 (which is not declared as a parity-job apt dep). astraea_core
# itself has no test framework dependency.
option(ASTRAEA_BUILD_TESTS "Build Catch2 unit tests under tests/" ON)
if(ASTRAEA_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED)
endif()

# RE2: prefer CMake config (vcpkg / upstream build) and fall back to pkg-config
# (Debian/Ubuntu libre2-dev ships pkg-config but omits the CMake config).
# Both paths produce the same re2::re2 target.
find_package(re2 CONFIG QUIET)
if(NOT re2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RE2 REQUIRED IMPORTED_TARGET re2)
    add_library(re2::re2 ALIAS PkgConfig::RE2)
endif()

# ---------------------------------------------------------------------------
# CPM.cmake — package manager for Drogon and future deps.
# ---------------------------------------------------------------------------
include(cmake/CPM.cmake)

# astraea_clients (and the Drogon + glaze fetches that back it) is opt-out so
# the CI parity job and any other "core only" build can skip the 5-minute
# Drogon clone+build. astraea_core itself has no Drogon dependency.
option(ASTRAEA_BUILD_CLIENTS "Build the HTTP client library and pull in Drogon + glaze" ON)

if(ASTRAEA_BUILD_CLIENTS)
    CPMAddPackage(
        NAME drogon
        GITHUB_REPOSITORY drogonframework/drogon
        GIT_TAG v1.9.7
        OPTIONS
            "BUILD_EXAMPLES OFF"
            "BUILD_CTL OFF"
            "BUILD_TESTING OFF"
            "BUILD_DROGON_SHARED OFF"
    )
    # Mark Drogon as a SYSTEM library (CMake 3.25+) so its headers do not
    # trigger warnings in our TUs. Cleaner than enumerating include dirs.
    if(drogon_ADDED)
        set_property(TARGET drogon PROPERTY SYSTEM ON)
    endif()

    # glaze: header-only C++23 JSON serializer used by Phase 3 HTTP clients.
    CPMAddPackage(
        NAME glaze
        GITHUB_REPOSITORY stephenberry/glaze
        GIT_TAG v4.4.3
        OPTIONS "glaze_ENABLE_AVX2 OFF"
    )
endif()

# TODO(Phase5): wire mimalloc as process allocator once main.cpp exists.
# Linking into a static archive does not reliably override malloc/new —
# the override requires an explicit TU with <mimalloc-new-delete.h> compiled
# into each final executable, verified by mi_is_in_heap_region().

# Core library — routing + sanitize, zero network deps
add_library(astraea_core STATIC
  src/aho_corasick.cpp
  src/routing.cpp
  src/sanitize.cpp
)
target_include_directories(astraea_core PUBLIC include)
target_link_libraries(astraea_core PUBLIC spdlog::spdlog astraea_warnings)
# RE2 is used only inside src/sanitize.cpp — keep it private so consumers
# of astraea_core don't inherit the include path.
target_link_libraries(astraea_core PRIVATE re2::re2)
# Required so astraea_core can be linked into the pybind11 shared module.
set_target_properties(astraea_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ASTRAEA_BUILD_CLIENTS)
    # HTTP client + pipeline library.
    # Phase 3: embedder, retriever, generator, reranker (async HTTP via Drogon).
    # Phase 5: pipeline (RAGPipeline), anchor (legislation anchor + guidance).
    add_library(astraea_clients STATIC
      src/embedder.cpp
      src/retriever.cpp
      src/generator.cpp
      src/reranker.cpp
      src/pipeline.cpp
      src/anchor.cpp
    )
    target_link_libraries(astraea_clients PUBLIC astraea_core drogon glaze::glaze astraea_warnings)
endif()

option(ASTRAEA_BUILD_BINDINGS "Build pybind11 Python bindings" ON)
if(ASTRAEA_BUILD_BINDINGS)
    add_subdirectory(bindings)
endif()

add_subdirectory(jurisdictions)

# Apps (jurisdiction binaries + spikes). Requires astraea_clients because every
# app pulls in Drogon + the HTTP client wrappers. Skip when clients are off
# (parity CI / lean builds).
option(ASTRAEA_BUILD_APPS "Build apps/ (hello_sse spike + future jurisdiction binaries)" ON)
if(ASTRAEA_BUILD_APPS AND ASTRAEA_BUILD_CLIENTS)
    add_subdirectory(apps)
endif()

option(ASTRAEA_BUILD_BENCH "Build microbenchmarks (use prod preset only)" OFF)
if(ASTRAEA_BUILD_BENCH)
    add_subdirectory(tools)
endif()

if(ASTRAEA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
