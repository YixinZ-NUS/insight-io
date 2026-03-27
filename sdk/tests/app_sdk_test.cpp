// role: focused task-9 SDK callback integration tests.
// revision: 2026-03-27 task9-runtime-review-fixes
// major changes: verifies startup binds, grouped callback fanout, late REST
// injection, idle-until-bind behavior, runtime rebind, same-URI fanout,
// omitted-name derivation through `bind_from_cli()`, and exact/grouped
// session-backed attach using synthetic devices and the checked-in backend
// runtime. See docs/past-tasks.md.

#include "insightos/app.hpp"

#include "insightio/backend/app_service.hpp"
#include "insightio/backend/catalog.hpp"
#include "insightio/backend/rest_server.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <unistd.h>

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
                      ("insight-io-sdk-test-" + std::to_string(::getpid()) +
                       "-" + std::to_string(counter++) + ".sqlite3");
    return path.string();
}

DeviceInfo make_test_v4l2_camera() {
    DeviceInfo device;
    device.uri = "test:front-camera";
    device.kind = DeviceKind::kV4l2;
    device.name = "Front Camera";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "front-camera";
    device.identity.kind_str = "v4l2";
    device.identity.hardware_name = device.name;

    StreamInfo stream;
    stream.stream_id = "image";
    stream.name = "frame";
    stream.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 1280, 720, 30});
    device.streams.push_back(std::move(stream));
    return device;
}

