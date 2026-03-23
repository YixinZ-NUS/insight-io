/// InsightOS backend — DeviceStore unit tests.
///
/// Self-contained test executable using simple assertion macros.
/// Build: see backend/CMakeLists.txt (device_store_test target).

#include "insightos/backend/device_store.hpp"
#include "insightos/backend/types.hpp"

#include <sqlite3.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
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
    static std::vector<TestEntry> t;
    return t;
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

std::string make_temp_db_path() {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("insightos-device-store-test-" + std::to_string(::getpid()) +
                 "-" + std::to_string(counter++) + ".sqlite3");
    return path.string();
}

DeviceInfo make_device(const std::string& uri, DeviceKind kind,
                       const std::string& name,
                       const std::string& usb_serial = "") {
    DeviceInfo dev;
    dev.uri = uri;
    dev.kind = kind;
    dev.name = name;
    dev.state = DeviceState::kDiscovered;
    dev.identity.device_uri = uri;
    dev.identity.device_id = name;
    dev.identity.kind_str = to_string(kind);
    dev.identity.hardware_name = name;
    dev.identity.usb_serial = usb_serial;

    // Add a stream with caps
    StreamInfo si;
    si.stream_id = "image";
    si.name = "frame";
    si.data_kind = DataKind::kFrame;
    ResolvedCaps cap;
    cap.index = 0;
    cap.format = "mjpeg";
    cap.width = 1920;
    cap.height = 1080;
    cap.fps = 30;
    si.supported_caps.push_back(cap);
    dev.streams.push_back(si);

    return dev;
}

DeviceInfo make_orbbec_device(const std::string& uri, const std::string& name,
                              const std::string& usb_serial = "") {
    DeviceInfo dev;
    dev.uri = uri;
    dev.kind = DeviceKind::kOrbbec;
    dev.name = name;
    dev.state = DeviceState::kDiscovered;
    dev.identity.device_uri = uri;
    dev.identity.device_id = name;
    dev.identity.kind_str = to_string(dev.kind);
    dev.identity.hardware_name = name;
    dev.identity.usb_serial = usb_serial;

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

    dev.streams.push_back(std::move(color));
    dev.streams.push_back(std::move(depth));
    return dev;
}

void seed_runtime_binding(DeviceStore& store, const std::string& run_id,
                          const std::string& capture_session_id,
                          const std::string& preset_id,
                          const std::string& delivery_session_id,
                          const std::string& stream_key,
                          const std::string& delivery_name = "mjpeg",
                          const std::string& transport = "ipc") {
    EXPECT_TRUE(store.start_daemon_run(DaemonRunRow{
        .daemon_run_id = run_id,
        .state = "active",
        .started_at_ms = 1,
        .ended_at_ms = 0,
        .pid = 1,
        .version = "test",
        .last_heartbeat_at_ms = 1,
    }));
    EXPECT_TRUE(store.save_capture_session(CaptureSessionRow{
        .capture_session_id = capture_session_id,
        .daemon_run_id = run_id,
        .preset_id = preset_id,
        .capture_policy_key = "policy",
        .capture_policy_json = "{}",
        .state = "active",
        .started_at_ms = 1,
        .stopped_at_ms = 0,
        .last_error = "",
    }));
    EXPECT_TRUE(store.save_delivery_session(DeliverySessionRow{
        .delivery_session_id = delivery_session_id,
        .capture_session_id = capture_session_id,
        .stream_key = stream_key,
        .delivery_name = delivery_name,
        .transport = transport,
        .promised_format = "mjpeg",
        .actual_format = "mjpeg",
        .channel_id = delivery_session_id,
        .rtsp_url = "",
        .state = "active",
        .started_at_ms = 1,
        .stopped_at_ms = 0,
        .last_error = "",
    }));
}

// ─── Tests ──────────────────────────────────────────────────────────────

TEST(open_close) {
    DeviceStore store(make_temp_db_path());
    EXPECT_TRUE(store.open());
    store.close();
}

