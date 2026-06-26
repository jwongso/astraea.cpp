#pragma once
#include "wiki_store.hpp"
#include <astraea/anthropic_client.hpp>
#include <drogon/utils/coroutine.h>
#include <string>
#include <string_view>
#include <vector>

namespace astraea::wiki {

struct IngestResult {
    int                      pages_created = 0;
    int                      pages_updated = 0;
    std::vector<std::string> page_names;
    std::string              error;  // non-empty on failure
};

struct QueryResult {
    std::string              answer;
    std::vector<std::string> pages_used;
    std::string              wiki_update;  // LLM-suggested write-back (may be empty)
};

struct LintIssue {
    std::string type;           // orphan|contradiction|broken_link|suggestion
    std::string page;
    std::string description;
    std::string suggested_fix;  // may be empty
};

/// High-level wiki operations: ingest, query, lint.
/// Each operation builds a prompt, calls Anthropic, parses the response,
/// and writes any changes back to WikiStore.
class WikiOps {
public:
    WikiOps(WikiStore& store, astraea::AnthropicClient& client);

    drogon::Task<IngestResult>          ingest(std::string_view content,
                                               std::string_view source_name);
    drogon::Task<QueryResult>           query(std::string_view question);
    drogon::Task<std::vector<LintIssue>> lint();

private:
    WikiStore&               _store;
    astraea::AnthropicClient& _client;

    // Strip ```json ... ``` fences that Claude sometimes wraps around JSON output.
    static std::string strip_json_fence(const std::string& s);

    IngestResult            parse_ingest(const std::string& raw,
                                         const std::string& source_name);
    QueryResult             parse_query(const std::string& raw);
    std::vector<LintIssue>  parse_lint(const std::string& raw);
};

} // namespace astraea::wiki