DeviceInfo make_test_orbbec_device() {
    DeviceInfo device;
    device.uri = "test:desk-rgbd";
    device.kind = DeviceKind::kOrbbec;
    device.name = "Desk RGBD";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "desk-rgbd";
    device.identity.kind_str = "orbbec";
    device.identity.hardware_name = device.name;

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

DeviceInfo make_test_pipewire_device() {
    DeviceInfo device;
    device.uri = "test:desk-audio";
    device.kind = DeviceKind::kPipeWire;
    device.name = "Desk Audio";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "desk-audio";
    device.identity.kind_str = "pipewire";
    device.identity.hardware_name = device.name;

    StreamInfo stream;
    stream.stream_id = "audio";
    stream.name = "audio";
    stream.supported_caps.push_back(ResolvedCaps{0, "s16le", 48000, 1, 0});
    stream.supported_caps.push_back(ResolvedCaps{1, "s16le", 48000, 2, 0});

    device.streams.push_back(std::move(stream));
    return device;
}

uint16_t start_test_server(RestServer& server) {
    for (uint16_t port = 29840; port < 29870; ++port) {
        if (server.start("127.0.0.1", port)) {
            return port;
        }
    }
    return 0;
}

template <typename Predicate>
bool wait_until(Predicate&& predicate,
                std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

httplib::Result post_json(httplib::Client& client,
                          const std::string& path,
                          const nlohmann::json& body) {
    return client.Post(path.c_str(), body.dump(), "application/json");
}

std::vector<char*> make_argv(std::vector<std::string>& storage) {
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& value : storage) {
        argv.push_back(value.data());
    }
    return argv;
}

TEST(startup_exact_bind_delivers_route_callbacks) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int frames{0};
    std::atomic_int caps_seen{0};

    insightos::App app({
        .name = "sdk-exact-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("yolov5")
        .expect(insightos::Video{})
        .on_caps([&](const insightos::Caps& caps) {
            EXPECT_EQ(caps.route_name, "yolov5");
            ++caps_seen;
        })
        .on_frame([&](const insightos::Frame& frame) {
            EXPECT_EQ(frame.route_name, "yolov5");
            EXPECT_EQ(frame.format, "mjpeg");
            if (++frames >= 2) {
                app.request_stop();
            }
        });
    std::vector<std::string> argv_storage = {
        "sdk-exact-test",
        "insightos://localhost/front-camera/720p_30",
    };
    auto argv_view = make_argv(argv_storage);
    EXPECT_TRUE(app.bind_from_cli(static_cast<int>(argv_view.size()), argv_view.data(), 1));

    EXPECT_EQ(app.connect(), 0);
    EXPECT_TRUE(frames.load() >= 2);
    EXPECT_TRUE(caps_seen.load() >= 1);

    server.stop();
}

TEST(startup_grouped_bind_fans_out_color_and_depth_routes) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_orbbec_device()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int color_frames{0};
    std::atomic_int depth_frames{0};

    insightos::App app({
        .name = "sdk-grouped-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("orbbec/color")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame& frame) {
            EXPECT_EQ(frame.route_name, "orbbec/color");
            ++color_frames;
            if (color_frames.load() > 0 && depth_frames.load() > 0) {
                app.request_stop();
            }
        });
    app.route("orbbec/depth")
        .expect(insightos::Depth{})
        .on_frame([&](const insightos::Frame& frame) {
            EXPECT_EQ(frame.route_name, "orbbec/depth");
            EXPECT_EQ(frame.format, "y16");
            ++depth_frames;
            if (color_frames.load() > 0 && depth_frames.load() > 0) {
                app.request_stop();
            }
        });
    std::vector<std::string> argv_storage = {
        "sdk-grouped-test",
        "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
    };
    auto argv_view = make_argv(argv_storage);
    EXPECT_TRUE(app.bind_from_cli(static_cast<int>(argv_view.size()), argv_view.data(), 1));

    EXPECT_EQ(app.connect(), 0);
    EXPECT_TRUE(color_frames.load() > 0);
    EXPECT_TRUE(depth_frames.load() > 0);

    server.stop();
}

TEST(startup_audio_bind_delivers_audio_caps_and_frames) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_pipewire_device()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int frames{0};
    std::atomic_int caps_seen{0};

    insightos::App app({
        .name = "sdk-audio-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("audio")
        .expect(insightos::Audio{})
        .on_caps([&](const insightos::Caps& caps) {
            EXPECT_EQ(caps.route_name, "audio");
            EXPECT_EQ(caps.media_kind, "audio");
            EXPECT_EQ(caps.selector, "audio/stereo");
            EXPECT_EQ(caps.format, "s16le");
            EXPECT_EQ(caps.sample_rate, 48000u);
            EXPECT_EQ(caps.channels, 2u);
            ++caps_seen;
        })
        .on_frame([&](const insightos::Frame& frame) {
            EXPECT_EQ(frame.route_name, "audio");
            EXPECT_EQ(frame.media_kind, "audio");
            EXPECT_EQ(frame.selector, "audio/stereo");
            EXPECT_EQ(frame.format, "s16le");
            EXPECT_EQ(frame.sample_rate, 48000u);
            EXPECT_EQ(frame.channels, 2u);
            EXPECT_TRUE(frame.size >= 512);
            if (++frames >= 2) {
                app.request_stop();
            }
        });
    std::vector<std::string> argv_storage = {
        "sdk-audio-test",
        "insightos://localhost/desk-audio/audio/stereo",
    };
    auto argv_view = make_argv(argv_storage);
    EXPECT_TRUE(app.bind_from_cli(static_cast<int>(argv_view.size()), argv_view.data(), 1));

    EXPECT_EQ(app.connect(), 0);
    EXPECT_TRUE(frames.load() >= 2);
    EXPECT_TRUE(caps_seen.load() >= 1);

    server.stop();
}

TEST(running_idle_app_can_receive_late_rest_source_bind) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int frames{0};
    std::atomic_int exit_code{1};

    insightos::App app({
        .name = "sdk-late-bind-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("yolov5")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            if (++frames >= 2) {
                app.request_stop();
            }
        });

    std::jthread app_thread([&] { exit_code.store(app.connect()); });

    EXPECT_TRUE(wait_until([&] { return app.app_id().has_value(); },
                           std::chrono::seconds(2)));

    httplib::Client client("127.0.0.1", port);
    const auto create = post_json(
        client,
        "/api/apps/" + std::to_string(*app.app_id()) + "/sources",
        {
            {"input", "insightos://localhost/front-camera/720p_30"},
            {"target", "yolov5"},
        });
    EXPECT_TRUE(create);
    EXPECT_EQ(create->status, 201);

    EXPECT_TRUE(wait_until([&] { return frames.load() >= 2; },
                           std::chrono::seconds(3)));
    app_thread.join();
    EXPECT_EQ(exit_code.load(), 0);

    server.stop();
}

