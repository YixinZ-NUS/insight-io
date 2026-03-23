/// InsightOS backend — REST API implementation.
///
/// Routes mirror the device-first model:
///   GET  /api/health        — liveness check
///   GET  /api/devices       — all public devices with presets and deliveries
///   POST /api/sessions      — create a session (resolve + start)
///   GET  /api/status        — active sessions grouped by device
///   DELETE /api/sessions/:id — destroy a session
///   POST /api/refresh       — trigger device re-discovery
///
/// Donor reference: iocontroller/src/rest/rest_server.cpp (commit 4032eb4)
/// uses the same cpp-httplib + nlohmann_json pattern.

#include "insightos/backend/rest_server.hpp"
#include "insightos/backend/version.hpp"
#include "insightos/backend/catalog.hpp"
#include "insightos/backend/request_support.hpp"
#include "insightos/backend/session_contract.hpp"
#include "insightos/backend/types.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace insightos::backend {

// ─── Helpers ────────────────────────────────────────────────────────────

namespace {

void send_error(httplib::Response& res, int status,
                const std::string& code, const std::string& message,
                const std::vector<std::string>& alternatives = {}) {
    res.status = status;
    nlohmann::json j;
    j["error"] = code;
    j["message"] = message;
    if (!alternatives.empty()) {
        j["alternatives"] = alternatives;
    }
    res.set_content(j.dump(2), "application/json");
}

const DeviceInfo* find_discovered_device(const SessionManager& mgr,
                                         const CatalogEndpoint& device_entry) {
    for (const auto& device : mgr.devices()) {
        if (device.uri == device_entry.device_uri) {
            return &device;
        }
    }
    return nullptr;
}

int alias_error_status(std::string_view code) {
    if (code == "invalid_alias") {
        return 422;
    }
    if (code == "conflict") {
        return 409;
    }
    if (code == "internal") {
        return 500;
    }
    return 404;
}

int app_error_status(std::string_view code) {
    if (code == "bad_request" || code == "parse_failed") {
        return 400;
    }
    if (code == "not_found") {
        return 404;
    }
    if (code == "conflict") {
        return 409;
    }
    if (code == "unsupported_source") {
        return 422;
    }
    if (code == "incompatible_target") {
        return 422;
    }
    if (code == "internal") {
        return 500;
    }
    return 422;
}

const StreamInfo* find_device_stream(const DeviceInfo& device,
                                     std::string_view stream_name) {
    for (const auto& stream : device.streams) {
        if (stream.name == stream_name) {
            return &stream;
        }
    }
    return nullptr;
}

nlohmann::json caps_to_json(const ResolvedCaps& caps) {
    nlohmann::json j;
    j["index"] = caps.index;
    j["format"] = caps.format;
    j["named"] = caps.to_named();
    if (caps.is_audio()) {
        j["sample_rate"] = caps.sample_rate();
        j["channels"] = caps.channels();
    } else {
        j["width"] = caps.width;
        j["height"] = caps.height;
        j["fps"] = caps.fps;
    }
    return j;
}

nlohmann::json stream_catalog_to_json(const StreamInfo& stream,
                                      bool include_caps) {
    nlohmann::json j;
    j["stream_id"] = stream.stream_id;
    j["stream_name"] = stream.name;
    j["data_kind"] = (stream.data_kind == DataKind::kFrame) ? "frame" : "message";
    if (include_caps) {
        nlohmann::json caps_arr = nlohmann::json::array();
        for (const auto& caps : stream.supported_caps) {
            caps_arr.push_back(caps_to_json(caps));
        }
        j["supported_caps"] = std::move(caps_arr);
    }
    return j;
}

nlohmann::json stream_alias_response_to_json(const CatalogEndpoint& device_entry,
                                             const StreamInfo& stream) {
    nlohmann::json body;
    body["name"] = device_entry.name;
    body["device_uuid"] = device_entry.device_uuid;
    body["stream"] = stream_catalog_to_json(stream, false);
    return body;
}

bool respond_with_stream_alias(SessionManager& mgr, httplib::Response& res,
                               std::string_view device_id,
                               std::string_view stream_name) {
    const auto device_name = std::string(device_id);
    const auto stream_label = std::string(stream_name);

    const auto* device_entry = mgr.catalog().find_endpoint(device_name);
    if (!device_entry) {
        send_error(res, 500, "internal",
                   "Updated device '" + device_name + "' not found in catalog");
        return false;
    }

    const auto* discovered = find_discovered_device(mgr, *device_entry);
    if (!discovered) {
        send_error(res, 500, "internal",
                   "Updated device '" + device_name +
                       "' not found in discovery cache");
        return false;
    }

    const auto* stream = find_device_stream(*discovered, stream_label);
    if (!stream) {
        send_error(res, 500, "internal",
                   "Updated stream '" + stream_label +
                       "' not found in discovery cache");
        return false;
    }

    res.set_content(stream_alias_response_to_json(*device_entry, *stream).dump(2),
                    "application/json");
    return true;
}

nlohmann::json preset_to_json(const PresetSpec& preset) {
    nlohmann::json j;
    j["name"] = preset.name;
    j["default_delivery"] = preset.default_delivery;
    return j;
}

nlohmann::json device_summary_to_json(const CatalogEndpoint& device_entry,
                                      const DeviceInfo* discovered,
                                      bool include_caps) {
    nlohmann::json j;
    j["name"] = device_entry.name;
    j["default_name"] = device_entry.default_name;
    j["device_uuid"] = device_entry.device_uuid;
    j["device_kind"] = to_string(device_entry.device_kind);
    j["device_uri"] = device_entry.device_uri;
    j["hardware_name"] = device_entry.hardware_name;

    nlohmann::json presets = nlohmann::json::array();
    for (const auto& preset : device_entry.presets) {
        presets.push_back(preset_to_json(preset));
    }
    j["presets"] = std::move(presets);
    j["supported_deliveries"] = device_entry.supported_deliveries;

    if (discovered) {
        j["source_device_id"] = discovered->identity.device_id;
        if (!discovered->identity.usb_vendor_id.empty() ||
            !discovered->identity.usb_product_id.empty() ||
            !discovered->identity.usb_serial.empty()) {
            nlohmann::json usb;
            if (!discovered->identity.usb_vendor_id.empty()) {
                usb["vendor"] = discovered->identity.usb_vendor_id;
            }
            if (!discovered->identity.usb_product_id.empty()) {
                usb["product"] = discovered->identity.usb_product_id;
            }
            if (!discovered->identity.usb_serial.empty()) {
                usb["serial"] = discovered->identity.usb_serial;
            }
            j["usb_info"] = std::move(usb);
        }

        nlohmann::json streams = nlohmann::json::array();
        for (const auto& stream : discovered->streams) {
            streams.push_back(stream_catalog_to_json(stream, include_caps));
        }
        j["streams"] = std::move(streams);
    }

    return j;
}

nlohmann::json stream_state_to_json(
    const StreamState& ss,
    const std::optional<IpcDescriptor>& ipc_descriptor = std::nullopt) {
    nlohmann::json j;
    j["stream_id"] = ss.stream_id;
    j["stream_name"] = ss.stream_name;
    j["promised_format"] = ss.promised_format;
    j["actual_format"] = ss.actual_format;
    if (ss.fps == 0 || is_audio_format(ss.actual_format) ||
        is_compressed_audio(ss.actual_format)) {
        j["sample_rate"] = ss.sample_rate;
        j["channels"] = ss.channels;
    } else {
        j["actual_width"] = ss.actual_width;
        j["actual_height"] = ss.actual_height;
        j["fps"] = ss.fps;
    }
    j["transport"] = ss.transport;
    j["frame_count"] = ss.frame_count;
    j["consumer_count"] = ss.consumer_count;
    if (ipc_descriptor) {
        j["ipc_descriptor"] = {
            {"socket_path", ipc_descriptor->socket_path},
            {"session_id", ipc_descriptor->session_id},
            {"stream_name", ipc_descriptor->stream_name},
            {"channel_id", ipc_descriptor->channel_id},
        };
    }
    if (!ss.rtsp_url.empty()) {
        j["rtsp_url"] = ss.rtsp_url;
    }
    return j;
}

nlohmann::json session_to_json(const StreamSession& session) {
    nlohmann::json j;
    j["session_id"] = session.session_id;
    j["state"] = session.state;
    j["name"] = session.name;
    j["device_uuid"] = session.device_uuid;
    j["preset"] = session.preset;
    j["delivery"] = session.delivery;
    j["host"] = session.host;
    j["locality"] = session.locality;
    j["capture_session_id"] = session.capture_session_id;
    if (!session.last_error.empty()) {
        j["last_error"] = session.last_error;
    }

    nlohmann::json streams = nlohmann::json::array();
    for (const auto& stream : session.streams) {
        auto desc_it = session.ipc_descriptors.find(stream.stream_name);
        std::optional<IpcDescriptor> desc;
        if (desc_it != session.ipc_descriptors.end()) {
            desc = desc_it->second;
        }
        streams.push_back(stream_state_to_json(stream, desc));
    }
    j["streams"] = std::move(streams);
    return j;
}

nlohmann::json app_source_to_json(const AppSourceView& source) {
    nlohmann::json j;
    j["source_id"] = source.source_id;
    j["target"] = source.target_name;
    j["target_kind"] = source.target_kind;
    j["input"] = source.input;
    j["canonical_uri"] = source.canonical_uri;
    j["state"] = source.state;
    j["created_at_ms"] = source.created_at_ms;
    j["updated_at_ms"] = source.updated_at_ms;
    j["request"] = session_request_to_json(source.request);
    nlohmann::json bindings = nlohmann::json::array();
    for (const auto& binding : source.bindings) {
        bindings.push_back({
            {"role", binding.role},
            {"stream_id", binding.stream_id},
            {"stream_name", binding.stream_name},
        });
    }
    j["bindings"] = std::move(bindings);
    if (!source.last_error.empty()) {
        j["last_error"] = source.last_error;
    }
    if (source.session) {
        j["session"] = session_to_json(*source.session);
    }
    return j;
}

nlohmann::json app_target_to_json(const AppTargetView& target) {
    return {
        {"target_id", target.target_id},
        {"target_name", target.target_name},
        {"target_kind", target.target_kind},
        {"contract_json", target.contract_json},
        {"created_at_ms", target.created_at_ms},
        {"updated_at_ms", target.updated_at_ms},
    };
}

nlohmann::json runtime_app_to_json(const RuntimeAppView& app) {
    nlohmann::json j;
    j["app_id"] = app.app_id;
    j["name"] = app.name;
    j["description"] = app.description;
    j["created_at_ms"] = app.created_at_ms;
    j["updated_at_ms"] = app.updated_at_ms;

    nlohmann::json targets = nlohmann::json::array();
    for (const auto& target : app.targets) {
        targets.push_back(app_target_to_json(target));
    }
    j["targets"] = std::move(targets);

    nlohmann::json sources = nlohmann::json::array();
    for (const auto& source : app.sources) {
        sources.push_back(app_source_to_json(source));
    }
    j["sources"] = std::move(sources);
    return j;
}

}  // namespace

