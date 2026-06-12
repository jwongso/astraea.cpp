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
