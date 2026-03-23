#include "insightos/backend/uri.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>  // gethostname
#endif

namespace insightos::backend {
namespace {

// Split `sv` by `delim`, returning all segments (including empty ones).
std::vector<std::string_view> split(std::string_view sv, char delim) {
    std::vector<std::string_view> parts;
    while (true) {
        auto pos = sv.find(delim);
        parts.push_back(sv.substr(0, pos));
        if (pos == std::string_view::npos) break;
        sv.remove_prefix(pos + 1);
    }
    return parts;
}

// Parse "key=val&key2=val2" into a map.
std::map<std::string, std::string> parse_query(std::string_view qs) {
    std::map<std::string, std::string> params;
    if (qs.empty()) return params;
    for (auto& kv : split(qs, '&')) {
        if (kv.empty()) continue;
        auto eq = kv.find('=');
        if (eq == std::string_view::npos) {
            params.emplace(std::string(kv), std::string{});
        } else {
            params.emplace(std::string(kv.substr(0, eq)),
                           std::string(kv.substr(eq + 1)));
        }
    }
    return params;
}

// Separate path from query: returns {path, query} where query may be empty.
std::pair<std::string_view, std::string_view> split_query(std::string_view s) {
    auto qpos = s.find('?');
    if (qpos == std::string_view::npos) return {s, {}};
    return {s.substr(0, qpos), s.substr(qpos + 1)};
}

// Strip host:port → host only.
std::string_view strip_port(std::string_view host_port) {
    // Handle IPv6 bracket notation [::1]:8080
    if (!host_port.empty() && host_port.front() == '[') {
        auto bracket = host_port.find(']');
        if (bracket != std::string_view::npos) {
            return host_port.substr(1, bracket - 1);
        }
        return host_port;
    }
    auto colon = host_port.find(':');
    if (colon == std::string_view::npos) return host_port;
    return host_port.substr(0, colon);
}

}  // namespace

// ─── InsightOsUri::to_string ────────────────────────────────────────────

std::string InsightOsUri::to_string() const {
    std::string out = "insightos://";
    out += host;
    out += '/';
    out += device;
    out += '/';
    out += preset;
    if (!delivery.empty()) {
        out += '/';
        out += delivery;
    }
    if (!query_params.empty()) {
        out += '?';
        bool first = true;
        for (auto& [k, v] : query_params) {
            if (!first) out += '&';
            first = false;
            out += k;
            if (!v.empty()) {
                out += '=';
                out += v;
            }
        }
    }
    return out;
}

// ─── InsightOsUri::is_local ─────────────────────────────────────────────

bool InsightOsUri::is_local() const {
    if (host == "localhost" || host == "127.0.0.1" || host == "::1")
        return true;

    std::array<char, 256> buf{};
    if (::gethostname(buf.data(), buf.size()) == 0) {
        return host == buf.data();
    }
    return false;
}

// ─── parse_insightos_uri ────────────────────────────────────────────────

std::optional<InsightOsUri> parse_insightos_uri(std::string_view uri) {
    constexpr std::string_view scheme = "insightos://";
    if (uri.size() < scheme.size()) return std::nullopt;
    if (uri.substr(0, scheme.size()) != scheme) return std::nullopt;

    auto rest = uri.substr(scheme.size());
    if (rest.empty()) return std::nullopt;

    // Extract host (up to first '/')
    auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0) return std::nullopt;

    InsightOsUri result;
    result.host = std::string(rest.substr(0, slash));
    rest.remove_prefix(slash + 1);

    // Separate path from query string
    auto [path, query] = split_query(rest);
    if (path.empty()) return std::nullopt;

    // Split path into segments; expect 2 (device/preset) or 3 (+delivery)
    auto segments = split(path, '/');

    // Filter out trailing empty segment from "preset/" case
    while (!segments.empty() && segments.back().empty()) segments.pop_back();

    if (segments.size() < 2 || segments.size() > 3) return std::nullopt;

    // Reject empty segments
    for (auto& seg : segments) {
        if (seg.empty()) return std::nullopt;
    }

    result.device = std::string(segments[0]);
    result.preset = std::string(segments[1]);
    if (segments.size() == 3) {
        result.delivery = std::string(segments[2]);
    }

    result.query_params = parse_query(query);
    return result;
}

// ─── parse_http_mirror ──────────────────────────────────────────────────

std::optional<InsightOsUri> parse_http_mirror(std::string_view url) {
    std::string_view rest;

    constexpr std::string_view http = "http://";
    constexpr std::string_view https = "https://";

    if (url.size() >= https.size() && url.substr(0, https.size()) == https) {
        rest = url.substr(https.size());
    } else if (url.size() >= http.size() && url.substr(0, http.size()) == http) {
        rest = url.substr(http.size());
    } else {
        return std::nullopt;
    }

    if (rest.empty()) return std::nullopt;

    // Extract host[:port]
    auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0) return std::nullopt;

    auto host_port = rest.substr(0, slash);
    rest.remove_prefix(slash + 1);

    // Separate path from query
    auto [path, query] = split_query(rest);
    if (path.empty()) return std::nullopt;

    auto segments = split(path, '/');
    while (!segments.empty() && segments.back().empty()) segments.pop_back();

    if (segments.size() < 2 || segments.size() > 3) return std::nullopt;
    for (auto& seg : segments) {
        if (seg.empty()) return std::nullopt;
    }

    InsightOsUri result;
    result.host = std::string(strip_port(host_port));
    result.device = std::string(segments[0]);
    result.preset = std::string(segments[1]);
    if (segments.size() == 3) {
        result.delivery = std::string(segments[2]);
    }
    result.query_params = parse_query(query);
    return result;
}

// ─── http_to_insightos ──────────────────────────────────────────────────

std::optional<std::string> http_to_insightos(std::string_view http_url) {
    auto parsed = parse_http_mirror(http_url);
    if (!parsed) return std::nullopt;
    return parsed->to_string();
}

}  // namespace insightos::backend