TEST(open_twice_is_safe) {
    DeviceStore store(make_temp_db_path());
    EXPECT_TRUE(store.open());
    EXPECT_TRUE(store.open());  // second open is a no-op
    store.close();
}

TEST(replace_devices_basic) {
    DeviceStore store(make_temp_db_path());
    store.open();

    std::vector<DeviceInfo> devs;
    devs.push_back(make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001"));
    devs.push_back(make_device("v4l2:/dev/video2", DeviceKind::kV4l2, "cam2", "SN002"));

    EXPECT_TRUE(store.replace_devices(devs));

    auto result = store.get_devices();
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].uri, "v4l2:/dev/video0");
    EXPECT_EQ(result[1].uri, "v4l2:/dev/video2");
}

TEST(replace_devices_preserves_alias) {
    DeviceStore store(make_temp_db_path());
    store.open();

    // Insert initial devices
    auto dev1 = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
    EXPECT_TRUE(store.replace_devices({dev1}));

    std::string uuid = stable_device_uuid(dev1);
    auto found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());

    auto alias_res = store.set_alias(found->device_key, "front-camera");
    EXPECT_TRUE(alias_res.ok());

    // Verify alias and current public id are updated.
    found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->public_id, "front-camera");

    // Replace devices (same UUID, different URI simulating re-enumeration)
    auto dev1_new = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
    EXPECT_TRUE(store.replace_devices({dev1_new}));

    // Public device id should be preserved.
    found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->public_id, "front-camera");
}

TEST(find_by_uri) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uri("v4l2:/dev/video0");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "cam0");

    auto not_found = store.find_by_uri("v4l2:/dev/video99");
    EXPECT_FALSE(not_found.has_value());
}

TEST(find_by_uuid) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));

    std::string uuid = stable_device_uuid(dev);
    auto found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->uri, "v4l2:/dev/video0");

    auto not_found = store.find_by_uuid("nonexistent-uuid");
    EXPECT_FALSE(not_found.has_value());
}

TEST(find_by_key) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));

    auto by_uuid = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(by_uuid.has_value());
    auto by_key = store.find_by_key(by_uuid->device_key);
    EXPECT_TRUE(by_key.has_value());
    EXPECT_EQ(by_key->uri, "v4l2:/dev/video0");
    EXPECT_EQ(by_key->default_public_id, "cam0");
    EXPECT_EQ(by_key->public_id, "cam0");
}

TEST(set_alias) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));
    std::string uuid = stable_device_uuid(dev);
    auto found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());

    auto res = store.set_alias(found->device_key, "my-camera");
    EXPECT_TRUE(res.ok());

    found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->default_public_id, "cam0");
    EXPECT_EQ(found->public_id, "my-camera");
}

TEST(set_alias_not_found) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto res = store.set_alias("nonexistent-device-key", "alias");
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.error().code, "not_found");
}

TEST(clear_alias) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));
    std::string uuid = stable_device_uuid(dev);
    auto found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());

    store.set_alias(found->device_key, "my-camera");
    auto res = store.clear_alias(found->device_key);
    EXPECT_TRUE(res.ok());

    found = store.find_by_uuid(uuid);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->default_public_id, "cam0");
    EXPECT_EQ(found->public_id, "cam0");
}

