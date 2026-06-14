add_library(nz_tenancy_routes STATIC
    nz_tenancy/routes.cpp
    nz_tenancy/jurisdiction.cpp
)
target_include_directories(nz_tenancy_routes PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/jurisdictions
)
target_link_libraries(nz_tenancy_routes PUBLIC astraea_core)
# Route data + jurisdiction config use partial designated init - unset fields
# default to empty/0 by design. -Wmissing-designated-field-initializers is a
# Clang 21+ split of -Wmissing-field-initializers; probe before use so older
# CI compilers don't error on an unknown -Wno-* flag.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-missing-designated-field-initializers
    _WNO_MISSING_DESIGNATED_SUPPORTED)
target_compile_options(nz_tenancy_routes PRIVATE
    -Wno-missing-field-initializers
    $<$<BOOL:${_WNO_MISSING_DESIGNATED_SUPPORTED}>:-Wno-missing-designated-field-initializers>
)
