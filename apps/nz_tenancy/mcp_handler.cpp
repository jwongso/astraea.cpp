#include "mcp_handler.hpp"

#include "astraea/coordinator.hpp"
#include "astraea/generator.hpp"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>
#include <glaze/glaze.hpp>
#include <trantor/utils/Logger.h>

#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 helpers
// ---------------------------------------------------------------------------

namespace {

// Build a JSON-RPC success response. `result_json` must be valid JSON.
std::string rpc_ok(std::string_view id_json, std::string_view result_json) {
    return std::string{R"({"jsonrpc":"2.0","id":)"} +
           std::string(id_json) + R"(,"result":)" +
           std::string(result_json) + "}";
}

// Build a JSON-RPC error response.
std::string rpc_err(std::string_view id_json, int code, std::string_view msg) {
    return std::string{R"({"jsonrpc":"2.0","id":)"} +
           std::string(id_json) +
           R"(,"error":{"code":)" + std::to_string(code) +
           R"(,"message":")" + std::string(msg) + R"("}})";
}

// JSON-escape a raw string value (adds surrounding quotes).
// glz::write_json replaces its output buffer, so we always use a temp.
std::string json_quote(std::string_view sv) {
    std::string tmp;
    (void)glz::write_json(std::string(sv), tmp);
    return tmp;
}

// Wrap tool output text in the MCP content envelope.
std::string tool_result(std::string_view text, bool is_error = false) {
    std::string out;
    out.reserve(text.size() + 64);
    out += R"({"content":[{"type":"text","text":)";
    out += json_quote(text);
    out += R"(}],"isError":)";   // closing brace for {"type":"text","text":...}
    out += is_error ? "true" : "false";
    out += '}';
    return out;
}

drogon::HttpResponsePtr json_resp(drogon::HttpStatusCode code, std::string body) {
    auto r = drogon::HttpResponse::newHttpResponse();
    r->setStatusCode(code);
    r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    r->setBody(std::move(body));
    return r;
}

// ---------------------------------------------------------------------------
// Tool JSON-Schema definitions (inlined constants - no dynamic allocation)
// ---------------------------------------------------------------------------

constexpr std::string_view kToolsListResult = R"({
"tools":[
  {
    "name":"legal_search",
    "description":"Search NZ Tenancy Tribunal decisions and legislation by semantic similarity. Returns sources with title, court, date, URL, and relevance score. Use this to find raw sources; use legal_ask for a synthesised answer.",
    "inputSchema":{
      "type":"object",
      "properties":{
        "query":{"type":"string","description":"Legal question or topic to search for."},
        "top_k":{"type":"integer","description":"Number of results to return (1-20, default 5).","default":5}
      },
      "required":["query"]
    }
  },
  {
    "name":"legal_ask",
    "description":"Ask a legal question about NZ tenancy law and receive a researched answer with citations. Performs RAG retrieval then generates a complete answer. Use when you need a synthesised legal explanation, not just raw sources.",
    "inputSchema":{
      "type":"object",
      "properties":{
        "question":{"type":"string","description":"The legal question to answer."}
      },
      "required":["question"]
    }
  },
  {
    "name":"legal_get_source",
    "description":"Fetch a Tenancy Tribunal decision or case document by its source_id UUID. Use source_id values returned by legal_search or legal_ask.",
    "inputSchema":{
      "type":"object",
      "properties":{
        "source_id":{"type":"string","description":"UUID of the corpus point to fetch."}
      },
      "required":["source_id"]
    }
  },
  {
    "name":"legal_get_legislation",
    "description":"Fetch the text of an NZ legislation section by its case_id (e.g. NZLEG/RTA/s42A, NZLEG/HHS/r8). Returns the operative section text as stored in Qdrant.",
    "inputSchema":{
      "type":"object",
      "properties":{
        "section_id":{"type":"string","description":"case_id of the legislation section, e.g. NZLEG/RTA/s42A."}
      },
      "required":["section_id"]
    }
  }
]})";

constexpr std::string_view kInitializeResult = R"({
"protocolVersion":"2024-11-05",
"capabilities":{"tools":{}},
"serverInfo":{"name":"astraea-nz-tenancy","version":"1.0.0"}
})";

// ---------------------------------------------------------------------------
// glaze-parsed argument structs
// ---------------------------------------------------------------------------

struct SearchArgs { std::string query; int top_k = 5; };
struct AskArgs    { std::string question; };
struct SourceArgs { std::string source_id; };
struct LegArgs    { std::string section_id; };

} // anonymous namespace

