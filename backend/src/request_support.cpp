#include "insightos/backend/request_support.hpp"

#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace insightos::backend {
namespace {

std::optional<std::uint32_t> json_u32(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        const auto parsed = value.get<std::uint64_t>();
        if (parsed <= std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(parsed);
        }
        return std::nullopt;
    }
    if (value.is_number_integer()) {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value >= 0 &&
            static_cast<std::uint64_t>(signed_value) <=
                std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(signed_value);
        }
    }
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (text.empty()) return std::nullopt;

        std::uint32_t parsed = 0;
        const auto* begin = text.data();
        const auto* end = begin + text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, parsed);
        if (ec == std::errc{} && ptr == end) {
            return parsed;
        }
    }
    return std::nullopt;
}

std::optional<bool> json_bool(const nlohmann::json& value) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (text == "true" || text == "1") return true;
        if (text == "false" || text == "0") return false;
    }
    return std::nullopt;
}

std::optional<std::string> json_string(const nlohmann::json& value) {
    if (!value.is_string()) return std::nullopt;
    return value.get<std::string>();
}

std::optional<std::uint32_t> parse_u32_string(std::string_view value) {
    if (value.empty()) return std::nullopt;

    std::uint32_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> parse_bool_string(std::string_view value) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return std::nullopt;
}

nlohmann::json request_json_from_uri(const InsightOsUri& uri) {
    nlohmann::json body;
    body["name"] = uri.device;
    body["preset"] = uri.preset;
    if (!uri.delivery.empty()) {
        body["delivery"] = uri.delivery;
    }

    if (!uri.query_params.empty()) {
        nlohmann::json overrides = nlohmann::json::object();
        for (const auto& [key, value] : uri.query_params) {
            if (key == "audio_rate" || key == "channels") {
                if (auto parsed = parse_u32_string(value)) {
                    overrides[key] = *parsed;
                } else {
                    overrides[key] = value;
                }
            } else if (key == "must_match") {
                if (auto parsed = parse_bool_string(value)) {
                    overrides[key] = *parsed;
                } else {
                    overrides[key] = value;
                }
            } else {
                overrides[key] = value;
            }
        }
        body["overrides"] = std::move(overrides);
    }

    return body;
}

}  // namespace

