find_package(Catch2 3 REQUIRED)

add_executable(test_routing test_routing.cpp)
target_link_libraries(test_routing PRIVATE astraea_core Catch2::Catch2WithMain)
# Suppress warnings that are noise in test code (partial struct init, pedantic bytes)
target_compile_options(test_routing PRIVATE -Wno-missing-field-initializers -Wno-pedantic)

add_executable(test_sanitize test_sanitize.cpp)
target_link_libraries(test_sanitize PRIVATE astraea_core Catch2::Catch2WithMain)
target_compile_options(test_sanitize PRIVATE -Wno-missing-field-initializers)

add_executable(test_ac test_ac.cpp)
target_link_libraries(test_ac PRIVATE astraea_core Catch2::Catch2WithMain)
target_compile_options(test_ac PRIVATE -Wno-missing-field-initializers)

add_executable(test_route_table test_route_table.cpp)
target_link_libraries(test_route_table PRIVATE astraea_core Catch2::Catch2WithMain)
target_compile_options(test_route_table PRIVATE -Wno-missing-field-initializers)

add_executable(test_nz_tenancy_routes test_nz_tenancy_routes.cpp)
target_link_libraries(test_nz_tenancy_routes PRIVATE nz_tenancy_routes Catch2::Catch2WithMain)
target_compile_options(test_nz_tenancy_routes PRIVATE -Wno-missing-field-initializers)

include(CTest)
include(Catch)
catch_discover_tests(test_routing)
catch_discover_tests(test_sanitize)
catch_discover_tests(test_ac)
catch_discover_tests(test_route_table)
catch_discover_tests(test_nz_tenancy_routes)