namespace glz {
template<> struct meta<SearchArgs> {
    using T = SearchArgs;
    static constexpr auto value = object("query", &T::query, "top_k", &T::top_k);
};
template<> struct meta<AskArgs> {
    using T = AskArgs;
    static constexpr auto value = object("question", &T::question);
};
template<> struct meta<SourceArgs> {
    using T = SourceArgs;
    static constexpr auto value = object("source_id", &T::source_id);
};
template<> struct meta<LegArgs> {
    using T = LegArgs;
    static constexpr auto value = object("section_id", &T::section_id);
};
} // namespace glz

// ---------------------------------------------------------------------------
// Tool handlers (coroutines - all I/O is non-blocking)
// ---------------------------------------------------------------------------

namespace {

// Serialize a QdrantPoint payload as a compact JSON object.
// glz::write_json replaces its buffer, so use json_quote() for each value.
std::string point_to_json(const astraea::QdrantPoint& p) {
    std::string out;
    out += R"({"score":)";
    out += std::to_string(p.score);
    for (const auto& [k, v] : p.payload) {
        out += ',';
        out += json_quote(k);
        out += ':';
        out += json_quote(v);
    }
    out += '}';
    return out;
}

drogon::Task<std::string> tool_legal_search(
        astraea::RAGPipeline& pipeline, std::string_view args_json) {
    SearchArgs args;
    if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(
            args, args_json); e) {
        co_return tool_result(R"({"error":"invalid arguments"})", true);
    }
    if (args.query.empty()) {
        co_return tool_result(R"({"error":"query is required"})", true);
    }
    args.top_k = std::clamp(args.top_k, 1, 20);

    try {
        auto result = co_await pipeline.retrieve(args.query, args.top_k,
                                                 /*min_score=*/0.70f,
                                                 /*min_chunks=*/1);
        std::string out;
        out += R"({"count":)";
        out += std::to_string(result.sources.size());
        out += R"(,"sources":[)";
        for (std::size_t i = 0; i < result.sources.size(); ++i) {
            if (i > 0) out += ',';
            out += point_to_json(result.sources[i]);
        }
        out += "]}";
        co_return tool_result(out);
    } catch (const std::exception& ex) {
        LOG_ERROR << "mcp legal_search error: " << ex.what();
        co_return tool_result(R"({"error":"retrieval failed"})", true);
    }
}

drogon::Task<std::string> tool_legal_ask(
        astraea::RAGPipeline& pipeline,
        const astraea::JurisdictionBase& jurisdiction,
        std::string_view args_json,
        astraea::CoordinatorClient* llm_sem,
        int llm_acquire_timeout_s) {
    AskArgs args;
    if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(
            args, args_json); e) {
        co_return tool_result(R"({"error":"invalid arguments"})", true);
    }
    if (args.question.empty()) {
        co_return tool_result(R"({"error":"question is required"})", true);
    }

    try {
        auto result = co_await pipeline.retrieve(args.question,
                                                 /*top_k=*/5,
                                                 /*min_score=*/0.70f,
                                                 /*min_chunks=*/1);

        // Build context from retrieved chunks.
        std::string context;
        context.reserve(4096);
        for (std::size_t i = 0; i < result.texts.size(); ++i) {
            context += "\n[S" + std::to_string(i + 1) + "] ";
            context += result.texts[i];
            context += '\n';
        }

        std::vector<astraea::ChatMessage> messages{
            {.role = "system", .content = jurisdiction.system_prompt()},
            {.role = "user",   .content =
                "RETRIEVED SOURCES:\n" + context +
                "\n\nQUESTION: " + args.question}
        };

        // Acquire the global LLM semaphore so legal_ask competes fairly with
        // live /ask/stream traffic instead of bypassing the concurrency limit.
        astraea::CoordinatorClient::Permit gen_permit;
        if (llm_sem) {
            if (llm_acquire_timeout_s > 0) {
                auto maybe = co_await llm_sem->acquire(
                    std::chrono::seconds(llm_acquire_timeout_s));
                if (!maybe) {
                    co_return tool_result(
                        R"({"error":"LLM busy - retry in a moment"})", true);
                }
                gen_permit = std::move(*maybe);
            } else {
                gen_permit = co_await llm_sem->acquire();
            }
        }

        std::string answer = co_await pipeline.generator().generate(messages);

        // Collect source metadata for the caller.
        std::string sources_json;
        sources_json += '[';
        for (std::size_t i = 0; i < result.sources.size(); ++i) {
            if (i > 0) sources_json += ',';
            sources_json += point_to_json(result.sources[i]);
        }
        sources_json += ']';

        std::string out;
        out += R"({"answer":)";
        out += json_quote(answer);
        out += R"(,"sources":)";
        out += sources_json;
        out += '}';
        co_return tool_result(out);
    } catch (const std::exception& ex) {
        LOG_ERROR << "mcp legal_ask error: " << ex.what();
        co_return tool_result(R"({"error":"generation failed"})", true);
    }
}

