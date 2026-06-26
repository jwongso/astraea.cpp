#include "wiki_store.hpp"
#include "wiki_ops.hpp"
#include <astraea/anthropic_client.hpp>
#include <drogon/drogon.h>
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// Load KEY=VALUE pairs from a .env file without overriding existing env vars.
// Skips blank lines and # comments. Strips optional surrounding quotes.
// ---------------------------------------------------------------------------
static void load_dotenv(const std::filesystem::path& path = ".env")
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip blank lines and comments
        if (line.empty() || line.front() == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        // Strip optional surrounding quotes (" or ')
        if (value.size() >= 2 &&
            ((value.front() == '"'  && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        // 0 = don't override existing env vars; shell always wins over .env
        setenv(key.c_str(), value.c_str(), 0);
    }
    spdlog::info("[wiki] loaded {}", std::filesystem::absolute(path).string());
}

// ---------------------------------------------------------------------------
// JSON bodies - named namespace required for glaze external linkage
// ---------------------------------------------------------------------------
namespace astraea::wiki::json {

struct IngestReq  { std::string content; std::string source_name; std::string file_path; };
struct QueryReq   { std::string question; };

struct IngestRespJson {
    bool ok{};
    int  pages_created{};
    int  pages_updated{};
    std::vector<std::string> page_names;
    std::string error;
};
struct QueryRespJson {
    bool ok{};
    std::string answer;
    std::vector<std::string> pages_used;
    std::string wiki_update;
};
struct LintIssueJson {
    std::string type;
    std::string page;
    std::string description;
    std::string suggested_fix;
};
struct LintRespJson {
    bool ok{};
    std::vector<LintIssueJson> issues;
};
struct PageListJson  { std::vector<std::string> pages; };
struct RawListJson   { std::vector<std::string> files; };
struct ErrorJson     { bool ok = false; std::string error; };
struct UploadRespJson { bool ok{}; std::string filename; std::string path; };

} // namespace astraea::wiki::json

namespace {

using namespace astraea::wiki::json;
using CB = std::function<void(const drogon::HttpResponsePtr&)>;

drogon::HttpResponsePtr json_resp(const auto& val,
                                  drogon::HttpStatusCode st = drogon::k200OK)
{
    std::string body;
    (void)glz::write_json(val, body);
    auto r = drogon::HttpResponse::newHttpResponse();
    r->setStatusCode(st);
    r->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    r->setBody(std::move(body));
    return r;
}

drogon::HttpResponsePtr error_resp(std::string_view msg,
                                   drogon::HttpStatusCode st = drogon::k400BadRequest)
{
    return json_resp(ErrorJson{false, std::string(msg)}, st);
}

std::string env(const char* key, std::string_view def = "")
{
    const char* v = std::getenv(key);
    return v ? v : std::string(def);
}

// Minimal JSON string escape for SSE event values (error messages etc).
std::string json_esc(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 2);
    o += '"';
    for (char c : s) {
        if      (c == '"')  o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else                o += c;
    }
    o += '"';
    return o;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    spdlog::set_level(spdlog::level::info);
    load_dotenv();

    const std::string api_key  = env("ANTHROPIC_API_KEY");
    if (api_key.empty()) { spdlog::error("ANTHROPIC_API_KEY not set"); return 1; }

    const std::string wiki_token = env("WIKI_TOKEN");
    if (wiki_token.empty()) spdlog::warn("[wiki] WIKI_TOKEN not set - write endpoints are unprotected");
    else spdlog::info("[wiki] write endpoints protected by X-API-Key");

    const std::string model    = env("WIKI_MODEL",  "claude-sonnet-4-6");
    const std::string data_dir = env("WIKI_DIR",    "./wiki_data");
    const int         port     = std::stoi(env("WIKI_PORT", "8090"));
    // PDF ingest can take 2-5 minutes for large documents. 120s is too short.
    const double      llm_timeout = std::stod(env("WIKI_LLM_TIMEOUT", "600.0"));
    const int         max_tokens  = std::stoi(env("WIKI_MAX_TOKENS",  "16384"));

    astraea::AnthropicClient   anthropic(api_key, model, max_tokens, llm_timeout);
    astraea::wiki::WikiStore   store(data_dir);
    store.init();
    astraea::wiki::WikiOps     ops(store, anthropic);

    spdlog::info("[wiki] commit:      {}", WIKI_GIT_HASH);
    spdlog::info("[wiki] data dir:    {}", std::filesystem::absolute(data_dir).string());
    spdlog::info("[wiki] model:       {}", model);
    spdlog::info("[wiki] max_tokens:  {}", max_tokens);
    spdlog::info("[wiki] llm timeout: {}s", llm_timeout);
    spdlog::info("[wiki] port:        {}", port);

    // -- GET /  -> wiki.html
    const std::string static_dir{WIKI_STATIC_DIR};

    // Auth check for write endpoints: returns true if allowed.
    auto auth_ok = [&wiki_token](const drogon::HttpRequestPtr& req) -> bool {
        if (wiki_token.empty()) return true; // no token configured = open
        return req->getHeader("x-api-key") == wiki_token;
    };
    auto unauth = []() {
        return json_resp(ErrorJson{false, "unauthorized"}, drogon::k401Unauthorized);
    };

    // -- GET /wiki  -> serve index.html when accessed via /wiki subpath proxy
    drogon::app().registerHandler("/wiki",
        [&static_dir](const drogon::HttpRequestPtr&, CB&& cb) {
            cb(drogon::HttpResponse::newFileResponse(static_dir + "/index.html"));
        }, {drogon::Get});

    // -- GET /wiki/pages
    drogon::app().registerHandler("/wiki/pages",
        [&store](const drogon::HttpRequestPtr&, CB&& cb) {
            cb(json_resp(PageListJson{store.list_pages()}));
        }, {drogon::Get});

    // -- GET /wiki/pages/:name
    drogon::app().registerHandler("/wiki/pages/{1}",
        [&store](const drogon::HttpRequestPtr&, CB&& cb, const std::string& name) {
            auto text = store.read_page(name);
            if (!text) { cb(error_resp("page not found", drogon::k404NotFound)); return; }
            auto r = drogon::HttpResponse::newHttpResponse();
            r->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            r->setBody(*text);
            cb(r);
        }, {drogon::Get});

    // -- GET /wiki/raw
    drogon::app().registerHandler("/wiki/raw",
        [&store](const drogon::HttpRequestPtr&, CB&& cb) {
            RawListJson resp;
            for (auto& p : store.list_raw())
                resp.files.push_back(p.string());
            cb(json_resp(resp));
        }, {drogon::Get});

    // -- GET /wiki/index
    drogon::app().registerHandler("/wiki/index",
        [&store](const drogon::HttpRequestPtr&, CB&& cb) {
            auto r = drogon::HttpResponse::newHttpResponse();
            r->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            r->setBody(store.index_text());
            cb(r);
        }, {drogon::Get});

    // -- POST /wiki/upload  (multipart/form-data, field name "file")
    drogon::app().registerHandler("/wiki/upload",
        [&store, auth_ok, unauth](const drogon::HttpRequestPtr& req, CB&& cb) {
            if (!auth_ok(req)) { cb(unauth()); return; }
            drogon::MultiPartParser mp;
            if (mp.parse(req) != 0) {
                spdlog::warn("[wiki] upload: multipart parse failed");
                cb(error_resp("multipart parse failed")); return;
            }
            auto& files = mp.getFiles();
            if (files.empty()) {
                spdlog::warn("[wiki] upload: no file in request");
                cb(error_resp("no file uploaded")); return;
            }
            auto& f = files[0];
            spdlog::info("[wiki] upload: name='{}' size={} bytes",
                         f.getFileName(), f.fileLength());
            auto saved = store.save_raw(f.getFileName(), f.fileData(), f.fileLength());
            spdlog::info("[wiki] upload: saved -> {}", saved.string());
            cb(json_resp(UploadRespJson{true, f.getFileName(), saved.string()}));
        }, {drogon::Post});

    // -- POST /wiki/ingest  (SSE: streams status/page/done events to the browser)
    drogon::app().registerHandler("/wiki/ingest",
        [&store, &ops, auth_ok, unauth](const drogon::HttpRequestPtr& req, CB&& cb) {
            if (!auth_ok(req)) { cb(unauth()); return; }
            // Wrap body in shared_ptr so the outer lambda stays copyable
            // (std::function<void(ResponseStreamPtr)> requirement).
            auto body_sptr = std::make_shared<std::string>(req->body());

            auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                [body_sptr, &store, &ops](drogon::ResponseStreamPtr stream) {
                    // Move unique_ptr into shared_ptr so coroutine lambda can capture it.
                    auto sh = std::shared_ptr<drogon::ResponseStream>{std::move(stream)};

                    drogon::async_run(
                        [sh, body_sptr, &store, &ops]() -> drogon::Task<> {

                        auto send = [&sh](std::string_view json) {
                            sh->send(std::string("data: ") + std::string(json) + "\n\n");
                        };

                        IngestReq body{};
                        if (glz::read<glz::opts{.error_on_unknown_keys = false}>(
                                body, *body_sptr)) {
                            spdlog::warn("[wiki] ingest: invalid JSON body");
                            send(R"({"type":"error","error":"invalid JSON body"})");
                            co_return;
                        }

                        std::string content     = body.content;
                        std::string source_name = body.source_name;

                        if (content.empty() && !body.file_path.empty()) {
                            send(R"({"type":"status","msg":"Extracting text from file..."})");
                            const auto t0 = std::chrono::steady_clock::now();
                            spdlog::info("[wiki] ingest: extracting '{}'", body.file_path);
                            auto raw = store.read_raw_as_text(body.file_path);
                            const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - t0).count();
                            if (!raw) {
                                spdlog::error("[wiki] ingest: extraction failed for '{}' after {}ms",
                                              body.file_path, dt);
                                send(R"({"type":"error","error":"file not found or extraction failed"})");
                                co_return;
                            }
                            spdlog::info("[wiki] ingest: extracted {}c in {}ms", raw->size(), dt);
                            content = std::move(*raw);
                            if (source_name.empty())
                                source_name = std::filesystem::path(body.file_path).filename().string();
                        }

                        if (content.empty()) {
                            send(R"({"type":"error","error":"content or file_path required"})");
                            co_return;
                        }
                        if (source_name.empty()) source_name = "unnamed";

                        // Cap content to ~150k chars (~37k tokens) - fits within Claude's context
                        // window alongside system prompt and JSON response.
                        static constexpr std::size_t MAX_CHARS = 150'000;
                        if (content.size() > MAX_CHARS) {
                            spdlog::warn("[wiki] ingest: truncating {}c -> {}c for '{}'",
                                         content.size(), MAX_CHARS, source_name);
                            content.resize(MAX_CHARS);
                            send("{\"type\":\"status\",\"msg\":\"Document truncated to first 150k chars (too large for one pass - consider chunking).\"}");
                        }

                        send(R"({"type":"status","msg":"Claude is compiling sources into wiki pages..."})");
                        spdlog::info("[wiki] ingest: LLM compile source='{}' content={}c",
                                     source_name, content.size());
                        const auto t_llm = std::chrono::steady_clock::now();

                        try {
                            auto r = co_await ops.ingest(content, source_name);
                            const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - t_llm).count();

                            if (!r.error.empty()) {
                                spdlog::error("[wiki] ingest: LLM error after {}ms: {}", dt, r.error);
                                send("{\"type\":\"error\",\"error\":" + json_esc(r.error) + "}");
                            } else {
                                spdlog::info("[wiki] ingest: done in {}ms created={} updated={}",
                                             dt, r.pages_created, r.pages_updated);
                                for (auto& name : r.page_names)
                                    send("{\"type\":\"page\",\"name\":\"" + name + "\"}");

                                std::string done =
                                    "{\"type\":\"done\",\"ok\":true"
                                    ",\"pages_created\":" + std::to_string(r.pages_created) +
                                    ",\"pages_updated\":" + std::to_string(r.pages_updated) +
                                    ",\"page_names\":[";
                                for (std::size_t i = 0; i < r.page_names.size(); i++) {
                                    if (i) done += ",";
                                    done += "\"" + r.page_names[i] + "\"";
                                }
                                done += "]}";
                                send(done);
                            }
                        } catch (const std::exception& e) {
                            const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - t_llm).count();
                            spdlog::error("[wiki] ingest: exception after {}ms: {}", dt, e.what());
                            send("{\"type\":\"error\",\"error\":" + json_esc(e.what()) + "}");
                        }

                        sh->close();
                    });
                });

            resp->setContentTypeCodeAndCustomString(
                drogon::CT_TEXT_PLAIN, "text/event-stream; charset=utf-8");
            resp->addHeader("cache-control", "no-cache");
            resp->addHeader("x-accel-buffering", "no");
            cb(resp);
        }, {drogon::Post});

    // -- POST /wiki/query
    drogon::app().registerHandler("/wiki/query",
        [&ops, auth_ok, unauth](const drogon::HttpRequestPtr& req, CB&& cb) {
            if (!auth_ok(req)) { cb(unauth()); return; }
            drogon::async_run([req, cb = std::move(cb), &ops]() -> drogon::Task<> {
                QueryReq body{};
                if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(
                        body, req->body()); e || body.question.empty()) {
                    cb(error_resp("question required")); co_return;
                }
                try {
                    auto r = co_await ops.query(body.question);
                    cb(json_resp(QueryRespJson{true, r.answer, r.pages_used, r.wiki_update}));
                } catch (const std::exception& e) {
                    cb(error_resp(e.what(), drogon::k500InternalServerError));
                }
            });
        }, {drogon::Post});

    // -- POST /wiki/lint
    drogon::app().registerHandler("/wiki/lint",
        [&ops, auth_ok, unauth](const drogon::HttpRequestPtr& req, CB&& cb) {
            if (!auth_ok(req)) { cb(unauth()); return; }
            drogon::async_run([cb = std::move(cb), &ops]() -> drogon::Task<> {
                try {
                    auto issues = co_await ops.lint();
                    LintRespJson resp;
                    resp.ok = true;
                    for (auto& i : issues)
                        resp.issues.push_back({i.type, i.page, i.description, i.suggested_fix});
                    cb(json_resp(resp));
                } catch (const std::exception& e) {
                    cb(error_resp(e.what(), drogon::k500InternalServerError));
                }
            });
        }, {drogon::Post});

    drogon::app()
        .addListener("127.0.0.1", port)
        .setThreadNum(4)
        .setClientMaxBodySize(100 * 1024 * 1024)  // 100 MB - enough for large PDFs
        .setIdleConnectionTimeout(660)             // > LLM timeout (600s); keeps SSE alive
        .setDocumentRoot(static_dir)
        .run();

    return 0;
}
