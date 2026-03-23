/// InsightOS backend — REST alias and catalog tests.

#include "insightos/backend/discovery.hpp"
#include "insightos/backend/device_store.hpp"
#include "insightos/backend/rest_server.hpp"
#include "insightos/backend/session.hpp"
#include "insightos/backend/types.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

#define TEST(name)                                                         \
    void test_##name();                                                    \
    struct Register_##name {                                               \
        Register_##name() { tests().push_back({#name, test_##name}); }     \
    } register_##name;                                                     \
    void test_##name()

struct TestEntry {
    const char* name;
    void (*fn)();
};

std::vector<TestEntry>& tests() {
    static std::vector<TestEntry> entries;
    return entries;
}

#define EXPECT_EQ(a, b)                                                    \
    do {                                                                   \
        if ((a) != (b)) {                                                  \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected: " << (b)                         \
                      << "\n    got:      " << (a) << "\n";                \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define EXPECT_TRUE(x)                                                     \
    do {                                                                   \
        if (!(x)) {                                                        \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected true\n";                          \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define EXPECT_FALSE(x)                                                    \
    do {                                                                   \
        if ((x)) {                                                         \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected false\n";                         \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

using namespace insightos::backend;

std::vector<DeviceInfo> g_discovered_devices;

std::string make_temp_db_path() {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("insightos-rest-test-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter++) + ".sqlite3");
    return path.string();
}

DeviceInfo make_camera() {
    DeviceInfo device;
    device.uri = "v4l2:/dev/video0";
    device.kind = DeviceKind::kV4l2;
    device.name = "Web Camera";
    device.state = DeviceState::kDiscovered;
    device.identity.device_uri = device.uri;
    device.identity.device_id = "video0";
    device.identity.kind_str = "v4l2";
    device.identity.hardware_name = device.name;
    device.identity.usb_serial = "SN001";

    StreamInfo image;
    image.stream_id = "image";
    image.name = "frame";
    image.data_kind = DataKind::kFrame;
    image.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 1280, 720, 30});
    device.streams.push_back(std::move(image));
    return device;
}

DeviceInfo make_test_camera() {
    auto device = make_camera();
    device.uri = "test:front-camera";
    device.name = "Front Camera";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "front-camera";
    device.identity.hardware_name = device.name;
    device.identity.usb_serial = "TESTCAM001";
    return device;
}

DeviceInfo make_test_microphone() {
    DeviceInfo device;
    device.uri = "test:desk-mic";
    device.kind = DeviceKind::kPipeWire;
    device.name = "Desk Mic";
    device.state = DeviceState::kDiscovered;
    device.identity.device_uri = device.uri;
    device.identity.device_id = "desk-mic";
    device.identity.kind_str = "pw";
    device.identity.hardware_name = device.name;
    device.identity.usb_serial = "TESTMIC001";

    StreamInfo audio;
    audio.stream_id = "audio";
    audio.name = "audio";
    audio.data_kind = DataKind::kFrame;
    audio.supported_caps.push_back(ResolvedCaps{0, "s16le", 48000, 2, 0});
    device.streams.push_back(std::move(audio));
    return device;
}

DeviceInfo make_orbbec_camera() {
    DeviceInfo device;
    device.uri = "orbbec://device-001";
    device.kind = DeviceKind::kOrbbec;
    device.name = "Depth Cam";
    device.state = DeviceState::kDiscovered;
    device.identity.device_uri = device.uri;
    device.identity.device_id = "depth-cam";
    device.identity.kind_str = "orbbec";
    device.identity.hardware_name = device.name;
    device.identity.usb_serial = "ORB001";

    StreamInfo color;
    color.stream_id = "color";
    color.name = "color";
    color.data_kind = DataKind::kFrame;
    color.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 1280, 720, 30});

    StreamInfo depth;
    depth.stream_id = "depth";
    depth.name = "depth";
    depth.data_kind = DataKind::kFrame;
    depth.supported_caps.push_back(ResolvedCaps{1, "y16", 640, 480, 30});

    device.streams.push_back(std::move(color));
    device.streams.push_back(std::move(depth));
    return device;
}

uint16_t start_test_server(RestServer& server) {
    for (uint16_t port = 29481; port < 29491; ++port) {
        if (server.start("127.0.0.1", port)) {
            return port;
        }
    }
    return 0;
}

void seed_stopped_session(const std::string& db_path,
                          const std::string& session_id,
                          const std::string& device_uuid,
                          const std::string& request_name,
                          const std::string& preset_name,
                          const std::string& delivery_name) {
    DeviceStore store(db_path);
    EXPECT_TRUE(store.open());
    EXPECT_TRUE(store.save_session(SessionRow{
        .session_id = session_id,
        .state = "stopped",
        .request_name = request_name,
        .request_device_uuid = device_uuid,
        .request_preset_name = preset_name,
        .request_delivery_name = delivery_name,
        .request_origin = "api",
        .request_overrides_json = "{}",
        .device_uuid = device_uuid,
        .preset_id = "test-preset-id",
        .preset_name = preset_name,
        .delivery_name = delivery_name,
        .host = "localhost",
        .locality = "local",
        .started_at_ms = 0,
        .stopped_at_ms = 1,
        .last_error = "",
    }, {}));
    store.close();
}

}  // namespace

