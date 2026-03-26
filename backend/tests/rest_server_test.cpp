// role: focused REST tests for the standalone backend slices.
// revision: 2026-03-26 task6-serving-runtime-reuse
// major changes: verifies catalog, direct-session, app/route/source,
// grouped-route delete cleanup, and runtime-status plus serving-runtime reuse
// behavior on the SQLite-backed HTTP surface without relying on a fixed
// Orbbec serial, while covering route JSON shape and path-id overflow
// handling.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/app_service.hpp"
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
    device.uri = "orbbec://ORB001";
    device.kind = DeviceKind::kOrbbec;
    device.name = "Desk RGBD";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "ORB001";
    device.identity.kind_str = "orbbec";
    device.identity.hardware_name = device.name;
    device.identity.usb_vendor_id = "2bc5";
    device.identity.usb_product_id = "0511";
    device.identity.usb_serial = "ORB001";

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
    AppService apps(store, sessions, "localhost", "127.0.0.1");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "/tmp/frontend");
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
    AppService apps(store, sessions);
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
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
    AppService apps(store, sessions, "localhost", "127.0.0.1");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    const auto create = client.Post("/api/sessions",
                                    R"({"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":false})",
                                    "application/json");
    EXPECT_TRUE(create);
    EXPECT_EQ(create->status, 201);

    const auto created_json = nlohmann::json::parse(create->body);
    EXPECT_EQ(created_json.at("state").get<std::string>(), "active");
    EXPECT_TRUE(created_json.at("resolved_exact_stream_id").get<std::int64_t>() > 0);
    EXPECT_EQ(created_json.at("resolved_source").at("selector").get<std::string>(), "720p_30");
    EXPECT_TRUE(created_json.contains("serving_runtime"));
    EXPECT_EQ(created_json.at("serving_runtime").at("consumer_count").get<int>(), 1);
    EXPECT_TRUE(!created_json.contains("rtsp_url"));

    const auto session_id = created_json.at("session_id").get<std::int64_t>();

    const auto second_create = client.Post(
        "/api/sessions",
        R"({"input":"insightos://localhost/web-camera/720p_30","rtsp_enabled":true})",
        "application/json");
    EXPECT_TRUE(second_create);
    EXPECT_EQ(second_create->status, 201);
    const auto second_json = nlohmann::json::parse(second_create->body);
    EXPECT_EQ(second_json.at("rtsp_url").get<std::string>(),
              "rtsp://127.0.0.1/web-camera/720p_30");
    EXPECT_TRUE(second_json.at("serving_runtime").at("shared").get<bool>());
    EXPECT_EQ(second_json.at("serving_runtime").at("consumer_count").get<int>(), 2);
    const auto second_session_id = second_json.at("session_id").get<std::int64_t>();

    const auto list = client.Get("/api/sessions");
    EXPECT_TRUE(list);
    EXPECT_EQ(list->status, 200);
    const auto list_json = nlohmann::json::parse(list->body);
    EXPECT_EQ(list_json.at("sessions").size(), 2u);

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
    EXPECT_EQ(status_json.at("active_sessions").get<int>(), 2);
    EXPECT_EQ(status_json.at("total_sessions").get<int>(), 2);
    EXPECT_EQ(status_json.at("total_serving_runtimes").get<int>(), 1);
    EXPECT_EQ(status_json.at("serving_runtimes").size(), 1u);
    EXPECT_EQ(status_json.at("serving_runtimes")[0].at("consumer_count").get<int>(), 2);
    EXPECT_TRUE(status_json.at("serving_runtimes")[0].at("rtsp_enabled").get<bool>());

    const auto stop_second =
        client.Post(("/api/sessions/" + std::to_string(second_session_id) + "/stop").c_str(),
                    "",
                    "application/json");
    EXPECT_TRUE(stop_second);
    EXPECT_EQ(stop_second->status, 200);

    const auto destroy_second =
        client.Delete(("/api/sessions/" + std::to_string(second_session_id)).c_str());
    EXPECT_TRUE(destroy_second);
    EXPECT_EQ(destroy_second->status, 204);

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

