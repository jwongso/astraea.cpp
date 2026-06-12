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
# ---------------------------------------------------------------------------
add_compile_options($<$<CONFIG:Release>:-O3>)
add_compile_options($<$<CONFIG:Release>:-flto=thin>)
add_compile_options($<$<CONFIG:Release>:-march=znver5>)
add_link_options($<$<CONFIG:Release>:-flto=thin>)

# ---------------------------------------------------------------------------
# Debug: full sanitisers + Werror so warnings don't accumulate.
# ---------------------------------------------------------------------------
add_compile_options($<$<CONFIG:Debug>:-O0>)
add_compile_options($<$<CONFIG:Debug>:-g>)
add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
add_compile_options($<$<CONFIG:Debug>:-Werror>)
add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)

find_package(spdlog REQUIRED)
find_package(Catch2 3 REQUIRED)
find_package(re2 CONFIG REQUIRED)

# TODO(deps): migrate to CPM.cmake and switch from Catch2 to doctest for
# faster compile times once Drogon lands (PLAN_feedback §4.7/4.8).

# Core library — routing + sanitize, zero network deps
add_library(astraea_core STATIC
  src/aho_corasick.cpp
  src/routing.cpp
  src/sanitize.cpp
)
target_include_directories(astraea_core PUBLIC include)
target_link_libraries(astraea_core PUBLIC spdlog::spdlog)
# RE2 is used only inside src/sanitize.cpp — keep it private so consumers
# of astraea_core don't inherit the include path.
target_link_libraries(astraea_core PRIVATE re2::re2)
# Required so astraea_core can be linked into the pybind11 shared module.
set_target_properties(astraea_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(ASTRAEA_BUILD_BINDINGS "Build pybind11 Python bindings" ON)
if(ASTRAEA_BUILD_BINDINGS)
    add_subdirectory(bindings)
endif()

enable_testing()
add_subdirectory(tests)