TEST(bind_from_cli_derives_default_name_from_program_name) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int frames{0};
    std::atomic_int exit_code{1};

    insightos::App app({
        .name = "",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("camera")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            if (++frames >= 2) {
                app.request_stop();
            }
        });

    std::vector<std::string> argv_storage = {
        "/tmp/v4l2_latency_monitor",
        "insightos://localhost/front-camera/720p_30",
    };
    auto argv_view = make_argv(argv_storage);
    EXPECT_TRUE(app.bind_from_cli(static_cast<int>(argv_view.size()), argv_view.data(), 1));

    std::jthread app_thread([&] { exit_code.store(app.connect()); });
    EXPECT_TRUE(wait_until([&] { return app.app_id().has_value(); },
                           std::chrono::seconds(2)));

    httplib::Client client("127.0.0.1", port);
    const auto apps_list = client.Get("/api/apps");
    EXPECT_TRUE(apps_list);
    EXPECT_EQ(apps_list->status, 200);
    const auto apps_json = nlohmann::json::parse(apps_list->body);
    EXPECT_TRUE(apps_json.contains("apps"));
    EXPECT_TRUE(apps_json.at("apps").is_array());
    EXPECT_EQ(apps_json.at("apps").size(), 1u);
    EXPECT_EQ(apps_json.at("apps").front().at("name").get<std::string>(),
              "v4l2-latency-monitor");

    EXPECT_TRUE(wait_until([&] { return frames.load() >= 2; },
                           std::chrono::seconds(3)));
    app_thread.join();
    EXPECT_EQ(exit_code.load(), 0);

    server.stop();
}

TEST(omitted_app_name_derives_from_program_name_and_allows_late_bind) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int frames{0};
    std::atomic_int exit_code{1};

    insightos::App app({
        .name = "",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("camera")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            if (++frames >= 2) {
                app.request_stop();
            }
        });

    std::vector<std::string> argv_storage = {
        "/tmp/v4l2_latency_monitor",
    };
    auto argv_view = make_argv(argv_storage);

    std::jthread app_thread([&] {
        exit_code.store(
            app.run(static_cast<int>(argv_view.size()), argv_view.data()));
    });
    EXPECT_TRUE(wait_until([&] { return app.app_id().has_value(); },
                           std::chrono::seconds(2)));
    EXPECT_EQ(app.app_name(), "v4l2-latency-monitor");

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_EQ(frames.load(), 0);

    httplib::Client client("127.0.0.1", port);
    const auto bind = post_json(
        client,
        "/api/apps/" + std::to_string(*app.app_id()) + "/sources",
        {
            {"input", "insightos://localhost/front-camera/720p_30"},
            {"target", "camera"},
        });
    EXPECT_TRUE(bind);
    EXPECT_EQ(bind->status, 201);

    EXPECT_TRUE(wait_until([&] { return frames.load() >= 2; },
                           std::chrono::seconds(3)));
    app_thread.join();
    EXPECT_EQ(exit_code.load(), 0);

    server.stop();
}

