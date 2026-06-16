#pragma once
// GeocodeClient: async HTTP client for the geocode RPC sidecar.
//
// POST /resolve {"address": "..."} -> {found, lat, lng, zone_code, zone_name, council}
//
// Returns nullopt when the address is not found or the sidecar is unreachable
// (fail-open: zone context is optional, /ask still works without it).
#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>

namespace astraea {

struct ZoneInfo {
    std::string zone_code;
    std::string zone_name;
    std::string council;
    double lat = 0.0;
    double lng = 0.0;
};

} // namespace astraea

template <>
struct glz::meta<astraea::ZoneInfo> {
    using T = astraea::ZoneInfo;
    static constexpr auto value = object(
        "zone_code", &T::zone_code,
        "zone_name", &T::zone_name,
        "council",   &T::council,
        "lat",       &T::lat,
        "lng",       &T::lng
    );
};

namespace astraea {

struct GeocodeReq {
    std::string address;
};

struct GeocodeResp {
    bool        found = false;
    double      lat   = 0.0;
    double      lng   = 0.0;
    std::string zone_code;
    std::string zone_name;
    std::string council;
};

} // namespace astraea

template <>
struct glz::meta<astraea::GeocodeReq> {
    using T = astraea::GeocodeReq;
    static constexpr auto value = object("address", &T::address);
};

template <>
struct glz::meta<astraea::GeocodeResp> {
    using T = astraea::GeocodeResp;
    static constexpr auto value = object(
        "found",     &T::found,
        "lat",       &T::lat,
        "lng",       &T::lng,
        "zone_code", &T::zone_code,
        "zone_name", &T::zone_name,
        "council",   &T::council
    );
};

namespace astraea {

class GeocodeClient {
public:
    explicit GeocodeClient(std::string url)
        : _client(drogon::HttpClient::newHttpClient(url))
    {}

    drogon::Task<std::optional<ZoneInfo>> resolve(std::string address) const {
        GeocodeReq body_obj{std::move(address)};
        std::string body;
        if (auto e = glz::write_json(body_obj, body); e) co_return std::nullopt;

        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->setPath("/resolve");
        req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        req->setBody(body);

        drogon::HttpResponsePtr resp;
        try {
            resp = co_await _client->sendRequestCoro(req, 10.0);
        } catch (...) {
            co_return std::nullopt;
        }
        if (!resp || static_cast<int>(resp->statusCode()) != 200)
            co_return std::nullopt;

        GeocodeResp parsed{};
        if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(
                parsed, resp->body()); e) {
            co_return std::nullopt;
        }
        if (!parsed.found) co_return std::nullopt;

        co_return ZoneInfo{
            .zone_code = std::move(parsed.zone_code),
            .zone_name = std::move(parsed.zone_name),
            .council   = std::move(parsed.council),
            .lat       = parsed.lat,
            .lng       = parsed.lng,
        };
    }

private:
    drogon::HttpClientPtr _client;
};

} // namespace astraea