namespace insightos::backend {

DiscoveryResult discover_all() {
    DiscoveryResult result;
    result.devices = g_discovered_devices;
    return result;
}

}  // namespace insightos::backend

namespace {

TEST(alias_updates_devices_api_and_catalog_resolution) {
    g_discovered_devices = {make_camera()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    auto devices_res = client.Get("/api/devices");
    EXPECT_TRUE(devices_res && devices_res->status == 200);

    auto devices_json = nlohmann::json::parse(devices_res->body);
    EXPECT_EQ(devices_json["devices"].size(), 1u);
    EXPECT_EQ(devices_json["devices"][0]["name"], "web-camera");
    EXPECT_EQ(devices_json["devices"][0]["default_name"], "web-camera");

    auto alias_res = client.Put("/api/devices/web-camera/alias/front-camera", "", "text/plain");
    EXPECT_TRUE(alias_res && alias_res->status == 200);

    auto alias_json = nlohmann::json::parse(alias_res->body);
    EXPECT_EQ(alias_json["name"], "front-camera");
    EXPECT_EQ(alias_json["default_name"], "web-camera");
    EXPECT_EQ(alias_json["hardware_name"], "Web Camera");

    devices_res = client.Get("/api/devices");
    EXPECT_TRUE(devices_res && devices_res->status == 200);
    devices_json = nlohmann::json::parse(devices_res->body);
    EXPECT_EQ(devices_json["devices"][0]["name"], "front-camera");
    EXPECT_EQ(devices_json["devices"][0]["default_name"], "web-camera");

    StreamRequest new_alias_request;
    new_alias_request.selector.name = "front-camera";
    new_alias_request.preset_name = "720p_30";
    new_alias_request.delivery_name = "mjpeg";

    auto resolved = mgr.catalog().resolve(new_alias_request, mgr.devices());
    EXPECT_TRUE(std::holds_alternative<ResolvedSession>(resolved));

    StreamRequest old_alias_request = new_alias_request;
    old_alias_request.selector.name = "web-camera";
    resolved = mgr.catalog().resolve(old_alias_request, mgr.devices());
    EXPECT_TRUE(std::holds_alternative<ResolutionError>(resolved));
    EXPECT_EQ(std::get<ResolutionError>(resolved).code, "device_not_found");

    StreamRequest uuid_request;
    uuid_request.selector.device_uuid =
        devices_json["devices"][0]["device_uuid"].get<std::string>();
    uuid_request.preset_name = "720p_30";
    uuid_request.delivery_name = "mjpeg";
    resolved = mgr.catalog().resolve(uuid_request, mgr.devices());
    EXPECT_TRUE(std::holds_alternative<ResolvedSession>(resolved));

    server.stop();
}

TEST(stream_alias_endpoints_update_public_caps_lookup) {
    g_discovered_devices = {make_camera()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    auto devices_res = client.Get("/api/devices");
    EXPECT_TRUE(devices_res && devices_res->status == 200);
    auto devices_json = nlohmann::json::parse(devices_res->body);
    EXPECT_EQ(devices_json["devices"][0]["streams"][0]["stream_id"], "image");
    EXPECT_EQ(devices_json["devices"][0]["streams"][0]["stream_name"], "frame");

    auto caps_res = client.Get("/api/devices/web-camera/streams/frame/caps");
    EXPECT_TRUE(caps_res && caps_res->status == 200);
    auto caps_json = nlohmann::json::parse(caps_res->body);
    EXPECT_EQ(caps_json["stream_id"], "image");
    EXPECT_EQ(caps_json["stream_name"], "frame");

    auto alias_res = client.Put(
        "/api/devices/web-camera/streams/frame/alias/preview", "",
        "text/plain");
    EXPECT_TRUE(alias_res && alias_res->status == 200);
    auto alias_json = nlohmann::json::parse(alias_res->body);
    EXPECT_EQ(alias_json["name"], "web-camera");
    EXPECT_EQ(alias_json["stream"]["stream_id"], "image");
    EXPECT_EQ(alias_json["stream"]["stream_name"], "preview");

    caps_res = client.Get("/api/devices/web-camera/streams/frame/caps");
    EXPECT_TRUE(caps_res && caps_res->status == 404);

    caps_res = client.Get("/api/devices/web-camera/streams/preview/caps");
    EXPECT_TRUE(caps_res && caps_res->status == 200);
    caps_json = nlohmann::json::parse(caps_res->body);
    EXPECT_EQ(caps_json["stream_id"], "image");
    EXPECT_EQ(caps_json["stream_name"], "preview");

    auto clear_res = client.Delete("/api/devices/web-camera/streams/preview/alias");
    EXPECT_TRUE(clear_res && clear_res->status == 200);
    auto clear_json = nlohmann::json::parse(clear_res->body);
    EXPECT_EQ(clear_json["stream"]["stream_id"], "image");
    EXPECT_EQ(clear_json["stream"]["stream_name"], "frame");

    server.stop();
}

TEST(stream_alias_endpoints_validate_errors) {
    g_discovered_devices = {make_orbbec_camera()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    auto conflict_res = client.Put(
        "/api/devices/depth-cam/streams/depth/alias/color", "",
        "text/plain");
    EXPECT_TRUE(conflict_res && conflict_res->status == 409);
    auto conflict_json = nlohmann::json::parse(conflict_res->body);
    EXPECT_EQ(conflict_json["error"], "conflict");

    auto invalid_res = client.Put(
        "/api/devices/depth-cam/streams/depth/alias/---", "",
        "text/plain");
    EXPECT_TRUE(invalid_res && invalid_res->status == 422);
    auto invalid_json = nlohmann::json::parse(invalid_res->body);
    EXPECT_EQ(invalid_json["error"], "invalid_alias");

    auto missing_res = client.Delete("/api/devices/depth-cam/streams/ir/alias");
    EXPECT_TRUE(missing_res && missing_res->status == 404);
    auto missing_json = nlohmann::json::parse(missing_res->body);
    EXPECT_EQ(missing_json["error"], "not_found");

    server.stop();
}

TEST(stream_alias_change_does_not_rename_live_delivery_in_place) {
    g_discovered_devices = {make_test_camera()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());
    mgr.set_ipc_socket_path("/tmp/insightos-rest-test.sock");

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);
    const auto body = R"json({
  "name": "front-camera",
  "preset": "720p_30",
  "delivery": "mjpeg"
})json";

    auto first_res = client.Post("/api/sessions", body, "application/json");
    EXPECT_TRUE(first_res && first_res->status == 200);
    auto first_json = nlohmann::json::parse(first_res->body);
    const auto first_session_id = first_json["session_id"].get<std::string>();
    EXPECT_EQ(first_json["streams"][0]["stream_name"], "frame");

    auto alias_res = client.Put(
        "/api/devices/front-camera/streams/frame/alias/preview", "",
        "text/plain");
    EXPECT_TRUE(alias_res && alias_res->status == 200);

    auto first_get_res =
        client.Get(("/api/sessions/" + first_session_id).c_str());
    EXPECT_TRUE(first_get_res && first_get_res->status == 200);
    auto first_get_json = nlohmann::json::parse(first_get_res->body);
    EXPECT_EQ(first_get_json["streams"][0]["stream_name"], "frame");

    auto second_res = client.Post("/api/sessions", body, "application/json");
    EXPECT_TRUE(second_res && second_res->status == 200);
    auto second_json = nlohmann::json::parse(second_res->body);
    const auto second_session_id = second_json["session_id"].get<std::string>();
    EXPECT_EQ(second_json["streams"][0]["stream_name"], "frame");

    auto stop_first_res = client.Post(
        ("/api/sessions/" + first_session_id + "/stop").c_str(),
        "", "text/plain");
    EXPECT_TRUE(stop_first_res && stop_first_res->status == 200);

    auto stop_second_res = client.Post(
        ("/api/sessions/" + second_session_id + "/stop").c_str(),
        "", "text/plain");
    EXPECT_TRUE(stop_second_res && stop_second_res->status == 200);

    auto third_res = client.Post("/api/sessions", body, "application/json");
    EXPECT_TRUE(third_res && third_res->status == 200);
    auto third_json = nlohmann::json::parse(third_res->body);
    EXPECT_EQ(third_json["streams"][0]["stream_name"], "preview");

    server.stop();
}

TEST(start_failure_persists_last_error_on_stopped_session) {
    const auto db_path = make_temp_db_path();
    const std::string session_id = "persisted-session";
    const std::string device_uuid = "9558a6c2-4ec3-537b-a150-6eeb8faba1d9";

    seed_stopped_session(db_path, session_id, device_uuid, "web-camera",
                         "720p_30", "mjpeg");

    g_discovered_devices.clear();

    {
        SessionManager mgr(db_path);
        EXPECT_TRUE(mgr.initialize());

        RestServer server(mgr, "");
        const auto port = start_test_server(server);
        EXPECT_TRUE(port != 0);

        httplib::Client client("127.0.0.1", port);

        auto start_res = client.Post(("/api/sessions/" + session_id + "/start").c_str(),
                                     "", "text/plain");
        EXPECT_TRUE(start_res);
        EXPECT_EQ(start_res->status, 422);

        auto start_json = nlohmann::json::parse(start_res->body);
        EXPECT_EQ(start_json["error"], "device_not_found");

        auto session_res =
            client.Get(("/api/sessions/" + session_id).c_str());
        EXPECT_TRUE(session_res && session_res->status == 200);

        auto session_json = nlohmann::json::parse(session_res->body);
        EXPECT_EQ(session_json["state"], "stopped");
        EXPECT_EQ(session_json["capture_session_id"], "");
        EXPECT_TRUE(session_json.contains("last_error"));
        EXPECT_EQ(session_json["last_error"],
                  "device_not_found: No device with UUID '" + device_uuid +
                      "' in catalog");

        server.stop();
    }

    DeviceStore store(db_path);
    EXPECT_TRUE(store.open());
    auto row = store.find_session(session_id);
    EXPECT_TRUE(row.has_value());
    EXPECT_EQ(row->state, "stopped");
    EXPECT_EQ(row->last_error,
              "device_not_found: No device with UUID '" + device_uuid +
                  "' in catalog");
    store.close();
}

TEST(post_sessions_rejects_legacy_endpoint_field) {
    g_discovered_devices = {make_camera()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);
    const auto body = R"json({
  "endpoint": "web-camera",
  "preset": "720p_30",
  "delivery": "mjpeg"
})json";

    auto create_res = client.Post("/api/sessions", body, "application/json");
    EXPECT_TRUE(create_res);
    EXPECT_EQ(create_res->status, 400);

    auto create_json = nlohmann::json::parse(create_res->body);
    EXPECT_EQ(create_json["error"], "bad_request");
    EXPECT_EQ(create_json["message"],
              "'preset' and either 'name' or 'device_uuid' are required");

    server.stop();
}

