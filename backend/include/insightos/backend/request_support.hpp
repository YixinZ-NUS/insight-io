#pragma once

#include "insightos/backend/result.hpp"
#include "insightos/backend/session_contract.hpp"
#include "insightos/backend/uri.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace insightos::backend {

struct NormalizedSourceInput {
    InsightOsUri uri;
    std::string canonical_uri;
    bool is_local{false};
    SessionRequest request;
    nlohmann::json request_json;
};

Result<SessionRequest> session_request_from_json(
    const nlohmann::json& body,
    RequestOrigin default_origin = RequestOrigin::kHttpApi);

nlohmann::json session_request_to_json(const SessionRequest& request);

Result<NormalizedSourceInput> normalize_source_input(
    std::string_view input,
    RequestOrigin origin = RequestOrigin::kUri);

}  // namespace insightos::backend
