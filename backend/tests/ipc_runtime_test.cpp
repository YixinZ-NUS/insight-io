// role: focused task-7 IPC runtime tests for the standalone backend.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: verifies local attach over the unix control socket plus
// capture-to-IPC fanout for exact and grouped sessions using deterministic
// `test:` devices instead of live hardware.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/catalog.hpp"
#include "insightio/backend/ipc.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"
#include "insightio/backend/unix_socket.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <poll.h>
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
                      ("insight-io-ipc-runtime-test-" + std::to_string(::getpid()) +
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

    StreamInfo depth;
    depth.stream_id = "depth";
    depth.name = "depth";
    depth.supported_caps.push_back(ResolvedCaps{0, "y16", 640, 400, 30});

    device.streams.push_back(std::move(color));
    device.streams.push_back(std::move(depth));
    return device;
}

struct AttachLease {
    int control_fd{-1};
    std::shared_ptr<ipc::Reader> reader;
    nlohmann::json response_json;
};

AttachLease attach_reader_for_session(const std::string& socket_path,
                                      std::int64_t session_id,
                                      const std::string& stream_name = {}) {
    auto connect_res = ipc::connect_socket(socket_path);
    EXPECT_TRUE(connect_res.ok());
    const int control_fd = connect_res.value();

    nlohmann::json request = {
        {"session_id", session_id},
    };
    if (!stream_name.empty()) {
        request["stream_name"] = stream_name;
    }
    EXPECT_TRUE(ipc::send_message(control_fd, request.dump(), {}).ok());

    auto message_res = ipc::recv_message(control_fd, 4096, 2);
    EXPECT_TRUE(message_res.ok());
    EXPECT_EQ(message_res.value().fds.size(), 2u);

    auto response_json = nlohmann::json::parse(message_res.value().payload);
    EXPECT_EQ(response_json.at("status").get<std::string>(), "ok");

    auto reader_res =
        ipc::attach_reader(message_res.value().fds[0], message_res.value().fds[1]);
    EXPECT_TRUE(reader_res.ok());

    AttachLease lease;
    lease.control_fd = control_fd;
    lease.reader = reader_res.value();
    lease.response_json = std::move(response_json);
    return lease;
}

std::vector<std::uint8_t> wait_for_frame(const std::shared_ptr<ipc::Reader>& reader) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        pollfd descriptor{};
        descriptor.fd = reader->event_fd();
        descriptor.events = POLLIN;
        EXPECT_TRUE(::poll(&descriptor, 1, 500) >= 0);
        if ((descriptor.revents & POLLIN) != 0) {
            reader->clear_event();
        }

        if (auto frame = reader->read()) {
            return std::vector<std::uint8_t>(frame->data, frame->data + frame->size);
        }
    }
    std::cerr << "FAIL at " << __FILE__ << ":" << __LINE__
              << "\nno frame received\n";
    std::exit(1);
}

void close_lease(AttachLease& lease) {
    lease.reader.reset();
    if (lease.control_fd >= 0) {
        ::close(lease.control_fd);
        lease.control_fd = -1;
    }
}

bool wait_for_attached_consumer_count(SessionService& sessions,
                                      std::int64_t stream_id,
                                      std::string_view stream_name,
                                      int expected_count) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto snapshot = sessions.runtime_status();
        for (const auto& runtime : snapshot.serving_runtimes) {
            if (runtime.stream_id != stream_id) {
                continue;
            }
            for (const auto& channel : runtime.ipc_channels) {
                if (channel.stream_name == stream_name &&
                    channel.attached_consumer_count == expected_count) {
                    return true;
                }
            }
        }
        ::usleep(50 * 1000);
    }
    return false;
}

bool wait_for_runtime_state(SessionService& sessions,
                            std::int64_t stream_id,
                            std::string_view expected_state) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto snapshot = sessions.runtime_status();
        for (const auto& runtime : snapshot.serving_runtimes) {
            if (runtime.stream_id == stream_id && runtime.state == expected_state) {
                return true;
            }
        }
        ::usleep(50 * 1000);
    }
    return false;
}

