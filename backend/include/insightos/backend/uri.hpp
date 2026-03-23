#pragma once

/// InsightOS backend — URI parsing.
///
/// New canonical URI (see docs/plan/standalone-project-plan.md):
///   insightos://<host>/<device>/<preset>[/<delivery>][?overrides]
///
/// HTTP mirror (frontend-facing, same path layout):
///   http://<host>[:<port>]/<device>/<preset>[/<delivery>][?overrides]
///
/// This replaces the donor's 8-segment device-centric URI
/// (iocontroller/insightos_uri.hpp, commit 4032eb4) with a simpler
/// 3–4 segment device-centric scheme.

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace insightos::backend {

struct InsightOsUri {
    std::string host;
    std::string device;
    std::string preset;
    std::string delivery;  // empty = use preset default
    std::map<std::string, std::string> query_params;  // e.g. d2c=off

    /// Reconstruct canonical URI: insightos://host/device/preset[/delivery][?k=v&...]
    std::string to_string() const;

    /// Returns true for "localhost", "127.0.0.1", "::1", or gethostname().
    bool is_local() const;
};

/// Parse insightos://host/device/preset[/delivery][?params].
/// Returns nullopt for malformed URIs or wrong segment count.
std::optional<InsightOsUri> parse_insightos_uri(std::string_view uri);

/// Parse HTTP mirror: http[s]://host[:port]/device/preset[/delivery][?params].
/// Port is stripped from the resulting InsightOsUri.host.
/// Returns nullopt if the path doesn't match mirror format.
std::optional<InsightOsUri> parse_http_mirror(std::string_view url);

/// Convert HTTP mirror URL to insightos:// URI string (scheme swap only).
/// Preserves host (without port), path, and query string.
std::optional<std::string> http_to_insightos(std::string_view http_url);

}  // namespace insightos::backend