TEST(reopen_preserves_alias_and_sessions) {
    const auto db_path = make_temp_db_path();
    {
        DeviceStore store(db_path);
        EXPECT_TRUE(store.open());
        auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001");
        EXPECT_TRUE(store.replace_devices({dev}));
        auto uuid = stable_device_uuid(dev);
        auto found = store.find_by_uuid(uuid);
        EXPECT_TRUE(found.has_value());
        EXPECT_TRUE(store.set_alias(found->device_key, "front").ok());

        SessionRow row;
        row.session_id = "sess-reopen";
        row.state = "stopped";
        row.request_name = "front";
        row.request_preset_name = "720p_30";
        row.device_uuid = uuid;
        row.preset_id = found->device_key + "::720p_30";
        row.preset_name = "720p_30";
        EXPECT_TRUE(store.save_session(row, {}));
        store.close();
    }

    DeviceStore reopened(db_path);
    EXPECT_TRUE(reopened.open());
    auto devices = reopened.get_devices();
    EXPECT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].default_public_id, "cam0");
    EXPECT_EQ(devices[0].public_id, "front");
    auto session = reopened.find_session("sess-reopen");
    EXPECT_TRUE(session.has_value());
    EXPECT_EQ(session->state, "stopped");
    EXPECT_EQ(session->device_uuid, stable_device_uuid(devices[0]));
    EXPECT_EQ(session->preset_name, "720p_30");
}

TEST(refresh_updates_default_public_id_without_custom_alias) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "Web Camera", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->default_public_id, "web-camera");
    EXPECT_EQ(found->public_id, "web-camera");

    dev.name = "Lobby Camera";
    dev.identity.hardware_name = dev.name;
    EXPECT_TRUE(store.replace_devices({dev}));

    found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->default_public_id, "lobby-camera");
    EXPECT_EQ(found->public_id, "lobby-camera");
}

TEST(refresh_preserves_custom_alias_while_updating_default_public_id) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "Web Camera", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_TRUE(store.set_alias(found->device_key, "front-camera").ok());

    dev.name = "Lobby Camera";
    dev.identity.hardware_name = dev.name;
    EXPECT_TRUE(store.replace_devices({dev}));

    found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->default_public_id, "lobby-camera");
    EXPECT_EQ(found->public_id, "front-camera");
}

TEST(refresh_preserves_custom_stream_alias_while_device_metadata_changes) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2,
                           "Web Camera", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_TRUE(store.set_stream_alias(found->device_key, "image", "preview").ok());

    dev.name = "Lobby Camera";
    dev.identity.hardware_name = dev.name;
    EXPECT_TRUE(store.replace_devices({dev}));

    found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->streams.size(), 1u);
    EXPECT_EQ(found->streams[0].stream_id, "image");
    EXPECT_EQ(found->streams[0].name, "preview");
}

TEST(clear_stream_alias_restores_default_public_name) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2,
                           "Web Camera", "SN001");
    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_TRUE(store.set_stream_alias(found->device_key, "image", "preview").ok());

    auto res = store.clear_stream_alias(found->device_key, "image");
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.value(), "frame");

    found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->streams[0].stream_id, "image");
    EXPECT_EQ(found->streams[0].name, "frame");
}

TEST(set_stream_alias_conflict_on_same_device) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto dev = make_orbbec_device("orbbec://device-001", "depth-cam", "ORB001");
    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uuid(stable_device_uuid(dev));
    EXPECT_TRUE(found.has_value());

    auto res = store.set_stream_alias(found->device_key, "depth", "color");
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.error().code, "conflict");
}

TEST(reopen_preserves_stream_alias) {
    const auto db_path = make_temp_db_path();
    {
        DeviceStore store(db_path);
        EXPECT_TRUE(store.open());
        auto dev = make_device("v4l2:/dev/video0", DeviceKind::kV4l2,
                               "Web Camera", "SN001");
        EXPECT_TRUE(store.replace_devices({dev}));
        auto found = store.find_by_uuid(stable_device_uuid(dev));
        EXPECT_TRUE(found.has_value());
        EXPECT_TRUE(store.set_stream_alias(found->device_key, "image", "preview").ok());
        store.close();
    }

    DeviceStore reopened(db_path);
    EXPECT_TRUE(reopened.open());
    auto devices = reopened.get_devices();
    EXPECT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].streams.size(), 1u);
    EXPECT_EQ(devices[0].streams[0].stream_id, "image");
    EXPECT_EQ(devices[0].streams[0].name, "preview");
}