TEST(app_registry_lifecycle_and_source_state_updates) {
    g_discovered_devices = {make_test_camera(), make_test_microphone()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());
    mgr.set_ipc_socket_path("/tmp/insightos-rest-test.sock");

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    auto create_app_res = client.Post("/api/apps", "", "application/json");
    EXPECT_TRUE(create_app_res && create_app_res->status == 200);
    auto app_json = nlohmann::json::parse(create_app_res->body);
    const auto app_id = app_json["app_id"].get<std::string>();
    EXPECT_TRUE(!app_id.empty());
    EXPECT_EQ(app_json["targets"].size(), 0u);
    EXPECT_EQ(app_json["sources"].size(), 0u);

    const auto target_body = R"json({
  "target_name": "frame",
  "target_kind": "video"
})json";
    auto create_target_res = client.Post(
        ("/api/apps/" + app_id + "/targets").c_str(),
        target_body, "application/json");
    EXPECT_TRUE(create_target_res && create_target_res->status == 200);

    auto list_apps_res = client.Get("/api/apps");
    EXPECT_TRUE(list_apps_res && list_apps_res->status == 200);
    auto apps_json = nlohmann::json::parse(list_apps_res->body);
    EXPECT_EQ(apps_json.size(), 1u);
    EXPECT_EQ(apps_json[0]["app_id"], app_id);

    const auto source_body = R"json({
  "input": "insightos://localhost/front-camera/720p_30/mjpeg",
  "target": "frame"
})json";
    auto add_source_res = client.Post(
        ("/api/apps/" + app_id + "/sources").c_str(),
        source_body, "application/json");
    EXPECT_TRUE(add_source_res && add_source_res->status == 200);
    auto source_json = nlohmann::json::parse(add_source_res->body);
    const auto source_id = source_json["source_id"].get<std::string>();
    const auto session_id =
        source_json["session"]["session_id"].get<std::string>();
    EXPECT_EQ(source_json["canonical_uri"],
              "insightos://localhost/front-camera/720p_30/mjpeg");
    EXPECT_EQ(source_json["target"], "frame");
    EXPECT_EQ(source_json["target_kind"], "video");
    EXPECT_EQ(source_json["state"], "active");
    EXPECT_EQ(source_json["bindings"].size(), 1u);
    EXPECT_EQ(source_json["bindings"][0]["role"], "primary");
    EXPECT_EQ(source_json["session"]["name"], "front-camera");
    EXPECT_EQ(source_json["session"]["streams"].size(), 1u);
    EXPECT_EQ(source_json["session"]["streams"][0]["stream_name"], "frame");
    EXPECT_TRUE(source_json["session"]["streams"][0].contains("ipc_descriptor"));

    auto duplicate_res = client.Post(
        ("/api/apps/" + app_id + "/sources").c_str(),
        source_body, "application/json");
    EXPECT_TRUE(duplicate_res && duplicate_res->status == 409);

    const auto remote_body = R"json({
  "input": "insightos://lab-box/front-camera/720p_30/mjpeg",
  "target": "frame"
})json";
    auto remote_res = client.Post(
        ("/api/apps/" + app_id + "/sources").c_str(),
        remote_body, "application/json");
    EXPECT_TRUE(remote_res && remote_res->status == 422);

    auto list_sources_res =
        client.Get(("/api/apps/" + app_id + "/sources").c_str());
    EXPECT_TRUE(list_sources_res && list_sources_res->status == 200);
    auto sources_json = nlohmann::json::parse(list_sources_res->body);
    EXPECT_EQ(sources_json.size(), 1u);
    EXPECT_EQ(sources_json[0]["source_id"], source_id);

    auto stop_res = client.Post(
        ("/api/apps/" + app_id + "/sources/" + source_id + "/stop").c_str(),
        R"json({"last_error":"manual-stop"})json",
        "application/json");
    EXPECT_TRUE(stop_res && stop_res->status == 200);
    auto stopped_json = nlohmann::json::parse(stop_res->body);
    EXPECT_EQ(stopped_json["state"], "stopped");
    EXPECT_EQ(stopped_json["last_error"], "manual-stop");
    EXPECT_EQ(stopped_json["session"]["state"], "stopped");

    auto start_res = client.Post(
        ("/api/apps/" + app_id + "/sources/" + source_id + "/start").c_str(),
        "", "application/json");
    EXPECT_TRUE(start_res && start_res->status == 200);
    auto started_json = nlohmann::json::parse(start_res->body);
    EXPECT_EQ(started_json["state"], "active");
    EXPECT_EQ(started_json["session"]["state"], "active");

    auto bad_stop_body_res = client.Post(
        ("/api/apps/" + app_id + "/sources/" + source_id + "/stop").c_str(),
        R"json({"last_error":7})json",
        "application/json");
    EXPECT_TRUE(bad_stop_body_res && bad_stop_body_res->status == 400);

    auto delete_app_res = client.Delete(("/api/apps/" + app_id).c_str());
    EXPECT_TRUE(delete_app_res && delete_app_res->status == 200);

    auto get_app_res = client.Get(("/api/apps/" + app_id).c_str());
    EXPECT_TRUE(get_app_res && get_app_res->status == 404);

    auto get_session_res = client.Get(("/api/sessions/" + session_id).c_str());
    EXPECT_TRUE(get_session_res && get_session_res->status == 404);

    server.stop();
}

