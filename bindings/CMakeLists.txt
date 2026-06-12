find_package(pybind11 REQUIRED)

pybind11_add_module(_astraea_cpp bindings.cpp)
target_link_libraries(_astraea_cpp PRIVATE astraea_core)
# Output to build root so `import _astraea_cpp` works from the diff test dir.
set_target_properties(_astraea_cpp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
