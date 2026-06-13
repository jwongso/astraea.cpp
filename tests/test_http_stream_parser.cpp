#include "astraea/detail/http_stream_parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using astraea::detail::HttpStreamParser;
using astraea::detail::SseLineSplitter;

namespace {

struct BodyCollector {
    std::string out;
    std::vector<std::string> chunks; // one entry per sink call
    void operator()(std::string_view sv) {
        out.append(sv.data(), sv.size());
        chunks.emplace_back(sv);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// HttpStreamParser
// ---------------------------------------------------------------------------

TEST_CASE("HttpStreamParser: simple Content-Length response, single feed",
          "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;

    const std::string wire =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world";

    REQUIRE(p.feed(wire, std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Done);
    REQUIRE(p.status_code() == 200);
    REQUIRE(body.out == "hello world");
}

TEST_CASE("HttpStreamParser: Content-Length split across many feeds",
          "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;

    const std::string wire =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "abcde";

    // Feed one byte at a time - exercises the pending-line accumulator.
    for (char c : wire) {
        REQUIRE(p.feed(std::string_view(&c, 1), std::ref(body)));
    }
    REQUIRE(p.state() == HttpStreamParser::State::Done);
    REQUIRE(p.status_code() == 200);
    REQUIRE(body.out == "abcde");
}

TEST_CASE("HttpStreamParser: chunked transfer-encoding, multiple chunks",
          "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;

    const std::string wire =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n"
        "\r\n";

    REQUIRE(p.feed(wire, std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Done);
    REQUIRE(body.out == "hello world");
}

TEST_CASE("HttpStreamParser: chunked with chunk-extensions and uppercase hex",
          "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;

    const std::string wire =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "A;name=value\r\n0123456789\r\n"
        "0\r\n\r\n";

    REQUIRE(p.feed(wire, std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Done);
    REQUIRE(body.out == "0123456789");
}

TEST_CASE("HttpStreamParser: chunked split mid-chunk-data delivers incrementally",
          "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;

    const std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";
    REQUIRE(p.feed(headers, std::ref(body)));
    REQUIRE(body.chunks.empty());

    // First chunk header.
    REQUIRE(p.feed("B\r\n", std::ref(body)));
    REQUIRE(body.chunks.empty());

    // First half of chunk data.
    REQUIRE(p.feed("hello ", std::ref(body)));
    REQUIRE(body.chunks.size() == 1);
    REQUIRE(body.chunks[0] == "hello ");

    // Second half + chunk tail + terminator.
    REQUIRE(p.feed("world\r\n0\r\n\r\n", std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Done);
    REQUIRE(body.out == "hello world");
}

TEST_CASE("HttpStreamParser: lone-LF line terminators accepted",
          "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;

    const std::string wire =
        "HTTP/1.1 200 OK\n"
        "Content-Length: 3\n"
        "\n"
        "abc";

    REQUIRE(p.feed(wire, std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Done);
    REQUIRE(body.out == "abc");
}

TEST_CASE("HttpStreamParser: malformed status line -> Error", "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;
    REQUIRE_FALSE(p.feed("garbage no http prefix\r\n", std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Error);
}

TEST_CASE("HttpStreamParser: unsupported Transfer-Encoding -> Error",
          "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;
    const std::string wire =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: gzip\r\n"
        "\r\n";
    REQUIRE_FALSE(p.feed(wire, std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Error);
}

TEST_CASE("HttpStreamParser: 4xx status preserved", "[http_stream_parser]") {
    HttpStreamParser p;
    BodyCollector body;
    const std::string wire =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    REQUIRE(p.feed(wire, std::ref(body)));
    REQUIRE(p.state() == HttpStreamParser::State::Done);
    REQUIRE(p.status_code() == 503);
    REQUIRE(body.out.empty());
}

// ---------------------------------------------------------------------------
// SseLineSplitter
// ---------------------------------------------------------------------------

TEST_CASE("SseLineSplitter: single complete event", "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    sp.feed("data: hello\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0] == "hello");
}

TEST_CASE("SseLineSplitter: data colon with no space", "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    sp.feed("data:no-space\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0] == "no-space");
}

TEST_CASE("SseLineSplitter: multiple data lines joined with \\n",
          "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    sp.feed("data: line1\ndata: line2\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0] == "line1\nline2");
}

TEST_CASE("SseLineSplitter: events fed byte-by-byte", "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    const std::string wire = "data: a\n\ndata: b\n\n";
    for (char c : wire) sp.feed(std::string_view(&c, 1));
    REQUIRE(events.size() == 2);
    REQUIRE(events[0] == "a");
    REQUIRE(events[1] == "b");
}

TEST_CASE("SseLineSplitter: CRLF terminators (real-world server output)",
          "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    sp.feed("data: x\r\n\r\ndata: y\r\n\r\n");
    REQUIRE(events.size() == 2);
    REQUIRE(events[0] == "x");
    REQUIRE(events[1] == "y");
}

TEST_CASE("SseLineSplitter: comments ignored, non-data fields ignored",
          "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    sp.feed(": this is a comment\nevent: token\ndata: payload\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0] == "payload");
}

TEST_CASE("SseLineSplitter: event with no data field emits nothing",
          "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    sp.feed("event: ping\n\n");
    REQUIRE(events.empty());
}

TEST_CASE("SseLineSplitter: partial event held until terminator",
          "[sse_splitter]") {
    std::vector<std::string> events;
    SseLineSplitter sp([&](std::string_view v) { events.emplace_back(v); });
    sp.feed("data: only partial");
    REQUIRE(events.empty());
    sp.feed("\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0] == "only partial");
}