TEST(app_targets_and_sources_persist_across_restart_as_stopped) {
    const auto db_path = make_temp_db_path();
    g_discovered_devices = {make_test_camera()};

    std::string app_id;
    {
        SessionManager mgr(db_path);
        EXPECT_TRUE(mgr.initialize());
        mgr.set_ipc_socket_path("/tmp/insightos-rest-test.sock");

        RestServer server(mgr, "");
        const auto port = start_test_server(server);
        EXPECT_TRUE(port != 0);

        httplib::Client client("127.0.0.1", port);
        auto create_app_res = client.Post("/api/apps", "", "application/json");
        EXPECT_TRUE(create_app_res && create_app_res->status == 200);
        app_id = nlohmann::json::parse(create_app_res->body)["app_id"].get<std::string>();

        auto create_target_res = client.Post(
            ("/api/apps/" + app_id + "/targets").c_str(),
            R"json({"target_name":"yolov5","target_kind":"video"})json",
            "application/json");
        EXPECT_TRUE(create_target_res && create_target_res->status == 200);

        auto add_source_res = client.Post(
            ("/api/apps/" + app_id + "/sources").c_str(),
            R"json({"input":"insightos://localhost/front-camera/720p_30/mjpeg","target":"yolov5"})json",
            "application/json");
        EXPECT_TRUE(add_source_res && add_source_res->status == 200);
        auto source_json = nlohmann::json::parse(add_source_res->body);
        EXPECT_EQ(source_json["state"], "active");

        server.stop();
    }

    {
        SessionManager mgr(db_path);
        EXPECT_TRUE(mgr.initialize());
        mgr.set_ipc_socket_path("/tmp/insightos-rest-test.sock");

        RestServer server(mgr, "");
        const auto port = start_test_server(server);
        EXPECT_TRUE(port != 0);

        httplib::Client client("127.0.0.1", port);
        auto apps_res = client.Get("/api/apps");
        EXPECT_TRUE(apps_res && apps_res->status == 200);
        auto apps_json = nlohmann::json::parse(apps_res->body);
        EXPECT_EQ(apps_json.size(), 1u);
        EXPECT_EQ(apps_json[0]["app_id"], app_id);
        EXPECT_EQ(apps_json[0]["targets"].size(), 1u);
        EXPECT_EQ(apps_json[0]["targets"][0]["target_name"], "yolov5");
        EXPECT_EQ(apps_json[0]["sources"].size(), 1u);
        EXPECT_EQ(apps_json[0]["sources"][0]["target"], "yolov5");
        EXPECT_EQ(apps_json[0]["sources"][0]["state"], "stopped");

        auto sources_res = client.Get(("/api/apps/" + app_id + "/sources").c_str());
        EXPECT_TRUE(sources_res && sources_res->status == 200);
        auto sources_json = nlohmann::json::parse(sources_res->body);
        EXPECT_EQ(sources_json.size(), 1u);
        EXPECT_EQ(sources_json[0]["state"], "stopped");
        const auto source_id = sources_json[0]["source_id"].get<std::string>();

        auto restart_res = client.Post(
            ("/api/apps/" + app_id + "/sources/" + source_id + "/start").c_str(),
            "", "application/json");
        EXPECT_TRUE(restart_res && restart_res->status == 200);
        auto restarted_json = nlohmann::json::parse(restart_res->body);
        EXPECT_EQ(restarted_json["state"], "active");
        EXPECT_EQ(restarted_json["session"]["state"], "active");

        server.stop();
    }
}