drogon::Task<std::string> tool_legal_get_source(
        astraea::RAGPipeline& pipeline, std::string_view args_json) {
    SourceArgs args;
    if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(
            args, args_json); e) {
        co_return tool_result(R"({"error":"invalid arguments"})", true);
    }
    if (args.source_id.empty()) {
        co_return tool_result(R"({"error":"source_id is required"})", true);
    }
    try {
        auto pts = co_await pipeline.store().fetch({args.source_id});
        if (pts.empty()) {
            co_return tool_result(R"({"error":"source not found"})", true);
        }
        co_return tool_result(point_to_json(pts.front()));
    } catch (const std::exception& ex) {
        LOG_ERROR << "mcp legal_get_source error: " << ex.what();
        co_return tool_result(R"({"error":"fetch failed"})", true);
    }
}

drogon::Task<std::string> tool_legal_get_legislation(
        astraea::VectorStore* leg_store,
        std::string_view args_json) {
    if (!leg_store) {
        co_return tool_result(
            R"({"error":"legislation collection not configured"})", true);
    }
    LegArgs args;
    if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(
            args, args_json); e) {
        co_return tool_result(R"({"error":"invalid arguments"})", true);
    }
    if (args.section_id.empty()) {
        co_return tool_result(R"({"error":"section_id is required"})", true);
    }
    try {
        // Qdrant fetch() requires UUID point IDs; case_id is a payload field.
        // Use scroll_by_filter (no vector needed) to retrieve all chunks for
        // this section by payload equality.
        astraea::QdrantFilter filt;
        filt.must.push_back({"case_id", {args.section_id}});
        auto pts = co_await leg_store->scroll_by_filter(std::move(filt), /*limit=*/64);
        if (pts.empty()) {
            co_return tool_result(R"({"error":"section not found"})", true);
        }
        // Return the chunk with the lowest chunk_index (operative start).
        const astraea::QdrantPoint* best = &pts.front();
        for (const auto& p : pts) {
            auto it_new = p.payload.find("chunk_index");
            auto it_cur = best->payload.find("chunk_index");
            if (it_new != p.payload.end() && it_cur != best->payload.end()) {
                try {
                    if (std::stoi(it_new->second) < std::stoi(it_cur->second))
                        best = &p;
                } catch (...) {}
            }
        }
        co_return tool_result(point_to_json(*best));
    } catch (const std::exception& ex) {
        LOG_ERROR << "mcp legal_get_legislation error: " << ex.what();
        co_return tool_result(R"({"error":"fetch failed"})", true);
    }
}

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 dispatcher
// ---------------------------------------------------------------------------

// Extract a string field from a flat JSON object without pulling in a full
// JSON parser. Sufficient for the small, trusted RPC envelope fields we need
// (method, id, the arguments blob). Returns empty string_view on miss.
// Handles simple quoted strings and integer ids only.
//
// This is intentionally minimal: we only need "jsonrpc", "method", "id", and
// the raw "params.arguments" blob to dispatch. Full params parsing is done per
// tool via glaze.
std::string_view json_get_str(std::string_view json, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 3);
    needle += '"';
    needle += key;
    needle += "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string_view::npos) return {};
    return json.substr(pos, end - pos);
}

// Extract the raw (un-quoted) value for a key: handles strings, numbers,
// objects, arrays. Returns the raw JSON token/value (no unquoting).
std::string_view json_get_raw(std::string_view json, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 3);
    needle += '"';
    needle += key;
    needle += "\":";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r'))
        ++pos;
    if (pos >= json.size()) return {};
    char open = json[pos];
    if (open == '{' || open == '[') {
        // Walk matching brackets.
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        std::size_t i = pos + 1;
        bool in_str = false;
        while (i < json.size() && depth > 0) {
            char c = json[i];
            if (c == '"' && (i == 0 || json[i-1] != '\\')) in_str = !in_str;
            if (!in_str) {
                if (c == open) ++depth;
                else if (c == close) --depth;
            }
            ++i;
        }
        return json.substr(pos, i - pos);
    }
    if (open == '"') {
        // Quoted string - find closing quote respecting escapes.
        std::size_t i = pos + 1;
        while (i < json.size()) {
            if (json[i] == '\\') { i += 2; continue; }
            if (json[i] == '"') { ++i; break; }
            ++i;
        }
        return json.substr(pos, i - pos);
    }
    // Number, bool, null - find end at delimiter.
    auto end = json.find_first_of(",}\n]", pos);
    return json.substr(pos, (end == std::string_view::npos ? json.size() : end) - pos);
}

