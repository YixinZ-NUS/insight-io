// role: REST server implementation for the current standalone backend slices.
// revision: 2026-03-26 task6-serving-runtime-reuse
// major changes: exposes catalog, direct-session, app/route/source, and
// runtime-status endpoints while keeping media realization lightweight, route
// responses aligned with the documented `expect` field, surfacing serving
// runtime reuse, and hardening path-id parsing against 64-bit overflow.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/rest_server.hpp"

#include "insightio/backend/version.hpp"

#include <charconv>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>
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

nlohmann::json app_to_json(const AppRecord& app) {
    nlohmann::json json = {
        {"app_id", app.app_id},
        {"name", app.name},
        {"created_at_ms", app.created_at_ms},
        {"updated_at_ms", app.updated_at_ms},
    };
    if (!app.description.empty()) {
        json["description"] = app.description;
    }
    if (!app.config_json.is_null() && !app.config_json.empty()) {
        json["config_json"] = app.config_json;
    }
    return json;
}

nlohmann::json route_to_json(const RouteRecord& route) {
    nlohmann::json json = {
        {"route_id", route.route_id},
        {"route_name", route.route_name},
        {"target_resource_name", route.target_resource_name},
        {"created_at_ms", route.created_at_ms},
        {"updated_at_ms", route.updated_at_ms},
    };
    if (!route.expect_json.is_null() && !route.expect_json.empty()) {
        json["expect"] = route.expect_json;
    }
    if (!route.config_json.is_null() && !route.config_json.empty()) {
        json["config_json"] = route.config_json;
    }
    return json;
}

nlohmann::json serving_runtime_view_to_json(
    const SessionRecord::ServingRuntimeView& runtime) {
    nlohmann::json json = {
        {"runtime_key", runtime.runtime_key},
        {"owner_session_id", runtime.owner_session_id},
        {"state", runtime.state},
        {"rtsp_enabled", runtime.rtsp_enabled},
        {"shared", runtime.shared},
        {"consumer_count", runtime.consumer_count},
        {"consumer_session_ids", runtime.consumer_session_ids},
    };
    if (!runtime.last_error.empty()) {
        json["last_error"] = runtime.last_error;
    }
    if (!runtime.ipc_socket_path.empty()) {
        json["ipc_socket_path"] = runtime.ipc_socket_path;
    }
    if (!runtime.ipc_channels.empty()) {
        json["ipc_channels"] = nlohmann::json::array();
        for (const auto& channel : runtime.ipc_channels) {
            nlohmann::json channel_json = {
                {"channel_id", channel.channel_id},
                {"stream_name", channel.stream_name},
                {"media_kind", channel.media_kind},
                {"delivered_caps_json", channel.delivered_caps_json},
                {"attached_consumer_count", channel.attached_consumer_count},
                {"frames_published", channel.frames_published},
            };
            if (!channel.route_name.empty()) {
                channel_json["route_name"] = channel.route_name;
            }
            if (!channel.selector.empty()) {
                channel_json["selector"] = channel.selector;
            }
            json["ipc_channels"].push_back(std::move(channel_json));
        }
    }
    if (runtime.rtsp_publication.has_value()) {
        json["rtsp_publication"] = {
            {"publication_id", runtime.rtsp_publication->publication_id},
            {"stream_name", runtime.rtsp_publication->stream_name},
            {"selector", runtime.rtsp_publication->selector},
            {"url", runtime.rtsp_publication->url},
            {"state", runtime.rtsp_publication->state},
            {"publication_profile", runtime.rtsp_publication->publication_profile},
            {"transport", runtime.rtsp_publication->transport},
            {"promised_format", runtime.rtsp_publication->promised_format},
            {"actual_format", runtime.rtsp_publication->actual_format},
            {"frames_forwarded", runtime.rtsp_publication->frames_forwarded},
        };
        if (!runtime.rtsp_publication->last_error.empty()) {
            json["rtsp_publication"]["last_error"] =
                runtime.rtsp_publication->last_error;
        }
    }
    return json;
}

