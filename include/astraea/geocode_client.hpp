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

/// @brief District plan zone information resolved from a NZ property address.
///
/// Returned by GeocodeClient::resolve() when the geocode sidecar finds a match.
/// Used by JurisdictionBase::preprocess_question() to prepend zone context to
/// the question before retrieval.
struct ZoneInfo {
    std::string zone_code; ///< District plan zone identifier, e.g. "MHB" (Mixed Housing Broad).
    std::string zone_name; ///< Human-readable zone name, e.g. "Mixed Housing Broad".
    std::string council; ///< Territorial authority name, e.g. "Auckland Council".
    double lat = 0.0; ///< Latitude of the resolved address centroid.
    double lng = 0.0; ///< Longitude of the resolved address centroid.
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

/// @brief Request body for the geocode sidecar POST /resolve endpoint.
struct GeocodeReq {
    std::string address; ///< Free-text NZ property address to resolve.
};

/// @brief Raw response body from the geocode sidecar POST /resolve endpoint.
struct GeocodeResp {
    bool        found = false; ///< True when the address was successfully resolved to a zone.
    double      lat   = 0.0; ///< Latitude of the resolved centroid; 0.0 when not found.
    double      lng   = 0.0; ///< Longitude of the resolved centroid; 0.0 when not found.
    std::string zone_code; ///< District plan zone code; empty when not found.
    std::string zone_name; ///< Human-readable zone name; empty when not found.
    std::string council; ///< Territorial authority name; empty when not found.
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

/// @brief Async HTTP client for the geocode RPC sidecar.
///
/// Wraps a single POST /resolve call. Returns nullopt on any error (sidecar
/// unreachable, address not found, JSON parse failure) so the calling pipeline
/// can continue without zone context rather than failing the request.
class GeocodeClient {
public:
    explicit GeocodeClient(std::string url)
        : _client(drogon::HttpClient::newHttpClient(url))
    {}

    /// @brief Resolve a free-text NZ address to district plan zone information.
    /// @return ZoneInfo when the sidecar resolves the address; nullopt otherwise (fail-open).
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
