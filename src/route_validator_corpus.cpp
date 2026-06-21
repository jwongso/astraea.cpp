#include "astraea/route_validator.hpp"
#include "astraea/retriever.hpp"
#include <spdlog/spdlog.h>
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <trantor/net/EventLoopThread.h>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <unordered_set>

namespace astraea {
namespace {

// Hand-parse the tiny Qdrant /points/count response:
//   {"result":{"count":<n>},"status":"ok","time":<float>}
// We only need the count. Returns -1 on parse failure so the caller can
// distinguish missing-section (count==0) from inconclusive (count<0).
[[nodiscard]] long parse_count(std::string_view body) noexcept {
    auto pos = body.find("\"count\"");
    if (pos == std::string_view::npos) return -1;
    pos = body.find(':', pos);
    if (pos == std::string_view::npos) return -1;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos])))
        ++pos;
    long n = 0;
    bool any = false;
    while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
        n = n * 10 + (body[pos] - '0');
        ++pos;
        any = true;
    }
    return any ? n : -1;
}

} // anonymous namespace

RouteValidationReport validate_forced_sections_against_corpus(
    const JurisdictionBase& jurisdiction,
    VectorStore& leg_store,
    double timeout_s)
{
    std::unordered_set<std::string> distinct;
    for (const auto& r : jurisdiction.routes())
        for (const auto& sid : r.forced_sections) distinct.insert(sid);
    for (const auto& [sid, _terms] : jurisdiction.low_priority_sections())
        distinct.insert(sid);

    const std::vector<std::string> section_ids(distinct.begin(), distinct.end());
    const std::string qdrant_url = leg_store.url();
    const std::string collection = leg_store.collection();

    // We're being called from main() BEFORE drogon::app().run(), so the
    // app-global loop is not yet running. drogon::HttpClient + callback API
    // bound to our own EventLoopThread sidesteps that — we own the loop,
    // schedule N callbacks, count down on each completion, and wake the
    // main thread when all probes return (or the wall-clock ceiling fires).
    std::unordered_set<std::string> missing_set;
    if (!section_ids.empty()) {
        trantor::EventLoopThread elt;
        elt.run();
        auto* loop = elt.getLoop();

        std::mutex mu;
        std::condition_variable cv;
        std::size_t outstanding = section_ids.size();

        drogon::HttpClientPtr client;
        loop->runInLoop([&]() {
            client = drogon::HttpClient::newHttpClient(qdrant_url, loop);
        });
        // Wait until client is constructed on the loop thread before issuing.
        loop->runInLoop([](){});

        const std::string path = "/collections/" + collection + "/points/count";

        for (const auto& sid : section_ids) {
            std::string esc;
            esc.reserve(sid.size() + 8);
            for (char c : sid) {
                if (c == '"' || c == '\\') esc.push_back('\\');
                esc.push_back(c);
            }
            const std::string body =
                std::string("{\"filter\":{\"must\":[{\"key\":\"case_id\",")
              + "\"match\":{\"value\":\"" + esc + "\"}}]},\"exact\":true}";

            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(drogon::Post);
            req->setPath(path);
            req->setBody(body);
            req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

            const std::string sid_copy = sid; // copy into the callback
            loop->queueInLoop([&, req, sid_copy]() {
                client->sendRequest(req,
                    [&, sid_copy](drogon::ReqResult result,
                                  const drogon::HttpResponsePtr& resp) {
                        bool absent = false;
                        if (result != drogon::ReqResult::Ok || !resp) {
                            SPDLOG_WARN("route validator: count probe network error "
                                        "for {} — inconclusive", sid_copy);
                        } else if (static_cast<int>(resp->statusCode()) != 200) {
                            SPDLOG_WARN("route validator: count probe HTTP {} for {} "
                                        "— inconclusive",
                                        static_cast<int>(resp->statusCode()),
                                        sid_copy);
                        } else {
                            const long n = parse_count(resp->body());
                            if (n < 0) {
                                SPDLOG_WARN("route validator: parse failure for {} "
                                            "— inconclusive", sid_copy);
                            } else if (n == 0) {
                                absent = true;
                            }
                        }
                        {
                            std::lock_guard<std::mutex> g(mu);
                            if (absent) missing_set.insert(sid_copy);
                            --outstanding;
                            if (outstanding == 0) cv.notify_all();
                        }
                    },
                    timeout_s);
            });
        }

        // Wall-clock ceiling: N probes are dispatched concurrently against
        // one HttpClient → bounded by the slowest probe and connection setup.
        const auto wait_ms = std::max<long>(
            2000, static_cast<long>(timeout_s * 1000) + 5000);
        {
            std::unique_lock<std::mutex> lk(mu);
            const bool done = cv.wait_for(lk, std::chrono::milliseconds(wait_ms),
                                          [&] { return outstanding == 0; });
            if (!done) {
                SPDLOG_WARN("route validator: probe timed out after {}ms with {} "
                            "probe(s) outstanding — those are treated as inconclusive",
                            wait_ms, outstanding);
            }
        }

        loop->quit();
        // EventLoopThread destructor joins the worker thread.
    }

    return validate_forced_sections(
        jurisdiction,
        [&missing_set](std::string_view sid) {
            return missing_set.find(std::string{sid}) == missing_set.end();
        });
}

} // namespace astraea