TEST(running_idle_app_can_receive_late_rest_depth_bind) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_orbbec_device()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int depth_frames{0};
    std::atomic_int exit_code{1};

    insightos::App app({
        .name = "sdk-late-depth-bind-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("orbbec/depth")
        .expect(insightos::Depth{})
        .on_frame([&](const insightos::Frame& frame) {
            EXPECT_EQ(frame.route_name, "orbbec/depth");
            EXPECT_EQ(frame.format, "y16");
            if (++depth_frames >= 2) {
                app.request_stop();
            }
        });

    std::jthread app_thread([&] { exit_code.store(app.connect()); });

    EXPECT_TRUE(wait_until([&] { return app.app_id().has_value(); },
                           std::chrono::seconds(2)));

    httplib::Client client("127.0.0.1", port);
    const auto create = post_json(
        client,
        "/api/apps/" + std::to_string(*app.app_id()) + "/sources",
        {
            {"input", "insightos://localhost/desk-rgbd/orbbec/depth/480p_30"},
            {"target", "orbbec/depth"},
        });
    EXPECT_TRUE(create);
    EXPECT_EQ(create->status, 201);

    EXPECT_TRUE(wait_until([&] { return depth_frames.load() >= 2; },
                           std::chrono::seconds(3)));
    app_thread.join();
    EXPECT_EQ(exit_code.load(), 0);

    server.stop();
}

TEST(cli_supports_explicit_multi_target_connections) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera(), make_test_orbbec_device()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_int camera_frames{0};
    std::atomic_int color_frames{0};
    std::atomic_int depth_frames{0};

    insightos::App app({
        .name = "sdk-cli-multi-target-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("camera")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            ++camera_frames;
            if (camera_frames.load() > 0 && color_frames.load() > 0 &&
                depth_frames.load() > 0) {
                app.request_stop();
            }
        });
    app.route("orbbec/color")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            ++color_frames;
            if (camera_frames.load() > 0 && color_frames.load() > 0 &&
                depth_frames.load() > 0) {
                app.request_stop();
            }
        });
    app.route("orbbec/depth")
        .expect(insightos::Depth{})
        .on_frame([&](const insightos::Frame&) {
            ++depth_frames;
            if (camera_frames.load() > 0 && color_frames.load() > 0 &&
                depth_frames.load() > 0) {
                app.request_stop();
            }
        });

    std::vector<std::string> argv_storage = {
        "sdk-cli-multi-target-test",
        "camera=insightos://localhost/front-camera/720p_30",
        "orbbec=insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
    };
    auto argv_view = make_argv(argv_storage);
    EXPECT_TRUE(app.bind_from_cli(static_cast<int>(argv_view.size()), argv_view.data(), 1));

    EXPECT_EQ(app.connect(), 0);
    EXPECT_TRUE(camera_frames.load() > 0);
    EXPECT_TRUE(color_frames.load() > 0);
    EXPECT_TRUE(depth_frames.load() > 0);

    server.stop();
}