TEST(exact_session_attach_starts_capture_and_publishes_frames) {
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

    SessionRecord created;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(sessions.create_direct_session("insightos://localhost/front-camera/720p_30",
                                               false,
                                               created,
                                               error_status,
                                               error_code,
                                               error_message));
    EXPECT_TRUE(created.serving_runtime.has_value());
    EXPECT_EQ(created.serving_runtime->state, "ready");
    EXPECT_EQ(created.serving_runtime->ipc_channels.size(), 1u);
    EXPECT_EQ(created.serving_runtime->ipc_channels[0].stream_name, "image");

    auto lease =
        attach_reader_for_session(sessions.ipc_socket_path(), created.session_id);
    EXPECT_EQ(lease.response_json.at("stream").at("stream_name").get<std::string>(),
              "image");

    const auto first = wait_for_frame(lease.reader);
    const auto second = wait_for_frame(lease.reader);
    EXPECT_TRUE(!first.empty());
    EXPECT_TRUE(!second.empty());
    EXPECT_TRUE(first[0] != second[0]);

    EXPECT_TRUE(wait_for_attached_consumer_count(
        sessions, created.source.stream_id, "image", 1));

    close_lease(lease);

    EXPECT_TRUE(wait_for_attached_consumer_count(
        sessions, created.source.stream_id, "image", 0));
    EXPECT_TRUE(wait_for_runtime_state(sessions, created.source.stream_id, "ready"));

    auto second_lease =
        attach_reader_for_session(sessions.ipc_socket_path(), created.session_id);
    const auto after_idle = wait_for_frame(second_lease.reader);
    EXPECT_TRUE(!after_idle.empty());
    EXPECT_TRUE(wait_for_attached_consumer_count(
        sessions, created.source.stream_id, "image", 1));

    close_lease(second_lease);
    EXPECT_TRUE(wait_for_attached_consumer_count(
        sessions, created.source.stream_id, "image", 0));
    EXPECT_TRUE(wait_for_runtime_state(sessions, created.source.stream_id, "ready"));
}

TEST(grouped_orbbec_session_requires_stream_name_and_fans_out_color_and_depth) {
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

    SessionRecord created;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(sessions.create_direct_session(
        "insightos://localhost/desk-rgbd/orbbec/preset/480p_30",
        false,
        created,
        error_status,
        error_code,
        error_message));
    EXPECT_TRUE(created.serving_runtime.has_value());
    EXPECT_EQ(created.serving_runtime->ipc_channels.size(), 2u);

    auto missing_stream_fd = ipc::connect_socket(sessions.ipc_socket_path());
    EXPECT_TRUE(missing_stream_fd.ok());
    EXPECT_TRUE(ipc::send_message(
                    missing_stream_fd.value(),
                    nlohmann::json{{"session_id", created.session_id}}.dump(),
                    {})
                    .ok());
    auto missing_stream_reply = ipc::recv_message(missing_stream_fd.value(), 4096, 0);
    EXPECT_TRUE(missing_stream_reply.ok());
    const auto missing_stream_json =
        nlohmann::json::parse(missing_stream_reply.value().payload);
    EXPECT_EQ(missing_stream_json.at("status").get<std::string>(), "error");
    EXPECT_EQ(missing_stream_json.at("code").get<std::string>(), "missing_stream_name");
    ::close(missing_stream_fd.value());

    auto color = attach_reader_for_session(
        sessions.ipc_socket_path(), created.session_id, "color");
    auto depth = attach_reader_for_session(
        sessions.ipc_socket_path(), created.session_id, "orbbec/depth");

    EXPECT_EQ(color.response_json.at("stream").at("stream_name").get<std::string>(),
              "color");
    EXPECT_EQ(depth.response_json.at("stream").at("stream_name").get<std::string>(),
              "depth");

    const auto color_frame = wait_for_frame(color.reader);
    const auto depth_frame = wait_for_frame(depth.reader);
    EXPECT_TRUE(!color_frame.empty());
    EXPECT_TRUE(!depth_frame.empty());

    EXPECT_TRUE(wait_for_attached_consumer_count(
        sessions, created.source.stream_id, "color", 1));
    EXPECT_TRUE(wait_for_attached_consumer_count(
        sessions, created.source.stream_id, "depth", 1));

    close_lease(color);
    close_lease(depth);
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "ipc_runtime_test: " << tests().size() << " test(s) passed\n";
    return 0;
}