// Extract the "id" field (may be integer or quoted string) as raw JSON.
std::string_view json_get_id(std::string_view json) {
    return json_get_raw(json, "id");
}

drogon::Task<drogon::HttpResponsePtr> dispatch_rpc(
        std::string body,
        astraea::RAGPipeline& pipeline,
        astraea::VectorStore* leg_store,
        const astraea::JurisdictionBase& jurisdiction,
        astraea::CoordinatorClient* llm_sem,
        int llm_acquire_timeout_s) {

    const std::string_view sv{body};
    const std::string_view method = json_get_str(sv, "method");
    const std::string_view id_raw = json_get_id(sv);
    // Use "null" for missing/unparseable id (JSON-RPC 2.0 spec).
    const std::string id_json = id_raw.empty() ? "null" : std::string(id_raw);

    // Notification: no response body, 204 No Content.
    if (method == "notifications/initialized") {
        auto r = drogon::HttpResponse::newHttpResponse();
        r->setStatusCode(drogon::k204NoContent);
        co_return r;
    }

    if (method == "initialize") {
        co_return json_resp(drogon::k200OK,
            rpc_ok(id_json, kInitializeResult));
    }

    if (method == "tools/list") {
        co_return json_resp(drogon::k200OK,
            rpc_ok(id_json, kToolsListResult));
    }

    if (method == "tools/call") {
        // params.name and params.arguments
        const std::string_view params_raw = json_get_raw(sv, "params");
        if (params_raw.empty()) {
            co_return json_resp(drogon::k400BadRequest,
                rpc_err(id_json, -32602, "missing params"));
        }
        const std::string_view tool_name  = json_get_str(params_raw, "name");
        const std::string_view args_raw   = json_get_raw(params_raw, "arguments");
        const std::string args_json = args_raw.empty() ? "{}" : std::string(args_raw);

        std::string result;
        if (tool_name == "legal_search") {
            result = co_await tool_legal_search(pipeline, args_json);
        } else if (tool_name == "legal_ask") {
            result = co_await tool_legal_ask(pipeline, jurisdiction, args_json,
                                             llm_sem, llm_acquire_timeout_s);
        } else if (tool_name == "legal_get_source") {
            result = co_await tool_legal_get_source(pipeline, args_json);
        } else if (tool_name == "legal_get_legislation") {
            result = co_await tool_legal_get_legislation(leg_store, args_json);
        } else {
            co_return json_resp(drogon::k404NotFound,
                rpc_err(id_json, -32601, "unknown tool"));
        }
        co_return json_resp(drogon::k200OK, rpc_ok(id_json, result));
    }

    co_return json_resp(drogon::k400BadRequest,
        rpc_err(id_json, -32601, "method not found"));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public registration function
// ---------------------------------------------------------------------------

namespace astraea::detail::nz_tenancy_app {

void register_mcp_handler(
        astraea::RAGPipeline& pipeline,
        astraea::VectorStore* leg_store,
        const astraea::JurisdictionBase& jurisdiction,
        int /*embed_dims - reserved, unused*/,
        astraea::CoordinatorClient* llm_sem,
        int llm_acquire_timeout_s) {

    // POST /mcp - JSON-RPC 2.0 dispatch.
    drogon::app().registerHandler("/mcp",
        [&pipeline, leg_store, &jurisdiction, llm_sem, llm_acquire_timeout_s](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            drogon::async_run(
                [body = std::string(req->getBody()),
                 &pipeline, leg_store, &jurisdiction,
                 llm_sem, llm_acquire_timeout_s,
                 cb = std::move(cb)]() -> drogon::Task<> {
                    auto resp = co_await dispatch_rpc(
                        std::move(body), pipeline, leg_store, jurisdiction,
                        llm_sem, llm_acquire_timeout_s);
                    cb(resp);
                });
        }, {drogon::Post});

    // OPTIONS /mcp - CORS preflight.
    drogon::app().registerHandler("/mcp",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(drogon::HttpResponse::newHttpResponse());
        }, {drogon::Options});

    LOG_INFO << "MCP handler registered at POST /mcp (stateless HTTP)";
}

} // namespace astraea::detail::nz_tenancy_app
