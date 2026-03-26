// role: focused REST tests for the standalone backend slices.
// revision: 2026-03-26 direct-session-slice
// major changes: verifies catalog, direct-session, and runtime-status
// behavior on the SQLite-backed HTTP surface.

#include "insightio/backend/catalog.hpp"
#include "insightio/backend/rest_server.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
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

#define EXPECT_TRUE(value)                                                 \
    do {                                                                   \
        if (!(value)) {                                                    \
            std::cerr << "FAIL at " << __FILE__ << ":" << __LINE__         \
                      << "\nexpected true\n";                              \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define EXPECT_EQ(actual, expected)                                        \
    do {                                                                   \
        if ((actual) != (expected)) {                                      \
            std::cerr << "FAIL at " << __FILE__ << ":" << __LINE__         \
                      << "\nexpected: " << (expected)                      \
                      << "\ngot:      " << (actual) << "\n";               \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

using namespace insightio::backend;

std::string make_temp_db_path() {
    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("insight-io-rest-test-" + std::to_string(::getpid()) +
                       "-" + std::to_string(counter++) + ".sqlite3");
    return path.string();
}

DeviceInfo make_v4l2_camera() {
    DeviceInfo device;
    device.uri = "v4l2:/dev/video0";
    device.kind = DeviceKind::kV4l2;
    device.name = "Web Camera";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "video0";
    device.identity.kind_str = "v4l2";
    device.identity.hardware_name = device.name;
    device.identity.usb_vendor_id = "5843";
    device.identity.usb_product_id = "d527";
    device.identity.usb_serial = "SN00009";

    StreamInfo stream;
    stream.stream_id = "image";
    stream.name = "frame";
    stream.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 1280, 720, 30});
    stream.supported_caps.push_back(ResolvedCaps{1, "mjpeg", 1920, 1080, 30});
    device.streams.push_back(std::move(stream));
    return device;
}

DeviceInfo make_orbbec_device() {
    DeviceInfo device;
    device.uri = "orbbec://AY27552002M";
    device.kind = DeviceKind::kOrbbec;
    device.name = "Desk RGBD";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "AY27552002M";
    device.identity.kind_str = "orbbec";
    device.identity.hardware_name = device.name;
    device.identity.usb_vendor_id = "2bc5";
    device.identity.usb_product_id = "0511";
    device.identity.usb_serial = "AY27552002M";

    StreamInfo color;
    color.stream_id = "color";
    color.name = "color";
    color.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 640, 480, 30});
    color.supported_caps.push_back(ResolvedCaps{1, "mjpeg", 1280, 720, 30});

    StreamInfo depth;
    depth.stream_id = "depth";
    depth.name = "depth";
    depth.supported_caps.push_back(ResolvedCaps{0, "y16", 640, 400, 30});
    depth.supported_caps.push_back(ResolvedCaps{1, "y16", 1280, 800, 30});

    device.streams.push_back(std::move(color));
    device.streams.push_back(std::move(depth));
    return device;
}

uint16_t start_test_server(RestServer& server) {
    for (uint16_t port = 29680; port < 29690; ++port) {
        if (server.start("127.0.0.1", port)) {
            return port;
        }
    }
    return 0;
}

TEST(devices_endpoint_lists_rtsp_and_grouped_orbbec_entries) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_v4l2_camera(), make_orbbec_device()};
            return result;
        },
        "localhost",
        "127.0.0.1");
    EXPECT_TRUE(catalog.initialize());
    SessionService sessions(store, "localhost", "127.0.0.1");
    EXPECT_TRUE(sessions.initialize());

    RestServer server(store, catalog, sessions, "/tmp/frontend");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);
    const auto response = client.Get("/api/devices");
    EXPECT_TRUE(response);
    EXPECT_EQ(response->status, 200);

    const auto json = nlohmann::json::parse(response->body);
    EXPECT_EQ(json.at("devices").size(), 2u);

    const auto webcam = std::find_if(json.at("devices").begin(),
                                     json.at("devices").end(),
                                     [](const nlohmann::json& device) {
                                         return device.at("public_name") == "web-camera";
                                     });
    EXPECT_TRUE(webcam != json.at("devices").end());
    EXPECT_TRUE(!(*webcam).at("sources")[0].contains("selector_key"));
    EXPECT_EQ((*webcam).at("sources")[0].at("publications_json")
                  .at("rtsp")
                  .at("url")
                  .get<std::string>(),
              "rtsp://127.0.0.1/web-camera/1080p_30");

    const auto rgbd = std::find_if(json.at("devices").begin(),
                                   json.at("devices").end(),
                                   [](const nlohmann::json& device) {
                                       return device.at("public_name") == "desk-rgbd";
                                   });
    EXPECT_TRUE(rgbd != json.at("devices").end());

    std::set<std::string> selectors;
    for (const auto& source : (*rgbd).at("sources")) {
        selectors.insert(source.at("selector").get<std::string>());
    }
    EXPECT_TRUE(selectors.contains("orbbec/color/480p_30"));
    EXPECT_TRUE(selectors.contains("orbbec/depth/400p_30"));
    EXPECT_TRUE(selectors.contains("orbbec/depth/480p_30"));
    EXPECT_TRUE(selectors.contains("orbbec/preset/480p_30"));

    server.stop();
}