TEST(streams_and_caps_roundtrip) {
    DeviceStore store(make_temp_db_path());
    store.open();

    DeviceInfo dev;
    dev.uri = "v4l2:/dev/video0";
    dev.kind = DeviceKind::kV4l2;
    dev.name = "cam0";
    dev.state = DeviceState::kDiscovered;
    dev.identity.device_uri = dev.uri;
    dev.identity.device_id = "video0";
    dev.identity.kind_str = "v4l2";
    dev.identity.hardware_name = "Test Camera";
    dev.identity.usb_serial = "SN001";

    StreamInfo si;
    si.stream_id = "image";
    si.name = "frame";
    si.data_kind = DataKind::kFrame;

    ResolvedCaps cap1;
    cap1.index = 7;
    cap1.format = "mjpeg";
    cap1.width = 1920;
    cap1.height = 1080;
    cap1.fps = 30;

    ResolvedCaps cap2;
    cap2.index = 11;
    cap2.format = "yuyv";
    cap2.width = 640;
    cap2.height = 480;
    cap2.fps = 30;

    si.supported_caps = {cap1, cap2};
    dev.streams.push_back(si);

    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uri("v4l2:/dev/video0");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->streams.size(), 1u);
    EXPECT_EQ(found->streams[0].stream_id, "image");
    EXPECT_EQ(found->streams[0].name, "frame");
    EXPECT_EQ(found->streams[0].supported_caps.size(), 2u);
    EXPECT_EQ(found->streams[0].supported_caps[0].index, 7u);
    EXPECT_EQ(found->streams[0].supported_caps[0].format, "mjpeg");
    EXPECT_EQ(found->streams[0].supported_caps[0].width, 1920u);
    EXPECT_EQ(found->streams[0].supported_caps[1].index, 11u);
    EXPECT_EQ(found->streams[0].supported_caps[1].format, "yuyv");
    EXPECT_EQ(found->streams[0].supported_caps[1].width, 640u);
}

TEST(session_crud) {
    DeviceStore store(make_temp_db_path());
    store.open();

    SessionRow row;
    row.session_id = "sess-001";
    row.state = "pending";
    row.request_name = "front";
    row.request_preset_name = "1080p_30";
    row.request_delivery_name = "mjpeg";
    row.request_origin = "http_api";
    row.device_uuid = "uuid-001";
    row.preset_id = "dev_001::1080p_30";
    row.preset_name = "1080p_30";
    row.delivery_name = "mjpeg";

    EXPECT_TRUE(store.save_session(row, {}));

    // Find
    auto found = store.find_session("sess-001");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->session_id, "sess-001");
    EXPECT_EQ(found->state, "pending");
    EXPECT_EQ(found->request_name, "front");
    EXPECT_EQ(found->preset_id, "dev_001::1080p_30");
    EXPECT_EQ(found->device_uuid, "uuid-001");

    // Update
    seed_runtime_binding(store, "run-001", "cap-001", "dev_001::1080p_30",
                         "del-001", "dev_001::image");
    row.state = "active";
    row.started_at_ms = 1234;
    EXPECT_TRUE(store.save_session(
        row, {{"sess-001", "del-001"}}));

    found = store.find_session("sess-001");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->state, "active");
    EXPECT_EQ(found->started_at_ms, 1234);
    EXPECT_EQ(store.get_session_bindings("sess-001").size(), 1u);

    // List
    auto all = store.get_sessions();
    EXPECT_EQ(all.size(), 1u);

    // Delete
    EXPECT_TRUE(store.delete_session("sess-001"));
    EXPECT_FALSE(store.find_session("sess-001").has_value());
    EXPECT_FALSE(store.delete_session("sess-001"));
}