Result<SessionRequest> session_request_from_json(const nlohmann::json& body,
                                                 RequestOrigin default_origin) {
    SessionRequest request;
    request.origin = default_origin;
    if (body.contains("name")) {
        auto parsed = json_string(body["name"]);
        if (!parsed) {
            return Result<SessionRequest>::err(
                {"bad_request", "'name' must be a string"});
        }
        request.selector.name = std::move(*parsed);
    }
    if (body.contains("device_uuid")) {
        auto parsed = json_string(body["device_uuid"]);
        if (!parsed) {
            return Result<SessionRequest>::err(
                {"bad_request", "'device_uuid' must be a string"});
        }
        request.selector.device_uuid = std::move(*parsed);
    }
    if (body.contains("preset")) {
        auto parsed = json_string(body["preset"]);
        if (!parsed) {
            return Result<SessionRequest>::err(
                {"bad_request", "'preset' must be a string"});
        }
        request.preset_name = std::move(*parsed);
    }
    if (body.contains("delivery")) {
        auto parsed = json_string(body["delivery"]);
        if (!parsed) {
            return Result<SessionRequest>::err(
                {"bad_request", "'delivery' must be a string"});
        }
        request.delivery_name = std::move(*parsed);
    }

    if (request.preset_name.empty() ||
        (request.selector.name.empty() &&
         request.selector.device_uuid.empty())) {
        return Result<SessionRequest>::err(
            {"bad_request",
             "'preset' and either 'name' or 'device_uuid' are required"});
    }

    if (body.contains("overrides") && !body["overrides"].is_object()) {
        return Result<SessionRequest>::err(
            {"bad_request", "'overrides' must be an object"});
    }

    if (body.contains("overrides") && body["overrides"].is_object()) {
        const auto& overrides = body["overrides"];
        if (overrides.contains("must_match")) {
            auto parsed = json_bool(overrides["must_match"]);
            if (!parsed) {
                return Result<SessionRequest>::err(
                    {"bad_request", "overrides.must_match must be boolean"});
            }
            request.overrides.must_match = *parsed;
        }
        if (overrides.contains("audio_rate")) {
            auto parsed = json_u32(overrides["audio_rate"]);
            if (!parsed) {
                return Result<SessionRequest>::err(
                    {"bad_request", "overrides.audio_rate must be an integer"});
            }
            request.overrides.audio_rate = *parsed;
        }
        if (overrides.contains("channels")) {
            auto parsed = json_u32(overrides["channels"]);
            if (!parsed) {
                return Result<SessionRequest>::err(
                    {"bad_request", "overrides.channels must be an integer"});
            }
            request.overrides.channels = *parsed;
        }
        if (overrides.contains("depth_alignment")) {
            auto parsed = json_string(overrides["depth_alignment"]);
            if (!parsed) {
                return Result<SessionRequest>::err(
                    {"bad_request",
                     "overrides.depth_alignment must be a string"});
            }
            request.overrides.depth_alignment = std::move(*parsed);
        }
        if (overrides.contains("d2c")) {
            auto parsed = json_string(overrides["d2c"]);
            if (!parsed) {
                return Result<SessionRequest>::err(
                    {"bad_request", "overrides.d2c must be a string"});
            }
            if (request.overrides.depth_alignment &&
                *request.overrides.depth_alignment != *parsed) {
                return Result<SessionRequest>::err(
                    {"bad_request",
                     "overrides.depth_alignment and overrides.d2c must agree"});
            }
            request.overrides.depth_alignment = std::move(*parsed);
        }
        if (overrides.contains("audio_format")) {
            auto parsed = json_string(overrides["audio_format"]);
            if (!parsed) {
                return Result<SessionRequest>::err(
                    {"bad_request",
                     "overrides.audio_format must be a string"});
            }
            request.overrides.audio_format = std::move(*parsed);
        }
    }

    return Result<SessionRequest>::ok(std::move(request));
}

nlohmann::json session_request_to_json(const SessionRequest& request) {
    nlohmann::json body;
    if (!request.selector.name.empty()) {
        body["name"] = request.selector.name;
    }
    if (!request.selector.device_uuid.empty()) {
        body["device_uuid"] = request.selector.device_uuid;
    }
    body["preset"] = request.preset_name;
    if (request.delivery_name) {
        body["delivery"] = *request.delivery_name;
    }

    nlohmann::json overrides = nlohmann::json::object();
    if (request.overrides.audio_rate) {
        overrides["audio_rate"] = *request.overrides.audio_rate;
    }
    if (request.overrides.audio_format) {
        overrides["audio_format"] = *request.overrides.audio_format;
    }
    if (request.overrides.channels) {
        overrides["channels"] = *request.overrides.channels;
    }
    if (request.overrides.depth_alignment) {
        overrides["depth_alignment"] = *request.overrides.depth_alignment;
    }
    if (request.overrides.must_match) {
        overrides["must_match"] = true;
    }
    if (!overrides.empty()) {
        body["overrides"] = std::move(overrides);
    }
    return body;
}

Result<NormalizedSourceInput> normalize_source_input(std::string_view input,
                                                     RequestOrigin origin) {
    auto parsed = parse_insightos_uri(input);
    if (!parsed) {
        parsed = parse_http_mirror(input);
    }
    if (!parsed) {
        return Result<NormalizedSourceInput>::err(
            {"parse_failed",
             "Input is not a valid insightos:// URI or HTTP mirror URL"});
    }

    auto request_json = request_json_from_uri(*parsed);
    auto request_res = session_request_from_json(request_json, origin);
    if (!request_res.ok()) {
        return Result<NormalizedSourceInput>::err(request_res.error());
    }

    NormalizedSourceInput normalized;
    normalized.uri = *parsed;
    normalized.canonical_uri = parsed->to_string();
    normalized.is_local = parsed->is_local();
    normalized.request = std::move(request_res.value());
    normalized.request_json = std::move(request_json);
    return Result<NormalizedSourceInput>::ok(std::move(normalized));
}

}  // namespace insightos::backend
