#pragma once
// MCP Streamable-HTTP server for the nz_tenancy jurisdiction.
//
// Implements the Model Context Protocol (JSON-RPC 2.0 over HTTP POST /mcp) in
// stateless mode: every request is independent, no session state is kept.
//
// Supported methods:
//   initialize            - return server capabilities
//   notifications/initialized - fire-and-forget from client, returns 204
//   tools/list            - enumerate the 4 legal tools
//   tools/call            - dispatch to a tool handler
//
// Tools exposed:
//   legal_search(query, top_k)  - embed + retrieve, no generation
//   legal_ask(question)         - retrieve + generate (non-streaming)
//   legal_get_source(source_id) - fetch corpus point by UUID
//   legal_get_legislation(section_id) - fetch legislation point by case_id

#include "astraea/coordinator.hpp"
#include "astraea/jurisdiction.hpp"
#include "astraea/pipeline.hpp"
#include "astraea/retriever.hpp"

namespace astraea::detail::nz_tenancy_app {

// Register POST /mcp (and OPTIONS /mcp for CORS) into the running Drogon app.
// pipeline, leg_store, and jurisdiction must outlive the Drogon event loop.
// leg_store may be nullptr (legal_get_legislation will return an error).
// llm_sem may be nullptr (legal_ask will skip semaphore acquisition).
void register_mcp_handler(
    astraea::RAGPipeline&            pipeline,
    astraea::VectorStore*            leg_store,
    const astraea::JurisdictionBase& jurisdiction,
    int                              embed_dims,
    astraea::CoordinatorClient*      llm_sem = nullptr,
    int                              llm_acquire_timeout_s = 60);

} // namespace astraea::detail::nz_tenancy_app