TEST(app_sources_require_target_and_known_target) {
    g_discovered_devices = {make_test_camera()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());
    mgr.set_ipc_socket_path("/tmp/insightos-rest-test.sock");

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);
    auto create_app_res = client.Post("/api/apps", "", "application/json");
    EXPECT_TRUE(create_app_res && create_app_res->status == 200);
    auto app_id = nlohmann::json::parse(create_app_res->body)["app_id"].get<std::string>();

    auto missing_target_res = client.Post(
        ("/api/apps/" + app_id + "/sources").c_str(),
        R"json({"input":"insightos://localhost/front-camera/720p_30/mjpeg"})json",
        "application/json");
    EXPECT_TRUE(missing_target_res && missing_target_res->status == 400);

    auto unknown_target_res = client.Post(
        ("/api/apps/" + app_id + "/sources").c_str(),
        R"json({"input":"insightos://localhost/front-camera/720p_30/mjpeg","target":"yolov5"})json",
        "application/json");
    EXPECT_TRUE(unknown_target_res && unknown_target_res->status == 404);

    server.stop();
}

TEST(rgbd_target_rejects_single_stream_camera_source) {
    g_discovered_devices = {make_test_camera()};

    SessionManager mgr(make_temp_db_path());
    EXPECT_TRUE(mgr.initialize());
    mgr.set_ipc_socket_path("/tmp/insightos-rest-test.sock");

    RestServer server(mgr, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);
    auto create_app_res = client.Post("/api/apps", "", "application/json");
    EXPECT_TRUE(create_app_res && create_app_res->status == 200);
    auto app_id = nlohmann::json::parse(create_app_res->body)["app_id"].get<std::string>();

    auto create_target_res = client.Post(
        ("/api/apps/" + app_id + "/targets").c_str(),
        R"json({"target_name":"rgbd-view","target_kind":"rgbd"})json",
        "application/json");
    EXPECT_TRUE(create_target_res && create_target_res->status == 200);

    auto add_source_res = client.Post(
        ("/api/apps/" + app_id + "/sources").c_str(),
        R"json({"input":"insightos://localhost/front-camera/720p_30/mjpeg","target":"rgbd-view"})json",
        "application/json");
    EXPECT_TRUE(add_source_res && add_source_res->status == 422);
    auto error_json = nlohmann::json::parse(add_source_res->body);
    EXPECT_EQ(error_json["error"], "incompatible_target");

    server.stop();
}

}  // namespace

int main() {
    int passed = 0;
    for (const auto& test : tests()) {
        std::cout << "  " << test.name << " ... ";
        test.fn();
        std::cout << "ok\n";
        ++passed;
    }
    std::cout << "\n" << passed << " tests passed.\n";
    return 0;
}
