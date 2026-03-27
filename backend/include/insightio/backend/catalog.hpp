#pragma once

// role: persisted device/source catalog service for the standalone backend.
// revision: 2026-03-27 task8-rtsp-runtime-validation
// major changes: keeps the derived URI catalog shape while aligning default
// RTSP publication addresses with the local mediamtx-backed runtime contract.

#include "insightio/backend/discovery.hpp"
#include "insightio/backend/schema_store.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace insightio::backend {

struct CatalogSource {
    std::string selector;
    std::string uri;
    std::string media_kind;
    std::string shape_kind;
    std::string channel;
    std::string group_key;
    nlohmann::json caps_json;
    nlohmann::json capture_policy_json;
    nlohmann::json members_json;
    nlohmann::json publications_json;
};

struct CatalogDevice {
    std::string device_key;
    std::string public_name;
    std::string default_name;
    std::string driver;
    std::string status;
    nlohmann::json metadata_json;
    std::vector<CatalogSource> sources;
};

using DiscoveryFn = std::function<DiscoveryResult()>;

class CatalogService {
public:
    CatalogService(SchemaStore& store,
                   DiscoveryFn discovery_fn,
                   std::string uri_host = "localhost",
                   std::string rtsp_host = "127.0.0.1:8554");

    bool initialize();
    bool refresh();

    [[nodiscard]] std::vector<CatalogDevice> list_devices() const;
    [[nodiscard]] std::optional<CatalogDevice> get_device(
        std::string_view public_name) const;
    [[nodiscard]] std::vector<std::string> last_errors() const;

    bool set_alias(std::string_view current_public_name,
                   const std::string& alias,
                   CatalogDevice& updated_device,
                   int& error_status,
                   std::string& error_code,
                   std::string& error_message);

private:
    SchemaStore& store_;
    DiscoveryFn discovery_fn_;
    std::string uri_host_;
    std::string rtsp_host_;

    mutable std::mutex mutex_;
    std::vector<CatalogDevice> devices_;
    std::vector<std::string> last_errors_;
};

}  // namespace insightio::backend
