// role: REST server implementation for the current standalone backend slices.
// revision: 2026-03-26 direct-session-slice
// major changes: exposes catalog, direct-session, and runtime-status
// endpoints while keeping media realization intentionally lightweight.

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
                const std::string& message);

nlohmann::json resolved_source_to_json(const SessionResolvedSource& source) {
    nlohmann::json json = {
        {"stream_id", source.stream_id},
        {"device_key", source.device_key},
        {"public_name", source.public_name},
        {"selector", source.selector},
        {"uri", source.uri},
        {"media_kind", source.media_kind},
        {"shape_kind", source.shape_kind},
        {"delivered_caps_json", source.delivered_caps_json},
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

nlohmann::json session_to_json(const SessionRecord& session) {
    nlohmann::json json = {
        {"session_id", session.session_id},
        {"session_kind", session.session_kind},
        {"state", session.state},
        {"rtsp_enabled", session.rtsp_enabled},
        {"request_json", session.request_json},
        {"resolved_source", resolved_source_to_json(session.source)},
        {"resolved_exact_stream_id", session.source.stream_id},
        {"delivered_caps_json", session.source.delivered_caps_json},
    };
    if (session.started_at_ms > 0) {
        json["started_at_ms"] = session.started_at_ms;
    }
    if (session.stopped_at_ms > 0) {
        json["stopped_at_ms"] = session.stopped_at_ms;
    }
    json["created_at_ms"] = session.created_at_ms;
    json["updated_at_ms"] = session.updated_at_ms;
    if (!session.last_error.empty()) {
        json["last_error"] = session.last_error;
    }
    if (!session.resolved_members_json.is_null() && !session.resolved_members_json.empty()) {
        json["resolved_members_json"] = session.resolved_members_json;
    }
    if (!session.source.capture_policy_json.is_null() &&
        !session.source.capture_policy_json.empty()) {
        json["capture_policy_json"] = session.source.capture_policy_json;
    }
    if (session.rtsp_enabled && !session.rtsp_url.empty()) {
        json["rtsp_url"] = session.rtsp_url;
    }
    return json;
}

bool parse_json_body(const httplib::Request& request,
                     httplib::Response& response,
                     nlohmann::json& body) {
    try {
        body = nlohmann::json::parse(request.body);
        return true;
    } catch (...) {
        send_error(response, 400, "parse_failed",
                   "Request body must be valid JSON");
        return false;
    }
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
                       SessionService& sessions,
                       std::string frontend_dir)
    : store_(store),
      catalog_(catalog),
      sessions_(sessions),
      frontend_dir_(std::move(frontend_dir)) {}

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
        const auto status = sessions_.runtime_status();
        nlohmann::json body = {
            {"status", "ok"},
            {"version", kVersion},
            {"db_path", store_.database_path()},
            {"frontend_path", frontend_dir_},
            {"catalog_device_count", devices.size()},
            {"session_count", status.total_sessions},
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

    server_->Post("/api/sessions",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      nlohmann::json body;
                      if (!parse_json_body(request, response, body)) {
                          return;
                      }

                      if (!body.contains("input") || !body.at("input").is_string()) {
                          send_error(response, 400, "bad_request",
                                     "Request body must contain string field 'input'");
                          return;
                      }

                      bool rtsp_enabled = false;
                      if (body.contains("rtsp_enabled")) {
                          if (!body.at("rtsp_enabled").is_boolean()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'rtsp_enabled' must be boolean when present");
                              return;
                          }
                          rtsp_enabled = body.at("rtsp_enabled").get<bool>();
                      }

                      SessionRecord created;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!sessions_.create_direct_session(body.at("input").get<std::string>(),
                                                           rtsp_enabled,
                                                           created,
                                                           error_status,
                                                           error_code,
                                                           error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }

                      response.status = 201;
                      response.set_content(session_to_json(created).dump(2),
                                           "application/json");
                  });

    server_->Get("/api/sessions", [this](const httplib::Request&, httplib::Response& response) {
        nlohmann::json body = {
            {"sessions", nlohmann::json::array()},
        };
        for (const auto& session : sessions_.list_sessions()) {
            body["sessions"].push_back(session_to_json(session));
        }
        response.set_content(body.dump(2), "application/json");
    });

    server_->Get(R"(/api/sessions/(\d+))",
                 [this](const httplib::Request& request, httplib::Response& response) {
                     const auto session_id = std::stoll(request.matches[1].str());
                     const auto session = sessions_.get_session(session_id);
                     if (!session.has_value()) {
                         send_error(response, 404, "not_found",
                                    "Session '" + std::to_string(session_id) + "' not found");
                         return;
                     }
                     response.set_content(session_to_json(*session).dump(2),
                                          "application/json");
                 });

    server_->Post(R"(/api/sessions/(\d+)/start)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      const auto session_id = std::stoll(request.matches[1].str());
                      SessionRecord updated;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!sessions_.start_session(session_id,
                                                   updated,
                                                   error_status,
                                                   error_code,
                                                   error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }
                      response.set_content(session_to_json(updated).dump(2),
                                           "application/json");
                  });

    server_->Post(R"(/api/sessions/(\d+)/stop)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      const auto session_id = std::stoll(request.matches[1].str());
                      SessionRecord updated;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!sessions_.stop_session(session_id,
                                                  updated,
                                                  error_status,
                                                  error_code,
                                                  error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }
                      response.set_content(session_to_json(updated).dump(2),
                                           "application/json");
                  });

    server_->Delete(R"(/api/sessions/(\d+))",
                    [this](const httplib::Request& request, httplib::Response& response) {
                        const auto session_id = std::stoll(request.matches[1].str());
                        int error_status = 500;
                        std::string error_code;
                        std::string error_message;
                        if (!sessions_.delete_session(session_id,
                                                      error_status,
                                                      error_code,
                                                      error_message)) {
                            send_error(response, error_status, error_code, error_message);
                            return;
                        }
                        response.status = 204;
                    });

    server_->Get("/api/status", [this](const httplib::Request&, httplib::Response& response) {
        const auto snapshot = sessions_.runtime_status();
        nlohmann::json body = {
            {"total_sessions", snapshot.total_sessions},
            {"active_sessions", snapshot.active_sessions},
            {"stopped_sessions", snapshot.stopped_sessions},
            {"sessions", nlohmann::json::array()},
        };
        for (const auto& session : snapshot.sessions) {
            body["sessions"].push_back(session_to_json(session));
        }
        response.set_content(body.dump(2), "application/json");
    });
}

}  // namespace insightio::backend