TEST(alias_endpoint_updates_public_name_and_derived_uris) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_v4l2_camera()};
            return result;
        });
    EXPECT_TRUE(catalog.initialize());
    SessionService sessions(store);
    EXPECT_TRUE(sessions.initialize());

    RestServer server(store, catalog, sessions, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);
    const auto response = client.Post("/api/devices/web-camera/alias",
                                      R"({"public_name":"front-camera"})",
                                      "application/json");
    EXPECT_TRUE(response);
    EXPECT_EQ(response->status, 200);

    const auto json = nlohmann::json::parse(response->body);
    EXPECT_EQ(json.at("public_name").get<std::string>(), "front-camera");
    EXPECT_EQ(json.at("sources")[0].at("uri").get<std::string>(),
              "insightos://localhost/front-camera/1080p_30");
    EXPECT_EQ(json.at("sources")[0].at("publications_json")
                  .at("rtsp")
                  .at("url")
                  .get<std::string>(),
              "rtsp://127.0.0.1/front-camera/1080p_30");

    server.stop();
}

TEST(session_endpoints_cover_lifecycle_and_status) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_v4l2_camera()};
            return result;
        },
        "localhost",
        "127.0.0.1");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1");
    EXPECT_TRUE(sessions.initialize());

    RestServer server(store, catalog, sessions, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    const auto create = client.Post("/api/sessions",
                                    R"({"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":true})",
                                    "application/json");
    EXPECT_TRUE(create);
    EXPECT_EQ(create->status, 201);

    const auto created_json = nlohmann::json::parse(create->body);
    EXPECT_EQ(created_json.at("state").get<std::string>(), "active");
    EXPECT_TRUE(created_json.at("resolved_exact_stream_id").get<std::int64_t>() > 0);
    EXPECT_EQ(created_json.at("resolved_source").at("selector").get<std::string>(), "720p_30");
    EXPECT_EQ(created_json.at("rtsp_url").get<std::string>(),
              "rtsp://127.0.0.1/web-camera/720p_30");

    const auto session_id = created_json.at("session_id").get<std::int64_t>();

    const auto list = client.Get("/api/sessions");
    EXPECT_TRUE(list);
    EXPECT_EQ(list->status, 200);
    const auto list_json = nlohmann::json::parse(list->body);
    EXPECT_EQ(list_json.at("sessions").size(), 1u);

    const auto inspect = client.Get(("/api/sessions/" + std::to_string(session_id)).c_str());
    EXPECT_TRUE(inspect);
    EXPECT_EQ(inspect->status, 200);
    const auto inspect_json = nlohmann::json::parse(inspect->body);
    EXPECT_EQ(inspect_json.at("resolved_source").at("uri").get<std::string>(),
              "insightos://localhost/web-camera/720p_30");

    const auto status = client.Get("/api/status");
    EXPECT_TRUE(status);
    EXPECT_EQ(status->status, 200);
    const auto status_json = nlohmann::json::parse(status->body);
    EXPECT_EQ(status_json.at("active_sessions").get<int>(), 1);
    EXPECT_EQ(status_json.at("total_sessions").get<int>(), 1);

    const auto stop = client.Post(("/api/sessions/" + std::to_string(session_id) + "/stop").c_str(),
                                  "",
                                  "application/json");
    EXPECT_TRUE(stop);
    EXPECT_EQ(stop->status, 200);
    EXPECT_EQ(nlohmann::json::parse(stop->body).at("state").get<std::string>(), "stopped");

    const auto start = client.Post(("/api/sessions/" + std::to_string(session_id) + "/start").c_str(),
                                   "",
                                   "application/json");
    EXPECT_TRUE(start);
    EXPECT_EQ(start->status, 200);
    EXPECT_EQ(nlohmann::json::parse(start->body).at("state").get<std::string>(), "active");

    const auto stop_again =
        client.Post(("/api/sessions/" + std::to_string(session_id) + "/stop").c_str(),
                    "",
                    "application/json");
    EXPECT_TRUE(stop_again);
    EXPECT_EQ(stop_again->status, 200);

    const auto destroy = client.Delete(("/api/sessions/" + std::to_string(session_id)).c_str());
    EXPECT_TRUE(destroy);
    EXPECT_EQ(destroy->status, 204);

    const auto missing = client.Get(("/api/sessions/" + std::to_string(session_id)).c_str());
    EXPECT_TRUE(missing);
    EXPECT_EQ(missing->status, 404);

    server.stop();
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "rest_server_test: " << tests().size() << " test(s) passed\n";
    return 0;
}
