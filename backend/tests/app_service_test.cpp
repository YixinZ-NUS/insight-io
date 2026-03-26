// role: focused durable app/route/source service tests for the backend.
// revision: 2026-03-26 pr5-review-fixes
// major changes: verifies app CRUD, route guards, exact and grouped source
// binds, grouped-route delete cleanup, restart normalization, and route
// rebind behavior without relying on a fixed Orbbec serial, and covers
// app-delete stop-failure propagation.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/app_service.hpp"
#include "insightio/backend/catalog.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
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

int deny_session_updates(void*,
                         int action_code,
                         const char* arg1,
                         const char*,
                         const char*,
                         const char*) {
    if (action_code == SQLITE_UPDATE && arg1 != nullptr &&
        std::string_view(arg1) == "sessions") {
        return SQLITE_DENY;
    }
    return SQLITE_OK;
}

std::string make_temp_db_path() {
    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("insight-io-app-test-" + std::to_string(::getpid()) +
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

    StreamInfo depth;
    depth.stream_id = "depth";
    depth.name = "depth";
    depth.supported_caps.push_back(ResolvedCaps{0, "y16", 640, 400, 30});

    device.streams.push_back(std::move(color));
    device.streams.push_back(std::move(depth));
    return device;
}

struct Fixture {
    SchemaStore store;
    CatalogService catalog;
    SessionService sessions;
    AppService apps;

    Fixture()
        : store(make_temp_db_path()),
          catalog(store,
                  []() {
                      DiscoveryResult result;
                      result.devices = {make_v4l2_camera(), make_orbbec_device()};
                      return result;
                  },
                  "localhost",
                  "127.0.0.1"),
          sessions(store, "localhost", "127.0.0.1"),
          apps(store, sessions, "localhost", "127.0.0.1") {
        EXPECT_TRUE(store.initialize());
        EXPECT_TRUE(catalog.initialize());
        EXPECT_TRUE(sessions.initialize());
        EXPECT_TRUE(apps.initialize());
    }
};

TEST(uri_backed_exact_source_persists_and_restarts) {
    Fixture fx;

    AppRecord app;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(fx.apps.create_app("Vision Runner",
                                   "",
                                   nullptr,
                                   app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord route;
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "yolov5",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord source;
    EXPECT_TRUE(fx.apps.create_source(app.app_id,
                                      std::optional<std::string>{
                                          "insightos://localhost/web-camera/720p_30"},
                                      std::nullopt,
                                      "yolov5",
                                      false,
                                      source,
                                      error_status,
                                      error_code,
                                      error_message));
    EXPECT_EQ(source.target_resource_name,
              "apps/" + std::to_string(app.app_id) + "/routes/yolov5");
    EXPECT_EQ(source.source.selector, "720p_30");
    EXPECT_EQ(source.state, "active");
    EXPECT_EQ(source.source_session_id, 0);
    EXPECT_TRUE(source.active_session_id > 0);
    const auto original_active_session_id = source.active_session_id;

    AppSourceRecord stopped;
    EXPECT_TRUE(fx.apps.stop_source(app.app_id,
                                    source.source_id,
                                    stopped,
                                    error_status,
                                    error_code,
                                    error_message));
    EXPECT_EQ(stopped.state, "stopped");
    EXPECT_EQ(stopped.active_session_id, 0);

    AppSourceRecord restarted;
    EXPECT_TRUE(fx.apps.start_source(app.app_id,
                                     source.source_id,
                                     restarted,
                                     error_status,
                                     error_code,
                                     error_message));
    EXPECT_EQ(restarted.state, "active");
    EXPECT_TRUE(restarted.active_session_id > 0);
    EXPECT_TRUE(restarted.active_session_id != original_active_session_id);

    SessionService restarted_sessions(fx.store, "localhost", "127.0.0.1");
    EXPECT_TRUE(restarted_sessions.initialize());
    AppService restarted_apps(fx.store, restarted_sessions, "localhost", "127.0.0.1");
    EXPECT_TRUE(restarted_apps.initialize());

    std::vector<AppSourceRecord> listed_sources;
    EXPECT_TRUE(restarted_apps.list_sources(app.app_id,
                                            listed_sources,
                                            error_status,
                                            error_code,
                                            error_message));
    EXPECT_EQ(listed_sources.size(), 1u);
    EXPECT_EQ(listed_sources[0].state, "stopped");
    EXPECT_EQ(listed_sources[0].active_session_id, 0);
}

TEST(route_guard_rejects_ambiguous_target_roots) {
    Fixture fx;

    AppRecord app;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(fx.apps.create_app("rgbd-app",
                                   "",
                                   nullptr,
                                   app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord created;
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "orbbec/color",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     created,
                                     error_status,
                                     error_code,
                                     error_message));
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "orbbec/depth",
                                     nlohmann::json{{"media", "depth"}},
                                     nullptr,
                                     created,
                                     error_status,
                                     error_code,
                                     error_message));

    EXPECT_TRUE(!fx.apps.create_route(app.app_id,
                                      "orbbec",
                                      nullptr,
                                      nullptr,
                                      created,
                                      error_status,
                                      error_code,
                                      error_message));
    EXPECT_EQ(error_status, 409);
    EXPECT_EQ(error_code, "ambiguous_route_target");
}

TEST(session_backed_exact_and_grouped_binds_use_existing_direct_sessions) {
    Fixture fx;

    int error_status = 0;
    std::string error_code;
    std::string error_message;

    SessionRecord exact_session;
    EXPECT_TRUE(fx.sessions.create_direct_session(
        "insightos://localhost/web-camera/720p_30",
        false,
        exact_session,
        error_status,
        error_code,
        error_message));

    AppRecord exact_app;
    EXPECT_TRUE(fx.apps.create_app("exact-app",
                                   "",
                                   nullptr,
                                   exact_app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord route;
    EXPECT_TRUE(fx.apps.create_route(exact_app.app_id,
                                     "yolov5",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord attached;
    EXPECT_TRUE(fx.apps.create_source(exact_app.app_id,
                                      std::nullopt,
                                      std::optional<std::int64_t>{exact_session.session_id},
                                      "yolov5",
                                      false,
                                      attached,
                                      error_status,
                                      error_code,
                                      error_message));
    EXPECT_EQ(attached.source_session_id, exact_session.session_id);
    EXPECT_EQ(attached.active_session_id, exact_session.session_id);
    EXPECT_EQ(attached.state, "active");

    AppSourceRecord stopped;
    EXPECT_TRUE(fx.apps.stop_source(exact_app.app_id,
                                    attached.source_id,
                                    stopped,
                                    error_status,
                                    error_code,
                                    error_message));
    EXPECT_EQ(stopped.state, "stopped");
    EXPECT_EQ(stopped.active_session_id, 0);
    const auto exact_after_stop = fx.sessions.get_session(exact_session.session_id);
    EXPECT_TRUE(exact_after_stop.has_value());
    EXPECT_EQ(exact_after_stop->state, "active");

    SessionRecord grouped_session;
    EXPECT_TRUE(fx.sessions.create_direct_session(
        "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
        false,
        grouped_session,
        error_status,
        error_code,
        error_message));

    AppRecord grouped_app;
    EXPECT_TRUE(fx.apps.create_app("grouped-app",
                                   "",
                                   nullptr,
                                   grouped_app,
                                   error_status,
                                   error_code,
                                   error_message));

    EXPECT_TRUE(fx.apps.create_route(grouped_app.app_id,
                                     "orbbec/color",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));
    EXPECT_TRUE(fx.apps.create_route(grouped_app.app_id,
                                     "orbbec/depth",
                                     nlohmann::json{{"media", "depth"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord grouped_source;
    EXPECT_TRUE(fx.apps.create_source(grouped_app.app_id,
                                      std::nullopt,
                                      std::optional<std::int64_t>{grouped_session.session_id},
                                      "orbbec",
                                      false,
                                      grouped_source,
                                      error_status,
                                      error_code,
                                      error_message));
    EXPECT_EQ(grouped_source.source_session_id, grouped_session.session_id);
    EXPECT_EQ(grouped_source.active_session_id, grouped_session.session_id);
    EXPECT_TRUE(grouped_source.route_id == 0);
    EXPECT_TRUE(grouped_source.target_resource_name.empty());
    EXPECT_EQ(grouped_source.resolved_members_json.size(), 2u);
}

TEST(delete_grouped_member_route_removes_grouped_bind_and_app_owned_session) {
    Fixture fx;

    AppRecord app;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(fx.apps.create_app("grouped-delete-app",
                                   "",
                                   nullptr,
                                   app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord route;
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "orbbec/color",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "orbbec/depth",
                                     nlohmann::json{{"media", "depth"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord source;
    EXPECT_TRUE(fx.apps.create_source(app.app_id,
                                      std::optional<std::string>{
                                          "insightos://localhost/desk-rgbd/orbbec/preset/480p_30"},
                                      std::nullopt,
                                      "orbbec",
                                      false,
                                      source,
                                      error_status,
                                      error_code,
                                      error_message));
    EXPECT_TRUE(source.active_session_id > 0);
    const auto grouped_session_id = source.active_session_id;

    EXPECT_TRUE(fx.apps.delete_route(app.app_id,
                                     "orbbec/depth",
                                     error_status,
                                     error_code,
                                     error_message));

    std::vector<AppSourceRecord> sources;
    EXPECT_TRUE(fx.apps.list_sources(app.app_id,
                                     sources,
                                     error_status,
                                     error_code,
                                     error_message));
    EXPECT_EQ(sources.size(), 0u);

    std::vector<RouteRecord> routes;
    EXPECT_TRUE(fx.apps.list_routes(app.app_id,
                                    routes,
                                    error_status,
                                    error_code,
                                    error_message));
    EXPECT_EQ(routes.size(), 1u);
    EXPECT_EQ(routes[0].route_name, "orbbec/color");

    const auto deleted_session = fx.sessions.get_session(grouped_session_id);
    EXPECT_TRUE(!deleted_session.has_value());
}

TEST(rebind_replaces_app_owned_runtime_session) {
    Fixture fx;

    AppRecord app;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(fx.apps.create_app("rebind-app",
                                   "",
                                   nullptr,
                                   app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord route;
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "yolov5",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord source;
    EXPECT_TRUE(fx.apps.create_source(app.app_id,
                                      std::optional<std::string>{
                                          "insightos://localhost/web-camera/720p_30"},
                                      std::nullopt,
                                      "yolov5",
                                      false,
                                      source,
                                      error_status,
                                      error_code,
                                      error_message));
    const auto old_session_id = source.active_session_id;

    AppSourceRecord rebound;
    EXPECT_TRUE(fx.apps.rebind_source(app.app_id,
                                      source.source_id,
                                      std::optional<std::string>{
                                          "insightos://localhost/web-camera/1080p_30"},
                                      std::nullopt,
                                      false,
                                      rebound,
                                      error_status,
                                      error_code,
                                      error_message));
    EXPECT_EQ(rebound.source.selector, "1080p_30");
    EXPECT_TRUE(rebound.active_session_id > 0);
    EXPECT_TRUE(rebound.active_session_id != old_session_id);

    const auto old_session = fx.sessions.get_session(old_session_id);
    EXPECT_TRUE(old_session.has_value());
    EXPECT_EQ(old_session->state, "stopped");
}

TEST(delete_app_propagates_stop_session_failures) {
    Fixture fx;

    AppRecord app;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(fx.apps.create_app("delete-app-stop-failure",
                                   "",
                                   nullptr,
                                   app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord route;
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "yolov5",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord source;
    EXPECT_TRUE(fx.apps.create_source(app.app_id,
                                      std::optional<std::string>{
                                          "insightos://localhost/web-camera/720p_30"},
                                      std::nullopt,
                                      "yolov5",
                                      false,
                                      source,
                                      error_status,
                                      error_code,
                                      error_message));
    EXPECT_TRUE(source.active_session_id > 0);

    EXPECT_EQ(sqlite3_set_authorizer(fx.store.db(), deny_session_updates, nullptr),
              SQLITE_OK);
    EXPECT_TRUE(!fx.apps.delete_app(app.app_id,
                                    error_status,
                                    error_code,
                                    error_message));
    EXPECT_EQ(sqlite3_set_authorizer(fx.store.db(), nullptr, nullptr), SQLITE_OK);

    EXPECT_EQ(error_status, 500);
    EXPECT_EQ(error_code, "internal");
    EXPECT_TRUE(!error_message.empty());
    EXPECT_TRUE(fx.apps.get_app(app.app_id).has_value());
}

TEST(route_expectation_rejects_incompatible_exact_source) {
    Fixture fx;

    AppRecord app;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(fx.apps.create_app("depth-only",
                                   "",
                                   nullptr,
                                   app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord route;
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "orbbec/depth",
                                     nlohmann::json{{"media", "depth"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord source;
    EXPECT_TRUE(!fx.apps.create_source(app.app_id,
                                       std::optional<std::string>{
                                           "insightos://localhost/web-camera/720p_30"},
                                       std::nullopt,
                                       "orbbec/depth",
                                       false,
                                       source,
                                       error_status,
                                       error_code,
                                       error_message));
    EXPECT_EQ(error_status, 422);
    EXPECT_EQ(error_code, "route_expectation_mismatch");
}

TEST(uri_backed_source_rejects_non_local_uri_host) {
    Fixture fx;

    AppRecord app;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(fx.apps.create_app("host-check",
                                   "",
                                   nullptr,
                                   app,
                                   error_status,
                                   error_code,
                                   error_message));

    RouteRecord route;
    EXPECT_TRUE(fx.apps.create_route(app.app_id,
                                     "cam",
                                     nlohmann::json{{"media", "video"}},
                                     nullptr,
                                     route,
                                     error_status,
                                     error_code,
                                     error_message));

    AppSourceRecord source;
    EXPECT_TRUE(!fx.apps.create_source(app.app_id,
                                       std::optional<std::string>{
                                           "insightos://not-local/web-camera/720p_30"},
                                       std::nullopt,
                                       "cam",
                                       false,
                                       source,
                                       error_status,
                                       error_code,
                                       error_message));
    EXPECT_EQ(error_status, 422);
    EXPECT_EQ(error_code, "invalid_input");
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "app_service_test: " << tests().size()
              << " test(s) passed\n";
    return 0;
}