TEST(session_bindings) {
    DeviceStore store(make_temp_db_path());
    store.open();

    seed_runtime_binding(store, "run-002", "cap-002", "dev_002::preset",
                         "del-image", "dev_002::image");
    seed_runtime_binding(store, "run-002", "cap-002", "dev_002::preset",
                         "del-audio", "dev_002::audio");

    SessionRow row;
    row.session_id = "sess-002";
    row.state = "active";

    std::vector<SessionBindingRow> keys;
    keys.push_back({"sess-002", "del-image"});
    keys.push_back({"sess-002", "del-audio"});
    EXPECT_TRUE(store.save_session(row, keys));

    auto result = store.get_session_bindings("sess-002");
    EXPECT_EQ(result.size(), 2u);

    EXPECT_TRUE(store.save_session(row, {}));
    result = store.get_session_bindings("sess-002");
    EXPECT_EQ(result.size(), 0u);
}

TEST(session_bindings_cascade_on_delete) {
    DeviceStore store(make_temp_db_path());
    store.open();

    seed_runtime_binding(store, "run-003", "cap-003", "dev_003::preset",
                         "del-image", "dev_003::image");

    SessionRow row;
    row.session_id = "sess-003";

    std::vector<SessionBindingRow> keys;
    keys.push_back({"sess-003", "del-image"});
    EXPECT_TRUE(store.save_session(row, keys));

    // Deleting the session should cascade-delete bindings.
    store.delete_session("sess-003");
    auto result = store.get_session_bindings("sess-003");
    EXPECT_EQ(result.size(), 0u);
}

TEST(reset_runtime_state_marks_sessions_stopped) {
    DeviceStore store(make_temp_db_path());
    store.open();

    seed_runtime_binding(store, "run-004", "cap-004", "dev_004::preset",
                         "del-004", "dev_004::image");

    SessionRow row;
    row.session_id = "sess-004";
    row.state = "active";
    row.started_at_ms = 99;
    EXPECT_TRUE(store.save_session(
        row, {{"sess-004", "del-004"}}));

    store.reset_runtime_state();

    auto found = store.find_session("sess-004");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->state, "stopped");
    EXPECT_EQ(found->stopped_at_ms, 99);
    EXPECT_EQ(store.get_session_bindings("sess-004").size(), 0u);
}

TEST(replace_devices_clears_old) {
    DeviceStore store(make_temp_db_path());
    store.open();

    // Insert two devices
    EXPECT_TRUE(store.replace_devices({
        make_device("v4l2:/dev/video0", DeviceKind::kV4l2, "cam0", "SN001"),
        make_device("v4l2:/dev/video2", DeviceKind::kV4l2, "cam2", "SN002"),
    }));
    EXPECT_EQ(store.get_devices().size(), 2u);

    // Replace with one device — old ones should be gone
    EXPECT_TRUE(store.replace_devices({
        make_device("v4l2:/dev/video4", DeviceKind::kV4l2, "cam4", "SN003"),
    }));
    EXPECT_EQ(store.get_devices().size(), 1u);
    EXPECT_EQ(store.get_devices()[0].uri, "v4l2:/dev/video4");

    // Old device should not be found
    EXPECT_FALSE(store.find_by_uri("v4l2:/dev/video0").has_value());
}

TEST(device_identity_roundtrip) {
    DeviceStore store(make_temp_db_path());
    store.open();

    DeviceInfo dev;
    dev.uri = "v4l2:/dev/video0";
    dev.kind = DeviceKind::kV4l2;
    dev.name = "Test Camera";
    dev.description = "A test camera device";
    dev.state = DeviceState::kActive;
    dev.public_id = "lobby-cam";
    dev.identity.device_uri = dev.uri;
    dev.identity.device_id = "video0";
    dev.identity.kind_str = "v4l2";
    dev.identity.hardware_name = "USB Camera HD";
    dev.identity.persistent_key = "pci-0000:00:14.0-usb-0:3.2:1.0";
    dev.identity.usb_vendor_id = "0x1234";
    dev.identity.usb_product_id = "0x5678";
    dev.identity.usb_serial = "ABC123";

    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uri("v4l2:/dev/video0");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->default_public_id, "test-camera");
    EXPECT_EQ(found->public_id, "lobby-cam");
    EXPECT_TRUE(!found->device_key.empty());
    EXPECT_EQ(found->name, "Test Camera");
    EXPECT_EQ(found->description, "A test camera device");
    EXPECT_EQ(found->identity.device_id, "video0");
    EXPECT_EQ(found->identity.kind_str, "v4l2");
    EXPECT_EQ(found->identity.hardware_name, "USB Camera HD");
    EXPECT_EQ(found->identity.persistent_key, "pci-0000:00:14.0-usb-0:3.2:1.0");
    EXPECT_EQ(found->identity.usb_vendor_id, "0x1234");
    EXPECT_EQ(found->identity.usb_product_id, "0x5678");
    EXPECT_EQ(found->identity.usb_serial, "ABC123");
}