TEST(running_app_can_attach_existing_exact_and_grouped_sessions) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera(), make_test_orbbec_device()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    SessionRecord exact_session;
    SessionRecord grouped_session;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(sessions.create_direct_session(
        "insightos://localhost/front-camera/720p_30",
        false,
        exact_session,
        error_status,
        error_code,
        error_message));
    EXPECT_TRUE(sessions.create_direct_session(
        "insightos://localhost/desk-rgbd/orbbec/preset/720p_30",
        false,
        grouped_session,
        error_status,
        error_code,
        error_message));

    std::atomic_int exact_frames{0};
    std::atomic_int color_frames{0};
    std::atomic_int depth_frames{0};
    std::atomic_int exit_code{1};

    insightos::App app({
        .name = "sdk-session-attach-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("yolov5")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            ++exact_frames;
            if (exact_frames.load() > 0 && color_frames.load() > 0 &&
                depth_frames.load() > 0) {
                app.request_stop();
            }
        });
    app.route("orbbec/color")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            ++color_frames;
            if (exact_frames.load() > 0 && color_frames.load() > 0 &&
                depth_frames.load() > 0) {
                app.request_stop();
            }
        });
    app.route("orbbec/depth")
        .expect(insightos::Depth{})
        .on_frame([&](const insightos::Frame& frame) {
            EXPECT_EQ(frame.height, 800u);
            ++depth_frames;
            if (exact_frames.load() > 0 && color_frames.load() > 0 &&
                depth_frames.load() > 0) {
                app.request_stop();
            }
        });

    std::jthread app_thread([&] { exit_code.store(app.connect()); });
    EXPECT_TRUE(wait_until([&] { return app.app_id().has_value(); },
                           std::chrono::seconds(2)));

    httplib::Client client("127.0.0.1", port);
    const auto bind_exact = post_json(
        client,
        "/api/apps/" + std::to_string(*app.app_id()) + "/sources",
        {
            {"session_id", exact_session.session_id},
            {"target", "yolov5"},
        });
    EXPECT_TRUE(bind_exact);
    EXPECT_EQ(bind_exact->status, 201);

    const auto bind_grouped = post_json(
        client,
        "/api/apps/" + std::to_string(*app.app_id()) + "/sources",
        {
            {"session_id", grouped_session.session_id},
            {"target", "orbbec"},
        });
    EXPECT_TRUE(bind_grouped);
    EXPECT_EQ(bind_grouped->status, 201);

    EXPECT_TRUE(wait_until(
        [&] {
            return exact_frames.load() > 0 && color_frames.load() > 0 &&
                   depth_frames.load() > 0;
        },
        std::chrono::seconds(4)));
    app_thread.join();
    EXPECT_EQ(exit_code.load(), 0);

    server.stop();
}

TEST(direct_session_stays_idle_until_bind_exists) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    SessionRecord direct_session;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(sessions.create_direct_session(
        "insightos://localhost/front-camera/720p_30",
        false,
        direct_session,
        error_status,
        error_code,
        error_message));

    std::atomic_int frames{0};
    std::atomic_int exit_code{1};

    insightos::App app({
        .name = "sdk-idle-until-bind-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("camera")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame&) {
            if (++frames >= 2) {
                app.request_stop();
            }
        });

    std::jthread app_thread([&] { exit_code.store(app.connect()); });
    EXPECT_TRUE(wait_until([&] { return app.app_id().has_value(); },
                           std::chrono::seconds(2)));

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_EQ(frames.load(), 0);

    httplib::Client client("127.0.0.1", port);
    const auto bind = post_json(
        client,
        "/api/apps/" + std::to_string(*app.app_id()) + "/sources",
        {
            {"session_id", direct_session.session_id},
            {"target", "camera"},
        });
    EXPECT_TRUE(bind);
    EXPECT_EQ(bind->status, 201);

    EXPECT_TRUE(wait_until([&] { return frames.load() >= 2; },
                           std::chrono::seconds(3)));
    app_thread.join();
    EXPECT_EQ(exit_code.load(), 0);

    server.stop();
}

