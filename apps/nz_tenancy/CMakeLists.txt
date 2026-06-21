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
file(SHA1 "${CMAKE_CURRENT_SOURCE_DIR}/../../jurisdictions/nz_tenancy/routes.cpp"
     _routes_sha1)
string(SUBSTRING "${_routes_sha1}" 0 8 _routes_hash)
target_compile_definitions(nz_tenancy PRIVATE
    ASTRAEA_ROUTES_HASH="${_routes_hash}"
)
