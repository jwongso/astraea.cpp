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

# Pure-CPU helpers from pipeline.cpp and anchor.cpp (no Drogon, no mocks needed).
add_executable(test_pipeline_helpers test_pipeline_helpers.cpp)
target_link_libraries(test_pipeline_helpers PRIVATE astraea_core Catch2::Catch2WithMain)

# HTTP/SSE parsers - pure logic, no Drogon, lives in astraea_core.
add_executable(test_http_stream_parser test_http_stream_parser.cpp)
target_link_libraries(test_http_stream_parser PRIVATE astraea_core Catch2::Catch2WithMain)
target_compile_options(test_http_stream_parser PRIVATE -Wno-missing-field-initializers)

# AsyncSemaphore tests link astraea_clients (which is where the .cpp lives).
# Gated on ASTRAEA_BUILD_CLIENTS so the parity job (clients=OFF) still builds.
if(ASTRAEA_BUILD_CLIENTS)
    add_executable(test_async_semaphore test_async_semaphore.cpp)
    target_link_libraries(test_async_semaphore PRIVATE astraea_clients Catch2::Catch2WithMain)
    target_compile_options(test_async_semaphore PRIVATE -Wno-missing-field-initializers)

    # CoordinatorClient interface + InProcessCoordinator: same dependency
    # surface (InProcessCoordinator wraps AsyncSemaphore so it pulls trantor
    # transitively through astraea_clients).
    add_executable(test_coordinator test_coordinator.cpp)
    target_link_libraries(test_coordinator PRIVATE astraea_clients Catch2::Catch2WithMain)
    target_compile_options(test_coordinator PRIVATE -Wno-missing-field-initializers)

    # RedisCoordinator integration tests. Self-skips when REDIS_HOST is unset
    # so local dev without a Redis service still builds + runs everything else.
    # CI's test job runs a redis:7-alpine service container.
    add_executable(test_redis_coordinator test_redis_coordinator.cpp)
    target_link_libraries(test_redis_coordinator PRIVATE astraea_clients Catch2::Catch2WithMain)
    target_compile_options(test_redis_coordinator PRIVATE -Wno-missing-field-initializers)
endif()

include(CTest)
include(Catch)
catch_discover_tests(test_routing)
catch_discover_tests(test_sanitize)
catch_discover_tests(test_ac)
catch_discover_tests(test_route_table)
catch_discover_tests(test_nz_tenancy_routes)
catch_discover_tests(test_pipeline_helpers)
catch_discover_tests(test_http_stream_parser)
if(ASTRAEA_BUILD_CLIENTS)
    catch_discover_tests(test_async_semaphore)
    catch_discover_tests(test_coordinator)
    catch_discover_tests(test_redis_coordinator)
endif()