TEST(prefix_free_public_ids_use_discovered_names) {
    DeviceStore store(make_temp_db_path());
    store.open();

    auto camera =
        make_device("v4l2:/dev/video0", DeviceKind::kV4l2,
                    "Web Camera", "SN-CAM");
    camera.identity.device_id = "video0";
    camera.identity.kind_str = "v4l2";
    camera.identity.hardware_name = camera.name;

    auto microphone =
        make_device("pw:63", DeviceKind::kPipeWire, "Web Camera Mono");
    microphone.identity.device_id = "63";
    microphone.identity.kind_str = "pw";
    microphone.identity.hardware_name = microphone.name;
    microphone.identity.persistent_key = "alsa:pcm:1:hw:1:capture";

    EXPECT_TRUE(store.replace_devices({camera, microphone}));

    const auto devices = store.get_devices();
    EXPECT_EQ(devices.size(), 2u);

    std::map<std::string, std::string> ids_by_uri;
    for (const auto& device : devices) {
        ids_by_uri[device.uri] = device.public_id;
        EXPECT_TRUE(!device.default_public_id.empty());
    }

    EXPECT_EQ(ids_by_uri["v4l2:/dev/video0"], "web-camera");
    EXPECT_EQ(ids_by_uri["pw:63"], "web-camera-mono");
}

TEST(multiple_streams_per_device) {
    DeviceStore store(make_temp_db_path());
    store.open();

    DeviceInfo dev;
    dev.uri = "orbbec://SN001";
    dev.kind = DeviceKind::kOrbbec;
    dev.name = "rgbd";
    dev.state = DeviceState::kDiscovered;
    dev.identity.device_uri = dev.uri;
    dev.identity.usb_serial = "SN001";

    StreamInfo color_stream;
    color_stream.name = "color";
    color_stream.data_kind = DataKind::kFrame;
    ResolvedCaps color_cap;
    color_cap.format = "mjpeg";
    color_cap.width = 1280;
    color_cap.height = 720;
    color_cap.fps = 30;
    color_stream.supported_caps.push_back(color_cap);
    dev.streams.push_back(color_stream);

    StreamInfo depth_stream;
    depth_stream.name = "depth";
    depth_stream.data_kind = DataKind::kFrame;
    ResolvedCaps depth_cap;
    depth_cap.format = "y16";
    depth_cap.width = 640;
    depth_cap.height = 480;
    depth_cap.fps = 30;
    depth_stream.supported_caps.push_back(depth_cap);
    dev.streams.push_back(depth_stream);

    EXPECT_TRUE(store.replace_devices({dev}));

    auto found = store.find_by_uri("orbbec://SN001");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->streams.size(), 2u);
    // Streams are ordered by name
    EXPECT_EQ(found->streams[0].name, "color");
    EXPECT_EQ(found->streams[1].name, "depth");
    EXPECT_EQ(found->streams[0].supported_caps[0].format, "mjpeg");
    EXPECT_EQ(found->streams[1].supported_caps[0].format, "y16");
}

}  // namespace

int main() {
    int passed = 0;
    for (const auto& t : tests()) {
        std::cout << "  " << t.name << " ... ";
        t.fn();
        std::cout << "ok\n";
        ++passed;
    }
    std::cout << "\n" << passed << " tests passed.\n";
    return 0;
}
