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