TEST(app_endpoints_cover_exact_and_grouped_bind_flow) {
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
    AppService apps(store, sessions, "localhost", "127.0.0.1");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    const auto create_app = client.Post("/api/apps",
                                        R"({"name":"vision-runner"})",
                                        "application/json");
    EXPECT_TRUE(create_app);
    EXPECT_EQ(create_app->status, 201);
    const auto app_json = nlohmann::json::parse(create_app->body);
    const auto app_id = app_json.at("app_id").get<std::int64_t>();

    const auto create_route = client.Post(
        ("/api/apps/" + std::to_string(app_id) + "/routes").c_str(),
        R"({"route_name":"yolov5","expect":{"media":"video"}})",
        "application/json");
    EXPECT_TRUE(create_route);
    EXPECT_EQ(create_route->status, 201);
    const auto created_route_json = nlohmann::json::parse(create_route->body);
    EXPECT_TRUE(created_route_json.contains("expect"));
    EXPECT_TRUE(!created_route_json.contains("expect_json"));
    EXPECT_EQ(created_route_json.at("expect").at("media").get<std::string>(), "video");

    const auto bind_exact = client.Post(
        ("/api/apps/" + std::to_string(app_id) + "/sources").c_str(),
        R"({"input":"insightos://localhost/web-camera/720p_30","target":"yolov5"})",
        "application/json");
    EXPECT_TRUE(bind_exact);
    EXPECT_EQ(bind_exact->status, 201);
    const auto exact_json = nlohmann::json::parse(bind_exact->body);
    EXPECT_EQ(exact_json.at("target_resource_name").get<std::string>(),
              "apps/" + std::to_string(app_id) + "/routes/yolov5");
    const auto source_id = exact_json.at("source_id").get<std::int64_t>();
    const auto first_session_id = exact_json.at("active_session_id").get<std::int64_t>();

    const auto stop = client.Post(
        ("/api/apps/" + std::to_string(app_id) + "/sources/" +
         std::to_string(source_id) + "/stop")
            .c_str(),
        "",
        "application/json");
    EXPECT_TRUE(stop);
    EXPECT_EQ(stop->status, 200);
    EXPECT_EQ(nlohmann::json::parse(stop->body).at("state").get<std::string>(), "stopped");

    const auto start = client.Post(
        ("/api/apps/" + std::to_string(app_id) + "/sources/" +
         std::to_string(source_id) + "/start")
            .c_str(),
        "",
        "application/json");
    EXPECT_TRUE(start);
    EXPECT_EQ(start->status, 200);
    const auto restarted_source = nlohmann::json::parse(start->body);
    EXPECT_EQ(restarted_source.at("state").get<std::string>(), "active");
    EXPECT_TRUE(restarted_source.at("active_session_id").get<std::int64_t>() != first_session_id);

    const auto grouped_app = client.Post("/api/apps",
                                         R"({"name":"rgbd-runner"})",
                                         "application/json");
    EXPECT_TRUE(grouped_app);
    EXPECT_EQ(grouped_app->status, 201);
    const auto grouped_app_id =
        nlohmann::json::parse(grouped_app->body).at("app_id").get<std::int64_t>();

    EXPECT_TRUE(client.Post(
        ("/api/apps/" + std::to_string(grouped_app_id) + "/routes").c_str(),
        R"({"route_name":"orbbec/color","expect":{"media":"video"}})",
        "application/json"));
    EXPECT_TRUE(client.Post(
        ("/api/apps/" + std::to_string(grouped_app_id) + "/routes").c_str(),
        R"({"route_name":"orbbec/depth","expect":{"media":"depth"}})",
        "application/json"));

    const auto create_grouped_session = client.Post(
        "/api/sessions",
        R"({"input":"insightos://localhost/desk-rgbd/orbbec/preset/480p_30","rtsp_enabled":false})",
        "application/json");
    EXPECT_TRUE(create_grouped_session);
    EXPECT_EQ(create_grouped_session->status, 201);
    const auto grouped_session_id =
        nlohmann::json::parse(create_grouped_session->body).at("session_id").get<std::int64_t>();

    const auto grouped_bind = client.Post(
        ("/api/apps/" + std::to_string(grouped_app_id) + "/sources").c_str(),
        (std::string{"{\"session_id\":"} + std::to_string(grouped_session_id) +
         ",\"target\":\"orbbec\"}")
            .c_str(),
        "application/json");
    EXPECT_TRUE(grouped_bind);
    EXPECT_EQ(grouped_bind->status, 201);
    const auto grouped_json = nlohmann::json::parse(grouped_bind->body);
    EXPECT_TRUE(!grouped_json.contains("target_resource_name"));
    EXPECT_EQ(grouped_json.at("resolved_members_json").size(), 2u);

    const auto list_sources =
        client.Get(("/api/apps/" + std::to_string(grouped_app_id) + "/sources").c_str());
    EXPECT_TRUE(list_sources);
    EXPECT_EQ(list_sources->status, 200);
    EXPECT_EQ(nlohmann::json::parse(list_sources->body).at("sources").size(), 1u);

    const auto list_routes =
        client.Get(("/api/apps/" + std::to_string(app_id) + "/routes").c_str());
    EXPECT_TRUE(list_routes);
    EXPECT_EQ(list_routes->status, 200);
    const auto listed_routes = nlohmann::json::parse(list_routes->body).at("routes");
    EXPECT_EQ(listed_routes.size(), 1u);
    EXPECT_TRUE(listed_routes[0].contains("expect"));
    EXPECT_TRUE(!listed_routes[0].contains("expect_json"));
    EXPECT_EQ(listed_routes[0].at("expect").at("media").get<std::string>(), "video");

    server.stop();
}