// ─── Lifecycle ──────────────────────────────────────────────────────────

RestServer::RestServer(SessionManager& mgr, const std::string& frontend_dir)
    : mgr_(mgr), frontend_dir_(frontend_dir) {}

RestServer::~RestServer() { stop(); }

bool RestServer::start(const std::string& host, uint16_t port) {
    server_ = std::make_unique<httplib::Server>();
    server_->set_read_timeout(0, 100000);  // 100ms

    setup_routes();

    if (!server_->bind_to_port(host, port)) {
        std::cerr << "REST: failed to bind to " << host << ":" << port << "\n";
        return false;
    }

    running_.store(true);
    thread_ = std::thread([this]() { server_->listen_after_bind(); });
    return true;
}

void RestServer::stop() {
    running_.store(false);
    if (server_) server_->stop();
    if (thread_.joinable()) thread_.join();
}

// ─── Route setup ────────────────────────────────────────────────────────

void RestServer::setup_routes() {
    const auto render_devices =
        [this](httplib::Response& res, bool include_caps) {
            nlohmann::json devices = nlohmann::json::array();
            for (const auto& device_entry : mgr_.catalog().endpoints()) {
                devices.push_back(device_summary_to_json(
                    device_entry,
                    find_discovered_device(mgr_, device_entry),
                    include_caps));
            }
            nlohmann::json body;
            body["devices"] = std::move(devices);
            res.set_content(body.dump(2), "application/json");
        };

    server_->Get("/api/health",
        [](const httplib::Request&, httplib::Response& res) {
            nlohmann::json j;
            j["status"] = "ok";
            j["version"] = kVersion;
            res.set_content(j.dump(2), "application/json");
        });

    server_->Get("/api/catalog",
        [render_devices](const httplib::Request&, httplib::Response& res) {
            render_devices(res, false);
        });
    server_->Get("/api/devices",
        [render_devices](const httplib::Request&, httplib::Response& res) {
            render_devices(res, false);
        });

    server_->Get(R"(/api/devices/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto device_id = req.matches[1].str();
            const auto* device_entry = mgr_.catalog().find_endpoint(device_id);
            if (!device_entry) {
                send_error(res, 404, "not_found",
                           "Device '" + device_id + "' not found");
                return;
            }

            const auto* discovered = find_discovered_device(mgr_, *device_entry);
            res.set_content(
                device_summary_to_json(*device_entry, discovered, false).dump(2),
                "application/json");
        });

    server_->Get(R"(/api/devices/([^/]+)/caps$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto device_id = req.matches[1].str();
            const auto* device_entry = mgr_.catalog().find_endpoint(device_id);
            if (!device_entry) {
                send_error(res, 404, "not_found",
                           "Device '" + device_id + "' not found");
                return;
            }

            const auto* discovered = find_discovered_device(mgr_, *device_entry);
            if (!discovered) {
                send_error(res, 404, "not_found",
                           "Device '" + device_id + "' is offline");
                return;
            }

            nlohmann::json caps;
            for (const auto& stream : discovered->streams) {
                nlohmann::json stream_caps = nlohmann::json::array();
                for (const auto& cap : stream.supported_caps) {
                    stream_caps.push_back(caps_to_json(cap));
                }
                caps[stream.name] = std::move(stream_caps);
            }

            nlohmann::json body;
            body["name"] = device_entry->name;
            body["device_uuid"] = device_entry->device_uuid;
            body["caps"] = std::move(caps);
            res.set_content(body.dump(2), "application/json");
        });

    server_->Get(R"(/api/devices/([^/]+)/streams/([^/]+)/caps$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto device_id = req.matches[1].str();
            const auto stream_name = req.matches[2].str();
            const auto* device_entry = mgr_.catalog().find_endpoint(device_id);
            if (!device_entry) {
                send_error(res, 404, "not_found",
                           "Device '" + device_id + "' not found");
                return;
            }

            const auto* discovered = find_discovered_device(mgr_, *device_entry);
            if (!discovered) {
                send_error(res, 404, "not_found",
                           "Device '" + device_id + "' is offline");
                return;
            }

            if (const auto* stream = find_device_stream(*discovered, stream_name)) {
                nlohmann::json caps = nlohmann::json::array();
                for (const auto& cap : stream->supported_caps) {
                    caps.push_back(caps_to_json(cap));
                }

                nlohmann::json body;
                body["name"] = device_entry->name;
                body["device_uuid"] = device_entry->device_uuid;
                body["stream_id"] = stream->stream_id;
                body["stream_name"] = stream_name;
                body["supported_caps"] = std::move(caps);
                res.set_content(body.dump(2), "application/json");
                return;
            }

            send_error(res, 404, "not_found",
                       "Stream '" + stream_name + "' not found for device '" +
                           device_id + "'");
        });

    server_->Put(R"(/api/devices/([^/]+)/alias/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto device_id = req.matches[1].str();
            const auto alias = req.matches[2].str();
            auto result = mgr_.set_device_alias(device_id, alias);
            if (!result.ok()) {
                send_error(res, alias_error_status(result.error().code),
                           result.error().code, result.error().message);
                return;
            }

            const auto* device_entry = mgr_.catalog().find_endpoint(result.value());
            if (!device_entry) {
                send_error(res, 500, "internal",
                           "Updated device '" + result.value() + "' not found in catalog");
                return;
            }

            const auto* discovered = find_discovered_device(mgr_, *device_entry);
            res.set_content(
                device_summary_to_json(*device_entry, discovered, false).dump(2),
                "application/json");
        });

    server_->Delete(R"(/api/devices/([^/]+)/alias$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto device_id = req.matches[1].str();
            auto result = mgr_.clear_device_alias(device_id);
            if (!result.ok()) {
                send_error(res, alias_error_status(result.error().code),
                           result.error().code, result.error().message);
                return;
            }

            const auto* device_entry = mgr_.catalog().find_endpoint(result.value());
            if (!device_entry) {
                send_error(res, 500, "internal",
                           "Updated device '" + result.value() + "' not found in catalog");
                return;
            }

            const auto* discovered = find_discovered_device(mgr_, *device_entry);
            res.set_content(
                device_summary_to_json(*device_entry, discovered, true).dump(2),
                "application/json");
        });

    server_->Put(R"(/api/devices/([^/]+)/streams/([^/]+)/alias/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto device_id = req.matches[1].str();
            const auto stream_name = req.matches[2].str();
            const auto alias = req.matches[3].str();
            auto result = mgr_.set_stream_alias(device_id, stream_name, alias);
            if (!result.ok()) {
                send_error(res, alias_error_status(result.error().code),
                           result.error().code, result.error().message);
                return;
            }
            respond_with_stream_alias(mgr_, res, device_id, result.value());
        });

    server_->Delete(R"(/api/devices/([^/]+)/streams/([^/]+)/alias$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto device_id = req.matches[1].str();
            const auto stream_name = req.matches[2].str();
            auto result = mgr_.clear_stream_alias(device_id, stream_name);
            if (!result.ok()) {
                send_error(res, alias_error_status(result.error().code),
                           result.error().code, result.error().message);
                return;
            }
            respond_with_stream_alias(mgr_, res, device_id, result.value());
        });

    server_->Post("/api/sessions",
        [this](const httplib::Request& req, httplib::Response& res) {
            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::exception&) {
                send_error(res, 400, "bad_request", "Invalid JSON body");
                return;
            }

            auto request_res = session_request_from_json(body);
            if (!request_res.ok()) {
                send_error(res, 400, request_res.error().code,
                           request_res.error().message);
                return;
            }

            auto result = mgr_.create_session(request_res.value());
            if (auto* err = std::get_if<ResolutionError>(&result)) {
                send_error(res, 422, err->code, err->message, err->alternatives);
                return;
            }

            res.set_content(session_to_json(std::get<StreamSession>(result)).dump(2),
                            "application/json");
        });

    server_->Post("/api/apps",
        [this](const httplib::Request& req, httplib::Response& res) {
            nlohmann::json body;
            if (!req.body.empty()) {
                try {
                    body = nlohmann::json::parse(req.body);
                } catch (const nlohmann::json::exception&) {
                    send_error(res, 400, "bad_request", "Invalid JSON body");
                    return;
                }
            }
            std::string name;
            std::string description;
            if (body.contains("name")) {
                if (!body["name"].is_string()) {
                    send_error(res, 400, "bad_request",
                               "'name' must be a string");
                    return;
                }
                name = body["name"].get<std::string>();
            }
            if (body.contains("description")) {
                if (!body["description"].is_string()) {
                    send_error(res, 400, "bad_request",
                               "'description' must be a string");
                    return;
                }
                description = body["description"].get<std::string>();
            }

            auto created = mgr_.create_app(std::move(name), std::move(description));
            if (!created.ok()) {
                send_error(res, app_error_status(created.error().code),
                           created.error().code, created.error().message);
                return;
            }
            res.set_content(runtime_app_to_json(created.value()).dump(2),
                            "application/json");
        });

    server_->Get("/api/apps",
        [this](const httplib::Request&, httplib::Response& res) {
            auto apps = mgr_.list_apps();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& app : apps) {
                arr.push_back(runtime_app_to_json(app));
            }
            res.set_content(arr.dump(2), "application/json");
        });

    server_->Get(R"(/api/apps/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto app_id = req.matches[1].str();
            auto app = mgr_.get_app(app_id);
            if (!app) {
                send_error(res, 404, "not_found",
                           "App '" + app_id + "' not found");
                return;
            }
            res.set_content(runtime_app_to_json(*app).dump(2),
                            "application/json");
        });

    server_->Get(R"(/api/apps/([^/]+)/sources$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto app_id = req.matches[1].str();
            auto sources = mgr_.list_app_sources(app_id);
            if (!sources) {
                send_error(res, 404, "not_found",
                           "App '" + app_id + "' not found");
                return;
            }

            nlohmann::json arr = nlohmann::json::array();
            for (const auto& source : *sources) {
                arr.push_back(app_source_to_json(source));
            }
            res.set_content(arr.dump(2), "application/json");
        });

    server_->Get(R"(/api/apps/([^/]+)/targets$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto app_id = req.matches[1].str();
            auto targets = mgr_.list_app_targets(app_id);
            if (!targets) {
                send_error(res, 404, "not_found",
                           "App '" + app_id + "' not found");
                return;
            }
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& target : *targets) {
                arr.push_back(app_target_to_json(target));
            }
            res.set_content(arr.dump(2), "application/json");
        });

    server_->Post(R"(/api/apps/([^/]+)/targets$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::exception&) {
                send_error(res, 400, "bad_request", "Invalid JSON body");
                return;
            }

            if (!body.contains("target_name") || !body["target_name"].is_string()) {
                send_error(res, 400, "bad_request",
                           "'target_name' must be a string");
                return;
            }
            std::string target_kind = "video";
            if (body.contains("target_kind")) {
                if (!body["target_kind"].is_string()) {
                    send_error(res, 400, "bad_request",
                               "'target_kind' must be a string");
                    return;
                }
                target_kind = body["target_kind"].get<std::string>();
            }

            const auto app_id = req.matches[1].str();
            auto created = mgr_.create_app_target(
                app_id, body["target_name"].get<std::string>(),
                std::move(target_kind));
            if (!created.ok()) {
                send_error(res, app_error_status(created.error().code),
                           created.error().code, created.error().message);
                return;
            }
            res.set_content(app_target_to_json(created.value()).dump(2),
                            "application/json");
        });

    server_->Delete(R"(/api/apps/([^/]+)/targets/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto app_id = req.matches[1].str();
            const auto target_name = req.matches[2].str();
            if (!mgr_.delete_app_target(app_id, target_name)) {
                send_error(res, 404, "not_found",
                           "Target '" + target_name + "' not found or still in use");
                return;
            }
            nlohmann::json body;
            body["status"] = "deleted";
            body["app_id"] = app_id;
            body["target_name"] = target_name;
            res.set_content(body.dump(2), "application/json");
        });

    server_->Post(R"(/api/apps/([^/]+)/sources$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::exception&) {
                send_error(res, 400, "bad_request", "Invalid JSON body");
                return;
            }

            std::string input;
            if (body.contains("input") && body["input"].is_string()) {
                input = body["input"].get<std::string>();
            } else if (body.contains("uri") && body["uri"].is_string()) {
                input = body["uri"].get<std::string>();
            } else {
                send_error(res, 400, "bad_request",
                           "'input' must be a string");
                return;
            }
            if (!body.contains("target") || !body["target"].is_string()) {
                send_error(res, 400, "bad_request",
                           "'target' must be a string");
                return;
            }

            const auto app_id = req.matches[1].str();
            auto added = mgr_.add_app_source(app_id, std::move(input),
                                             body["target"].get<std::string>());
            if (!added.ok()) {
                send_error(res, app_error_status(added.error().code),
                           added.error().code, added.error().message);
                return;
            }
            res.set_content(app_source_to_json(added.value()).dump(2),
                            "application/json");
        });

    server_->Post(R"(/api/apps/([^/]+)/sources/([^/]+)/start$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto app_id = req.matches[1].str();
            const auto source_id = req.matches[2].str();
            auto updated = mgr_.start_app_source(app_id, source_id);
            if (!updated.ok()) {
                send_error(res, app_error_status(updated.error().code),
                           updated.error().code, updated.error().message);
                return;
            }
            res.set_content(app_source_to_json(updated.value()).dump(2),
                            "application/json");
        });

    server_->Post(R"(/api/apps/([^/]+)/sources/([^/]+)/stop$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            std::string last_error;
            if (!req.body.empty()) {
                nlohmann::json body;
                try {
                    body = nlohmann::json::parse(req.body);
                } catch (const nlohmann::json::exception&) {
                    send_error(res, 400, "bad_request", "Invalid JSON body");
                    return;
                }

                if (body.contains("last_error")) {
                    if (!body["last_error"].is_string()) {
                        send_error(res, 400, "bad_request",
                                   "'last_error' must be a string");
                        return;
                    }
                    last_error = body["last_error"].get<std::string>();
                }
            }

            const auto app_id = req.matches[1].str();
            const auto source_id = req.matches[2].str();
            auto updated =
                mgr_.stop_app_source(app_id, source_id, std::move(last_error));
            if (!updated.ok()) {
                send_error(res, app_error_status(updated.error().code),
                           updated.error().code, updated.error().message);
                return;
            }
            res.set_content(app_source_to_json(updated.value()).dump(2),
                            "application/json");
        });

    server_->Delete(R"(/api/apps/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto app_id = req.matches[1].str();
            if (!mgr_.destroy_app(app_id)) {
                send_error(res, 404, "not_found",
                           "App '" + app_id + "' not found");
                return;
            }
            nlohmann::json body;
            body["status"] = "destroyed";
            body["app_id"] = app_id;
            res.set_content(body.dump(2), "application/json");
        });

    server_->Get("/api/sessions",
        [this](const httplib::Request&, httplib::Response& res) {
            auto sessions = mgr_.list_sessions();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& s : sessions) {
                arr.push_back(session_to_json(s));
            }
            res.set_content(arr.dump(2), "application/json");
        });

    server_->Get(R"(/api/sessions/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto session_id = req.matches[1].str();
            auto session = mgr_.get_session(session_id);
            if (!session) {
                send_error(res, 404, "not_found",
                           "Session '" + session_id + "' not found");
                return;
            }
            res.set_content(session_to_json(*session).dump(2), "application/json");
        });

    server_->Post(R"(/api/sessions/([^/]+)/start$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto session_id = req.matches[1].str();
            auto result = mgr_.start_session(session_id);
            if (!result.ok()) {
                const auto status =
                    (result.error().code == "not_found") ? 404 : 422;
                send_error(res, status, result.error().code, result.error().message);
                return;
            }
            res.set_content(session_to_json(result.value()).dump(2),
                            "application/json");
        });

    server_->Post(R"(/api/sessions/([^/]+)/stop$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto session_id = req.matches[1].str();
            auto result = mgr_.stop_session(session_id);
            if (!result.ok()) {
                const auto status =
                    (result.error().code == "not_found") ? 404 : 409;
                send_error(res, status, result.error().code, result.error().message);
                return;
            }
            res.set_content(session_to_json(result.value()).dump(2),
                            "application/json");
        });

    server_->Delete(R"(/api/sessions/([^/]+)$)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const auto session_id = req.matches[1].str();
            if (!mgr_.destroy_session(session_id)) {
                send_error(res, 404, "not_found",
                           "Session '" + session_id + "' not found");
                return;
            }
            nlohmann::json body;
            body["status"] = "destroyed";
            body["session_id"] = session_id;
            res.set_content(body.dump(2), "application/json");
        });

    const auto refresh_devices =
        [this](const httplib::Request&, httplib::Response& res) {
            if (!mgr_.refresh()) {
                send_error(res, 500, "internal",
                           "Failed to persist refreshed devices into SQLite store");
                return;
            }
            nlohmann::json body;
            body["status"] = "ok";
            body["device_count"] = mgr_.devices().size();
            res.set_content(body.dump(2), "application/json");
        };
    server_->Post("/api/refresh", refresh_devices);
    server_->Post("/api/devices/refresh", refresh_devices);

    server_->Get("/api/status",
        [this](const httplib::Request&, httplib::Response& res) {
            auto entries = mgr_.get_status();

            nlohmann::json devices = nlohmann::json::array();
            for (const auto& entry : entries) {
                nlohmann::json device;
                device["name"] = entry.name;
                device["device_uuid"] = entry.device_uuid;
                device["preset"] = entry.preset;
                device["capture_session_id"] = entry.capture_session_id;
                device["capture_ref_count"] = entry.capture_ref_count;
                device["session_ids"] = entry.session_ids;

                nlohmann::json deliveries = nlohmann::json::array();
                for (const auto& delivery : entry.delivery_sessions) {
                    nlohmann::json item;
                    item["delivery"] = delivery.delivery;
                    item["stream_name"] = delivery.stream_name;
                    item["state"] = delivery.state;
                    item["session_ref_count"] = delivery.session_ref_count;
                    item["stream_state"] = stream_state_to_json(delivery.stream_state);
                    deliveries.push_back(std::move(item));
                }
                device["delivery_sessions"] = std::move(deliveries);
                devices.push_back(std::move(device));
            }

            nlohmann::json body;
            body["devices"] = std::move(devices);
            res.set_content(body.dump(2), "application/json");
        });

    // Exception handler
    server_->set_exception_handler(
        [](const httplib::Request&, httplib::Response& res,
           std::exception_ptr ep) {
            std::string msg = "Internal server error";
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                msg = e.what();
            } catch (...) {}
            res.status = 500;
            nlohmann::json j;
            j["error"] = "internal_error";
            j["message"] = msg;
            res.set_content(j.dump(2), "application/json");
        });

    // ── Static files (/static/*) ────────────────────────────────────────
    if (!frontend_dir_.empty() &&
        std::filesystem::is_directory(frontend_dir_)) {
        server_->set_mount_point("/static", frontend_dir_);
    }

    // ── Root and HTTP mirror routes ─────────────────────────────────────
    // Serve index.html for /, and for /{device}/{preset}[/{delivery}]
    // mirror paths (the frontend JS handles auto-session-creation).
    server_->Get(".*", [this](const httplib::Request& req,
                              httplib::Response& res) {
        const auto& path = req.path;

        // Skip /api/ paths (already handled above).
        if (path.rfind("/api/", 0) == 0) return;
        // Skip /static/ (mount point handles it).
        if (path.rfind("/static/", 0) == 0) return;

        // Serve index.html for root and mirror routes.
        if (frontend_dir_.empty()) {
            res.status = 404;
            res.set_content("Frontend not found", "text/plain");
            return;
        }
        auto index_path =
            std::filesystem::path(frontend_dir_) / "index.html";
        if (!std::filesystem::exists(index_path)) {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
            return;
        }
        std::ifstream ifs(index_path);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        res.set_content(ss.str(), "text/html");
    });
}

}  // namespace insightos::backend
