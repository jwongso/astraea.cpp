add_executable(nz_tenancy
    main.cpp
    mimalloc_override.cpp
)

target_link_libraries(nz_tenancy PRIVATE
    astraea_clients
    nz_tenancy_routes
    mimalloc
    astraea_warnings
)

# Compile-time timing instrumentation. ON by default so development builds
# always emit the `timing` SSE event. Turn off in production if the JSON
# overhead matters (it rarely does vs. LLM latency).
option(ASTRAEA_ENABLE_TIMING "Emit per-request timing SSE event on /ask/stream" ON)
if(ASTRAEA_ENABLE_TIMING)
    target_compile_definitions(nz_tenancy PRIVATE ASTRAEA_ENABLE_TIMING)
endif()

# Embed an 8-char SHA1 fingerprint of routes.cpp so /healthz can expose it.
# Matches the routes_hash computed by eval_judge.py for cache-key correlation.
#
# This MUST be recomputed every build (not at CMake configure time) because
# routes.cpp can change between configures — and an incremental `cmake --build`
# without a reconfigure would otherwise embed a stale hash, causing /healthz
# to advertise a routes_hash that no longer matches the binary's actual
# compiled-in routes. The probe_losers.py staleness check would then pass
# silently against a stale binary, which is exactly what PR #86 fought to
# prevent.
#
# Solution: a custom command computes the SHA1 into a generated header on
# every build where routes.cpp is newer than the header. main.cpp includes
# that header instead of consuming a -D macro from configure time. The
# add_custom_target wires the dependency so the header is regenerated before
# main.cpp is recompiled.
set(_routes_src "${CMAKE_CURRENT_SOURCE_DIR}/../../jurisdictions/nz_tenancy/routes.cpp")
set(_routes_hash_hdr "${CMAKE_CURRENT_BINARY_DIR}/generated/routes_hash.hpp")
add_custom_command(
    OUTPUT "${_routes_hash_hdr}"
    COMMAND ${CMAKE_COMMAND}
        -D ROUTES_SRC=${_routes_src}
        -D OUTPUT=${_routes_hash_hdr}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_routes_hash.cmake
    DEPENDS "${_routes_src}"
            "${CMAKE_CURRENT_SOURCE_DIR}/generate_routes_hash.cmake"
    COMMENT "Generating routes_hash.hpp from routes.cpp"
    VERBATIM
)
add_custom_target(nz_tenancy_routes_hash
    DEPENDS "${_routes_hash_hdr}"
)
add_dependencies(nz_tenancy nz_tenancy_routes_hash)
target_include_directories(nz_tenancy PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
