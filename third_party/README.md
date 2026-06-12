# Vendored third-party headers

Single-header dependencies vendored into the repo to keep the build hermetic
(no network at configure time, no version drift across machines).

## nanobench/

* Source: <https://github.com/martinus/nanobench>
* Version: v4.3.11
* License: MIT (see `nanobench/LICENSE`)
* Used by: `tools/bench_sanitize.cpp` and any future microbenchmarks.

To update: download `src/include/nanobench.h` from a newer release tag and
overwrite `nanobench/nanobench.h`. No CMake change needed.
