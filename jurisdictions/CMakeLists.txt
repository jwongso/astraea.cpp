add_library(nz_tenancy_routes STATIC
    nz_tenancy/routes.cpp
)
target_include_directories(nz_tenancy_routes PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/jurisdictions
)
target_link_libraries(nz_tenancy_routes PUBLIC astraea_core)
# Route data uses partial designated init - unset fields default to empty/0 by design.
target_compile_options(nz_tenancy_routes PRIVATE -Wno-missing-field-initializers)
