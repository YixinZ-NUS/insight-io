// role: bootstrap REST server implementation for the standalone insight-io backend.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: exposes the first runtime-tested HTTP surface, GET /api/health.

#include "insightio/backend/rest_server.hpp"

#include "insightio/backend/version.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace insightio::backend {

namespace {

nlohmann::json source_to_json(const CatalogSource& source) {
    nlohmann::json json = {
        {"selector_key", source.selector_key},
        {"selector", source.selector},
        {"uri", source.uri},
        {"media_kind", source.media_kind},
        {"shape_kind", source.shape_kind},
        {"caps_json", source.caps_json},
        {"publications_json", source.publications_json},
    };
    if (!source.channel.empty()) {
        json["channel"] = source.channel;
    }
    if (!source.group_key.empty()) {
        json["group_key"] = source.group_key;
    }
    if (!source.capture_policy_json.is_null() && !source.capture_policy_json.empty()) {
        json["capture_policy_json"] = source.capture_policy_json;
    }
    if (!source.members_json.is_null() && !source.members_json.empty()) {
        json["members_json"] = source.members_json;
    }
    return json;
}

nlohmann::json device_to_json(const CatalogDevice& device) {
    nlohmann::json json = {
        {"device_key", device.device_key},
        {"public_name", device.public_name},
        {"name", device.public_name},
        {"default_name", device.default_name},
        {"driver", device.driver},
        {"status", device.status},
        {"metadata_json", device.metadata_json},
        {"sources", nlohmann::json::array()},
    };
    for (const auto& source : device.sources) {
        json["sources"].push_back(source_to_json(source));
    }
    return json;
}

void send_error(httplib::Response& response,
                int status,
                const std::string& code,
                const std::string& message) {
    response.status = status;
    response.set_content(
        nlohmann::json{{"error", code}, {"message", message}}.dump(2),
        "application/json");
}

}  // namespace

RestServer::RestServer(SchemaStore& store,
                       CatalogService& catalog,
                       std::string frontend_dir)
    : store_(store), catalog_(catalog), frontend_dir_(std::move(frontend_dir)) {}

RestServer::~RestServer() {
    stop();
}

bool RestServer::start(const std::string& host, uint16_t port) {
    if (running_.load()) {
        return true;
    }

    server_ = std::make_unique<httplib::Server>();
    setup_routes();

    if (!server_->bind_to_port(host, port)) {
        server_.reset();
        return false;
    }

    running_.store(true);
    thread_ = std::thread([this]() {
        server_->listen_after_bind();
        running_.store(false);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return running_.load();
}

void RestServer::stop() {
    if (server_) {
        server_->stop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    server_.reset();
    running_.store(false);
}

void RestServer::setup_routes() {
    server_->Get("/api/health", [this](const httplib::Request&, httplib::Response& response) {
        const auto devices = catalog_.list_devices();
        nlohmann::json body = {
            {"status", "ok"},
            {"version", kVersion},
            {"db_path", store_.database_path()},
            {"frontend_path", frontend_dir_},
            {"catalog_device_count", devices.size()},
        };
        response.set_content(body.dump(2), "application/json");
    });

    server_->Get("/api/devices", [this](const httplib::Request&, httplib::Response& response) {
        nlohmann::json body = {
            {"devices", nlohmann::json::array()},
            {"errors", catalog_.last_errors()},
        };
        for (const auto& device : catalog_.list_devices()) {
            body["devices"].push_back(device_to_json(device));
        }
        response.set_content(body.dump(2), "application/json");
    });

    server_->Get(R"(/api/devices/([^/]+))",
                 [this](const httplib::Request& request, httplib::Response& response) {
                     const auto device_name = request.matches[1].str();
                     const auto device = catalog_.get_device(device_name);
                     if (!device.has_value()) {
                         send_error(response, 404, "not_found",
                                    "Device '" + device_name + "' not found");
                         return;
                     }
                     response.set_content(device_to_json(*device).dump(2),
                                          "application/json");
                 });

    server_->Post(R"(/api/devices/([^/]+)/alias)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      nlohmann::json body;
                      try {
                          body = nlohmann::json::parse(request.body);
                      } catch (...) {
                          send_error(response, 400, "parse_failed",
                                     "Request body must be valid JSON");
                          return;
                      }

                      if (!body.contains("public_name") ||
                          !body.at("public_name").is_string()) {
                          send_error(response, 400, "bad_request",
                                     "Request body must contain string field 'public_name'");
                          return;
                      }

                      CatalogDevice updated;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!catalog_.set_alias(request.matches[1].str(),
                                              body.at("public_name").get<std::string>(),
                                              updated,
                                              error_status,
                                              error_code,
                                              error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }

                      response.set_content(device_to_json(updated).dump(2),
                                           "application/json");
                  });
}

}  // namespace insightio::backend