TEST(rebind_runtime_switches_delivered_caps) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera(), make_test_orbbec_device()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::atomic_bool saw_webcam_caps{false};
    std::atomic_bool saw_orbbec_caps{false};
    std::atomic_int post_rebind_frames{0};
    std::atomic_int exit_code{1};

    insightos::App app({
        .name = "sdk-rebind-runtime-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    app.route("camera")
        .expect(insightos::Video{})
        .on_caps([&](const insightos::Caps& caps) {
            if (caps.width == 1280 && caps.height == 720) {
                saw_webcam_caps.store(true);
            }
            if (caps.width == 640 && caps.height == 480) {
                saw_orbbec_caps.store(true);
            }
        })
        .on_frame([&](const insightos::Frame& frame) {
            if (frame.width == 640 && frame.height == 480 &&
                ++post_rebind_frames >= 2) {
                app.request_stop();
            }
        });
    app.bind_source("camera", "insightos://localhost/front-camera/720p_30");

    std::jthread app_thread([&] { exit_code.store(app.connect()); });
    EXPECT_TRUE(wait_until([&] { return app.app_id().has_value(); },
                           std::chrono::seconds(2)));
    EXPECT_TRUE(wait_until([&] { return saw_webcam_caps.load(); },
                           std::chrono::seconds(3)));

    EXPECT_TRUE(app.rebind("camera",
                           "insightos://localhost/desk-rgbd/orbbec/color/480p_30"));

    EXPECT_TRUE(wait_until([&] { return saw_orbbec_caps.load(); },
                           std::chrono::seconds(4)));
    EXPECT_TRUE(wait_until([&] { return post_rebind_frames.load() >= 2; },
                           std::chrono::seconds(4)));
    app_thread.join();
    EXPECT_EQ(exit_code.load(), 0);

    server.stop();
}

TEST(identical_uri_fanout_shares_runtime_and_frame_pts) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_test_v4l2_camera()};
            return result;
        },
        "localhost",
        "127.0.0.1:8554");
    EXPECT_TRUE(catalog.initialize());

    SessionService sessions(store, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(sessions.initialize());
    AppService apps(store, sessions, "localhost", "127.0.0.1:8554");
    EXPECT_TRUE(apps.initialize());

    RestServer server(store, catalog, sessions, apps, "");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    std::mutex pts_mutex;
    std::vector<std::int64_t> first_pts;
    std::vector<std::int64_t> second_pts;
    std::atomic_int first_exit{1};
    std::atomic_int second_exit{1};

    insightos::App first_app({
        .name = "sdk-fanout-first-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    first_app.route("camera")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame& frame) {
            std::lock_guard lock(pts_mutex);
            first_pts.push_back(frame.pts_ns);
        });
    first_app.bind_source("camera", "insightos://localhost/front-camera/720p_30");

    insightos::App second_app({
        .name = "sdk-fanout-second-test",
        .description = "",
        .backend_host = "127.0.0.1",
        .backend_port = port,
        .refresh_interval_ms = 50,
    });
    second_app.route("camera")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame& frame) {
            std::lock_guard lock(pts_mutex);
            second_pts.push_back(frame.pts_ns);
        });
    second_app.bind_source("camera", "insightos://localhost/front-camera/720p_30");

    std::jthread first_thread([&] { first_exit.store(first_app.connect()); });
    EXPECT_TRUE(wait_until([&] { return first_app.app_id().has_value(); },
                           std::chrono::seconds(2)));
    EXPECT_TRUE(wait_until(
        [&] {
            std::lock_guard lock(pts_mutex);
            return first_pts.size() >= 6;
        },
        std::chrono::seconds(4)));

    std::jthread second_thread([&] { second_exit.store(second_app.connect()); });
    EXPECT_TRUE(wait_until([&] { return second_app.app_id().has_value(); },
                           std::chrono::seconds(2)));

    EXPECT_TRUE(wait_until(
        [&] {
            std::lock_guard lock(pts_mutex);
            return first_pts.size() >= 12 && second_pts.size() >= 4;
        },
        std::chrono::seconds(6)));

    httplib::Client client("127.0.0.1", port);
    const auto status = client.Get("/api/status");
    EXPECT_TRUE(status);
    EXPECT_EQ(status->status, 200);
    const auto status_json = nlohmann::json::parse(status->body);
    EXPECT_EQ(status_json.at("total_serving_runtimes").get<int>(), 1);

    std::set<std::int64_t> overlap;
    {
      std::lock_guard lock(pts_mutex);
      const std::set<std::int64_t> first_set(first_pts.begin(), first_pts.end());
      for (const auto pts : second_pts) {
          if (first_set.contains(pts)) {
              overlap.insert(pts);
          }
      }
    }
    EXPECT_TRUE(overlap.size() >= 2);

    first_app.request_stop();
    second_app.request_stop();
    first_thread.join();
    second_thread.join();
    EXPECT_EQ(first_exit.load(), 0);
    EXPECT_EQ(second_exit.load(), 0);

    server.stop();
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "app_sdk_test: " << tests().size() << " test(s) passed\n";
    return 0;
}
