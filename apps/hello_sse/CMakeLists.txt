add_executable(hello_sse main.cpp)

# Links astraea_clients which transitively brings Drogon (+ glaze, even though
# this spike doesn't use glaze). Validates that astraea_clients is correctly
# wired as a downstream dependency from this app's perspective.
target_link_libraries(hello_sse PRIVATE astraea_clients astraea_warnings)
