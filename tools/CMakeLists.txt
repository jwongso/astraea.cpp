# Microbenchmarks — opt-in, never compiled in CI.
#
# Enable with:  cmake --preset prod -DASTRAEA_BUILD_BENCH=ON

add_executable(bench_sanitize bench_sanitize.cpp)

target_include_directories(bench_sanitize PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/nanobench
)
target_link_libraries(bench_sanitize PRIVATE re2::re2)
# nanobench uses SIGPROF / clock_gettime, no extra libs needed on Linux.

# nanobench.h triggers a flood of -Wpedantic warnings from inside the header;
# silence them locally so the bench TU itself can still be -Werror-clean.
target_compile_options(bench_sanitize PRIVATE
    -Wno-pedantic
    -Wno-missing-field-initializers
)