TEST(oversized_path_ids_return_bad_request) {
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
    AppService apps(store, sessions, "localhost", "127.0.0.1");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    const auto huge_app = client.Get("/api/apps/9223372036854775808");
    EXPECT_TRUE(huge_app);
    EXPECT_EQ(huge_app->status, 400);
    EXPECT_EQ(nlohmann::json::parse(huge_app->body).at("error").get<std::string>(),
              "bad_request");

    const auto huge_session = client.Get("/api/sessions/9223372036854775808");
    EXPECT_TRUE(huge_session);
    EXPECT_EQ(huge_session->status, 400);
    EXPECT_EQ(nlohmann::json::parse(huge_session->body).at("error").get<std::string>(),
              "bad_request");

    server.stop();
}

TEST(delete_grouped_member_route_cleans_up_grouped_binding) {
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
    AppService apps(store, sessions, "localhost", "127.0.0.1");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);

    const auto create_app = client.Post("/api/apps",
                                        R"({"name":"grouped-delete-rest"})",
                                        "application/json");
    EXPECT_TRUE(create_app);
    EXPECT_EQ(create_app->status, 201);
    const auto app_id =
        nlohmann::json::parse(create_app->body).at("app_id").get<std::int64_t>();

    const auto create_color = client.Post(
        ("/api/apps/" + std::to_string(app_id) + "/routes").c_str(),
        R"({"route_name":"orbbec/color","expect":{"media":"video"}})",
        "application/json");
    EXPECT_TRUE(create_color);
    EXPECT_EQ(create_color->status, 201);

    const auto create_depth = client.Post(
        ("/api/apps/" + std::to_string(app_id) + "/routes").c_str(),
        R"({"route_name":"orbbec/depth","expect":{"media":"depth"}})",
        "application/json");
    EXPECT_TRUE(create_depth);
    EXPECT_EQ(create_depth->status, 201);

    const auto bind_grouped = client.Post(
        ("/api/apps/" + std::to_string(app_id) + "/sources").c_str(),
        R"({"input":"insightos://localhost/desk-rgbd/orbbec/preset/480p_30","target":"orbbec"})",
        "application/json");
    EXPECT_TRUE(bind_grouped);
    EXPECT_EQ(bind_grouped->status, 201);
    const auto grouped_json = nlohmann::json::parse(bind_grouped->body);
    const auto grouped_session_id =
        grouped_json.at("active_session_id").get<std::int64_t>();

    const auto destroy_route = client.Delete(
        ("/api/apps/" + std::to_string(app_id) + "/routes/orbbec%2Fdepth").c_str());
    EXPECT_TRUE(destroy_route);
    EXPECT_EQ(destroy_route->status, 204);

    const auto list_sources =
        client.Get(("/api/apps/" + std::to_string(app_id) + "/sources").c_str());
    EXPECT_TRUE(list_sources);
    EXPECT_EQ(list_sources->status, 200);
    EXPECT_EQ(nlohmann::json::parse(list_sources->body).at("sources").size(), 0u);

    const auto grouped_session =
        client.Get(("/api/sessions/" + std::to_string(grouped_session_id)).c_str());
    EXPECT_TRUE(grouped_session);
    EXPECT_EQ(grouped_session->status, 404);

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
