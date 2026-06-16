add_executable(building_consents
    main.cpp
    mimalloc_override.cpp
)

target_link_libraries(building_consents PRIVATE
    astraea_clients
    nz_building_routes
    mimalloc
    astraea_warnings
)

option(ASTRAEA_ENABLE_TIMING "Emit per-request timing SSE event on /ask/stream" ON)
if(ASTRAEA_ENABLE_TIMING)
    target_compile_definitions(building_consents PRIVATE ASTRAEA_ENABLE_TIMING)
endif()
