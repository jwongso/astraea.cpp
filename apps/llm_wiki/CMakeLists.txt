add_executable(llm_wiki
    main.cpp
    wiki_store.cpp
    wiki_ops.cpp
)

target_include_directories(llm_wiki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Bake the static directory path into the binary so wiki.html is served
# without any runtime ASTRAEA_SRC env var.
execute_process(
    COMMAND git describe --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE _WIKI_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT _WIKI_GIT_HASH)
    set(_WIKI_GIT_HASH "unknown")
endif()

target_compile_definitions(llm_wiki PRIVATE
    WIKI_STATIC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/static"
    WIKI_GIT_HASH="${_WIKI_GIT_HASH}"
)

target_link_libraries(llm_wiki PRIVATE
    astraea_clients
    astraea_warnings
)