nlohmann::json serving_runtime_snapshot_to_json(
    const ServingRuntimeSnapshot& runtime) {
    nlohmann::json json = {
        {"runtime_key", runtime.runtime_key},
        {"stream_id", runtime.stream_id},
        {"owner_session_id", runtime.owner_session_id},
        {"state", runtime.state},
        {"rtsp_enabled", runtime.rtsp_enabled},
        {"consumer_count", runtime.consumer_count},
        {"consumer_session_ids", runtime.consumer_session_ids},
        {"resolved_source", resolved_source_to_json(runtime.source)},
    };
    if (!runtime.last_error.empty()) {
        json["last_error"] = runtime.last_error;
    }
    if (!runtime.ipc_socket_path.empty()) {
        json["ipc_socket_path"] = runtime.ipc_socket_path;
    }
    if (!runtime.ipc_channels.empty()) {
        json["ipc_channels"] = nlohmann::json::array();
        for (const auto& channel : runtime.ipc_channels) {
            nlohmann::json channel_json = {
                {"channel_id", channel.channel_id},
                {"stream_name", channel.stream_name},
                {"media_kind", channel.media_kind},
                {"delivered_caps_json", channel.delivered_caps_json},
                {"attached_consumer_count", channel.attached_consumer_count},
                {"frames_published", channel.frames_published},
            };
            if (!channel.route_name.empty()) {
                channel_json["route_name"] = channel.route_name;
            }
            if (!channel.selector.empty()) {
                channel_json["selector"] = channel.selector;
            }
            json["ipc_channels"].push_back(std::move(channel_json));
        }
    }
    if (runtime.rtsp_publication.has_value()) {
        json["rtsp_publication"] = {
            {"publication_id", runtime.rtsp_publication->publication_id},
            {"stream_name", runtime.rtsp_publication->stream_name},
            {"selector", runtime.rtsp_publication->selector},
            {"url", runtime.rtsp_publication->url},
            {"state", runtime.rtsp_publication->state},
            {"publication_profile", runtime.rtsp_publication->publication_profile},
            {"transport", runtime.rtsp_publication->transport},
            {"promised_format", runtime.rtsp_publication->promised_format},
            {"actual_format", runtime.rtsp_publication->actual_format},
            {"frames_forwarded", runtime.rtsp_publication->frames_forwarded},
        };
        if (!runtime.rtsp_publication->last_error.empty()) {
            json["rtsp_publication"]["last_error"] =
                runtime.rtsp_publication->last_error;
        }
    }
    if (!runtime.resolved_members_json.is_null() &&
        !runtime.resolved_members_json.empty()) {
        json["resolved_members_json"] = runtime.resolved_members_json;
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
    if (session.serving_runtime.has_value()) {
        json["serving_runtime"] = serving_runtime_view_to_json(*session.serving_runtime);
    }
    return json;
}

nlohmann::json source_binding_to_json(const AppSourceRecord& source) {
    nlohmann::json json = {
        {"source_id", source.source_id},
        {"target", source.target_name},
        {"uri", source.source.uri},
        {"state", source.state},
        {"rtsp_enabled", source.rtsp_enabled},
        {"resolved_exact_stream_id", source.stream_id},
        {"delivered_caps_json", source.source.delivered_caps_json},
        {"capture_policy_json", source.source.capture_policy_json},
        {"created_at_ms", source.created_at_ms},
        {"updated_at_ms", source.updated_at_ms},
    };
    if (!source.target_resource_name.empty()) {
        json["target_resource_name"] = source.target_resource_name;
    }
    if (!source.last_error.empty()) {
        json["last_error"] = source.last_error;
    }
    if (!source.resolved_members_json.is_null() && !source.resolved_members_json.empty()) {
        json["resolved_members_json"] = source.resolved_members_json;
    }
    if (source.source_session_id > 0) {
        json["source_session_id"] = source.source_session_id;
    }
    if (source.active_session_id > 0) {
        json["active_session_id"] = source.active_session_id;
    }
    if (!source.rtsp_url.empty()) {
        json["rtsp_url"] = source.rtsp_url;
    }
    if (source.source_session.has_value()) {
        json["source_session"] = session_to_json(*source.source_session);
    }
    if (source.active_session.has_value()) {
        json["active_session"] = session_to_json(*source.active_session);
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

bool parse_path_id(const httplib::Request& request,
                   httplib::Response& response,
                   size_t match_index,
                   std::string_view field_name,
                   std::int64_t& value) {
    const auto raw = request.matches[match_index].str();
    const auto* begin = raw.data();
    const auto* end = begin + raw.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec == std::errc{} && result.ptr == end) {
        return true;
    }

    send_error(response,
               400,
               "bad_request",
               "Path parameter '" + std::string(field_name) +
                   "' must fit in a signed 64-bit integer");
    return false;
}

}  // namespace

RestServer::RestServer(SchemaStore& store,
                       CatalogService& catalog,
                       SessionService& sessions,
                       AppService& apps,
                       std::string frontend_dir)
    : store_(store),
      catalog_(catalog),
      sessions_(sessions),
      apps_(apps),
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
            {"ipc_socket_path", sessions_.ipc_socket_path()},
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

    server_->Post("/api/apps",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      nlohmann::json body;
                      if (!parse_json_body(request, response, body)) {
                          return;
                      }
                      if (!body.contains("name") || !body.at("name").is_string()) {
                          send_error(response, 400, "bad_request",
                                     "Request body must contain string field 'name'");
                          return;
                      }

                      std::string description;
                      if (body.contains("description")) {
                          if (!body.at("description").is_string()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'description' must be string when present");
                              return;
                          }
                          description = body.at("description").get<std::string>();
                      }

                      nlohmann::json config_json = nullptr;
                      if (body.contains("config_json")) {
                          if (!body.at("config_json").is_object()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'config_json' must be object when present");
                              return;
                          }
                          config_json = body.at("config_json");
                      }

                      AppRecord created;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!apps_.create_app(body.at("name").get<std::string>(),
                                            description,
                                            config_json,
                                            created,
                                            error_status,
                                            error_code,
                                            error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }

                      response.status = 201;
                      response.set_content(app_to_json(created).dump(2),
                                           "application/json");
                  });

    server_->Get("/api/apps", [this](const httplib::Request&, httplib::Response& response) {
        nlohmann::json body = {
            {"apps", nlohmann::json::array()},
        };
        for (const auto& app : apps_.list_apps()) {
            body["apps"].push_back(app_to_json(app));
        }
        response.set_content(body.dump(2), "application/json");
    });

    server_->Get(R"(/api/apps/(\d+))",
                 [this](const httplib::Request& request, httplib::Response& response) {
                     std::int64_t app_id = 0;
                     if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                         return;
                     }
                     const auto app = apps_.get_app(app_id);
                     if (!app.has_value()) {
                         send_error(response, 404, "not_found",
                                    "App '" + std::to_string(app_id) + "' not found");
                         return;
                     }
                     response.set_content(app_to_json(*app).dump(2),
                                          "application/json");
                 });

    server_->Delete(R"(/api/apps/(\d+))",
                    [this](const httplib::Request& request, httplib::Response& response) {
                        std::int64_t app_id = 0;
                        if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                            return;
                        }
                        int error_status = 500;
                        std::string error_code;
                        std::string error_message;
                        if (!apps_.delete_app(app_id,
                                              error_status,
                                              error_code,
                                              error_message)) {
                            send_error(response, error_status, error_code, error_message);
                            return;
                        }
                        response.status = 204;
                    });

    server_->Post(R"(/api/apps/(\d+)/routes)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      std::int64_t app_id = 0;
                      if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                          return;
                      }
                      nlohmann::json body;
                      if (!parse_json_body(request, response, body)) {
                          return;
                      }
                      if (!body.contains("route_name") ||
                          !body.at("route_name").is_string()) {
                          send_error(response, 400, "bad_request",
                                     "Request body must contain string field 'route_name'");
                          return;
                      }

                      nlohmann::json expect_json = nullptr;
                      if (body.contains("expect")) {
                          if (!body.at("expect").is_object()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'expect' must be object when present");
                              return;
                          }
                          expect_json = body.at("expect");
                      }

                      nlohmann::json config_json = nullptr;
                      if (body.contains("config_json")) {
                          if (!body.at("config_json").is_object()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'config_json' must be object when present");
                              return;
                          }
                          config_json = body.at("config_json");
                      }

                      RouteRecord created;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!apps_.create_route(app_id,
                                              body.at("route_name").get<std::string>(),
                                              expect_json,
                                              config_json,
                                              created,
                                              error_status,
                                              error_code,
                                              error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }

                      response.status = 201;
                      response.set_content(route_to_json(created).dump(2),
                                           "application/json");
                  });

    server_->Get(R"(/api/apps/(\d+)/routes)",
                 [this](const httplib::Request& request, httplib::Response& response) {
                     std::int64_t app_id = 0;
                     if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                         return;
                     }
                     std::vector<RouteRecord> routes;
                     int error_status = 500;
                     std::string error_code;
                     std::string error_message;
                     if (!apps_.list_routes(app_id,
                                            routes,
                                            error_status,
                                            error_code,
                                            error_message)) {
                         send_error(response, error_status, error_code, error_message);
                         return;
                     }
                     nlohmann::json body = {
                         {"routes", nlohmann::json::array()},
                     };
                     for (const auto& route : routes) {
                         body["routes"].push_back(route_to_json(route));
                     }
                     response.set_content(body.dump(2), "application/json");
                 });

    server_->Delete(R"(/api/apps/(\d+)/routes/(.+))",
                    [this](const httplib::Request& request, httplib::Response& response) {
                        std::int64_t app_id = 0;
                        if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                            return;
                        }
                        const auto route_name = request.matches[2].str();
                        int error_status = 500;
                        std::string error_code;
                        std::string error_message;
                        if (!apps_.delete_route(app_id,
                                                route_name,
                                                error_status,
                                                error_code,
                                                error_message)) {
                            send_error(response, error_status, error_code, error_message);
                            return;
                        }
                        response.status = 204;
                    });

    server_->Get(R"(/api/apps/(\d+)/sources)",
                 [this](const httplib::Request& request, httplib::Response& response) {
                     std::int64_t app_id = 0;
                     if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                         return;
                     }
                     std::vector<AppSourceRecord> sources;
                     int error_status = 500;
                     std::string error_code;
                     std::string error_message;
                     if (!apps_.list_sources(app_id,
                                             sources,
                                             error_status,
                                             error_code,
                                             error_message)) {
                         send_error(response, error_status, error_code, error_message);
                         return;
                     }
                     nlohmann::json body = {
                         {"sources", nlohmann::json::array()},
                     };
                     for (const auto& source : sources) {
                         body["sources"].push_back(source_binding_to_json(source));
                     }
                     response.set_content(body.dump(2), "application/json");
                 });

    server_->Post(R"(/api/apps/(\d+)/sources)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      std::int64_t app_id = 0;
                      if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                          return;
                      }
                      nlohmann::json body;
                      if (!parse_json_body(request, response, body)) {
                          return;
                      }
                      if (!body.contains("target") || !body.at("target").is_string()) {
                          send_error(response, 400, "bad_request",
                                     "Request body must contain string field 'target'");
                          return;
                      }

                      std::optional<std::string> input;
                      if (body.contains("input")) {
                          if (!body.at("input").is_string()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'input' must be string when present");
                              return;
                          }
                          input = body.at("input").get<std::string>();
                      }

                      std::optional<std::int64_t> session_id;
                      if (body.contains("session_id")) {
                          if (!body.at("session_id").is_number_integer()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'session_id' must be integer when present");
                              return;
                          }
                          session_id = body.at("session_id").get<std::int64_t>();
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

                      AppSourceRecord created;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!apps_.create_source(app_id,
                                               input,
                                               session_id,
                                               body.at("target").get<std::string>(),
                                               rtsp_enabled,
                                               created,
                                               error_status,
                                               error_code,
                                               error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }

                      response.status = 201;
                      response.set_content(source_binding_to_json(created).dump(2),
                                           "application/json");
                  });

    server_->Post(R"(/api/apps/(\d+)/sources/(\d+)/start)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      std::int64_t app_id = 0;
                      if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                          return;
                      }
                      std::int64_t source_id = 0;
                      if (!parse_path_id(request, response, 2, "source_id", source_id)) {
                          return;
                      }
                      AppSourceRecord updated;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!apps_.start_source(app_id,
                                              source_id,
                                              updated,
                                              error_status,
                                              error_code,
                                              error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }
                      response.set_content(source_binding_to_json(updated).dump(2),
                                           "application/json");
                  });

    server_->Post(R"(/api/apps/(\d+)/sources/(\d+)/stop)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      std::int64_t app_id = 0;
                      if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                          return;
                      }
                      std::int64_t source_id = 0;
                      if (!parse_path_id(request, response, 2, "source_id", source_id)) {
                          return;
                      }
                      AppSourceRecord updated;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!apps_.stop_source(app_id,
                                             source_id,
                                             updated,
                                             error_status,
                                             error_code,
                                             error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }
                      response.set_content(source_binding_to_json(updated).dump(2),
                                           "application/json");
                  });

    server_->Post(R"(/api/apps/(\d+)/sources/(\d+)/rebind)",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      std::int64_t app_id = 0;
                      if (!parse_path_id(request, response, 1, "app_id", app_id)) {
                          return;
                      }
                      std::int64_t source_id = 0;
                      if (!parse_path_id(request, response, 2, "source_id", source_id)) {
                          return;
                      }
                      nlohmann::json body;
                      if (!parse_json_body(request, response, body)) {
                          return;
                      }

                      std::optional<std::string> input;
                      if (body.contains("input")) {
                          if (!body.at("input").is_string()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'input' must be string when present");
                              return;
                          }
                          input = body.at("input").get<std::string>();
                      }

                      std::optional<std::int64_t> session_id;
                      if (body.contains("session_id")) {
                          if (!body.at("session_id").is_number_integer()) {
                              send_error(response, 400, "bad_request",
                                         "Field 'session_id' must be integer when present");
                              return;
                          }
                          session_id = body.at("session_id").get<std::int64_t>();
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

                      AppSourceRecord updated;
                      int error_status = 500;
                      std::string error_code;
                      std::string error_message;
                      if (!apps_.rebind_source(app_id,
                                               source_id,
                                               input,
                                               session_id,
                                               rtsp_enabled,
                                               updated,
                                               error_status,
                                               error_code,
                                               error_message)) {
                          send_error(response, error_status, error_code, error_message);
                          return;
                      }
                      response.set_content(source_binding_to_json(updated).dump(2),
                                           "application/json");
                  });

    server_->Get(R"(/api/sessions/(\d+))",
                 [this](const httplib::Request& request, httplib::Response& response) {
                     std::int64_t session_id = 0;
                     if (!parse_path_id(request, response, 1, "session_id", session_id)) {
                         return;
                     }
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
                      std::int64_t session_id = 0;
                      if (!parse_path_id(request, response, 1, "session_id", session_id)) {
                          return;
                      }
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
                      std::int64_t session_id = 0;
                      if (!parse_path_id(request, response, 1, "session_id", session_id)) {
                          return;
                      }
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
                        std::int64_t session_id = 0;
                        if (!parse_path_id(request, response, 1, "session_id", session_id)) {
                            return;
                        }
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
            {"ipc_socket_path", sessions_.ipc_socket_path()},
            {"total_sessions", snapshot.total_sessions},
            {"active_sessions", snapshot.active_sessions},
            {"stopped_sessions", snapshot.stopped_sessions},
            {"total_serving_runtimes", snapshot.total_serving_runtimes},
            {"sessions", nlohmann::json::array()},
            {"serving_runtimes", nlohmann::json::array()},
        };
        for (const auto& session : snapshot.sessions) {
            body["sessions"].push_back(session_to_json(session));
        }
        for (const auto& runtime : snapshot.serving_runtimes) {
            body["serving_runtimes"].push_back(serving_runtime_snapshot_to_json(runtime));
        }
        response.set_content(body.dump(2), "application/json");
    });
}

}  // namespace insightio::backend
