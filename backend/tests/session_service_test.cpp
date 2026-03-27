// role: focused direct-session service tests for the standalone backend.
// revision: 2026-03-26 task6-serving-runtime-reuse
// major changes: verifies direct session persistence, restart normalization,
// delete-conflict behavior, and serving-runtime reuse against the SQLite-backed
// service using the canonical app-route and app-source schema shape.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/catalog.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"

#include <sqlite3.h>

#include <algorithm>
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
                      ("insight-io-session-test-" + std::to_string(::getpid()) +
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
    device.streams.push_back(std::move(stream));
    return device;
}

void insert_app_source_reference(sqlite3* db,
                                 std::int64_t session_id,
                                 std::int64_t stream_id) {
    constexpr sqlite3_int64 now = 1700000000000LL;

    sqlite3_stmt* app_statement = nullptr;
    const char* app_sql =
        "INSERT INTO apps (name, created_at_ms, updated_at_ms) VALUES (?, ?, ?)";
    EXPECT_EQ(sqlite3_prepare_v2(db, app_sql, -1, &app_statement, nullptr), SQLITE_OK);
    sqlite3_bind_text(app_statement, 1, "test-app", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(app_statement, 2, now);
    sqlite3_bind_int64(app_statement, 3, now);
    EXPECT_EQ(sqlite3_step(app_statement), SQLITE_DONE);
    sqlite3_finalize(app_statement);

    const auto app_id = sqlite3_last_insert_rowid(db);

    sqlite3_stmt* route_statement = nullptr;
    const char* route_sql =
        "INSERT INTO app_routes (app_id, route_name, expect_json, config_json, created_at_ms, updated_at_ms) "
        "VALUES (?, ?, NULL, NULL, ?, ?)";
    EXPECT_EQ(sqlite3_prepare_v2(db, route_sql, -1, &route_statement, nullptr), SQLITE_OK);
    sqlite3_bind_int64(route_statement, 1, app_id);
    sqlite3_bind_text(route_statement, 2, "vision/detector", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(route_statement, 3, now);
    sqlite3_bind_int64(route_statement, 4, now);
    EXPECT_EQ(sqlite3_step(route_statement), SQLITE_DONE);
    sqlite3_finalize(route_statement);

    const auto route_id = sqlite3_last_insert_rowid(db);

    sqlite3_stmt* source_statement = nullptr;
    const char* source_sql =
        "INSERT INTO app_sources (app_id, route_id, stream_id, source_session_id, active_session_id, "
        "target_name, rtsp_enabled, state, resolved_routes_json, last_error, created_at_ms, "
        "updated_at_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL, NULL, ?, ?)";
    EXPECT_EQ(sqlite3_prepare_v2(db, source_sql, -1, &source_statement, nullptr), SQLITE_OK);
    sqlite3_bind_int64(source_statement, 1, app_id);
    sqlite3_bind_int64(source_statement, 2, route_id);
    sqlite3_bind_int64(source_statement, 3, stream_id);
    sqlite3_bind_int64(source_statement, 4, session_id);
    sqlite3_bind_int64(source_statement, 5, session_id);
    sqlite3_bind_text(source_statement, 6, "vision/detector", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(source_statement, 7, 0);
    sqlite3_bind_text(source_statement, 8, "active", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(source_statement, 9, now);
    sqlite3_bind_int64(source_statement, 10, now);
    EXPECT_EQ(sqlite3_step(source_statement), SQLITE_DONE);
    sqlite3_finalize(source_statement);
}

TEST(direct_session_persists_and_normalizes_to_stopped_after_restart) {
    const auto db_path = make_temp_db_path();
    SchemaStore store(db_path);
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

    SessionRecord created;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(sessions.create_direct_session("insightos://localhost/web-camera/720p_30",
                                               true,
                                               created,
                                               error_status,
                                               error_code,
                                               error_message));
    EXPECT_EQ(created.state, "active");
    EXPECT_EQ(created.source.selector, "720p_30");
    EXPECT_EQ(created.rtsp_url, "rtsp://127.0.0.1:8554/web-camera/720p_30");

    SessionService restarted(store);
    EXPECT_TRUE(restarted.initialize());
    const auto reloaded = restarted.get_session(created.session_id);
    EXPECT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->state, "stopped");

    SessionRecord restarted_session;
    EXPECT_TRUE(restarted.start_session(created.session_id,
                                        restarted_session,
                                        error_status,
                                        error_code,
                                        error_message));
    EXPECT_EQ(restarted_session.state, "active");

    SessionRecord stopped_session;
    EXPECT_TRUE(restarted.stop_session(created.session_id,
                                       stopped_session,
                                       error_status,
                                       error_code,
                                       error_message));
    EXPECT_EQ(stopped_session.state, "stopped");
}

TEST(delete_referenced_session_returns_conflict) {
    const auto db_path = make_temp_db_path();
    SchemaStore store(db_path);
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

    SessionRecord created;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(sessions.create_direct_session("insightos://localhost/web-camera/720p_30",
                                               false,
                                               created,
                                               error_status,
                                               error_code,
                                               error_message));

    insert_app_source_reference(store.db(),
                                created.session_id,
                                created.source.stream_id);

    EXPECT_TRUE(!sessions.delete_session(created.session_id,
                                         error_status,
                                         error_code,
                                         error_message));
    EXPECT_EQ(error_status, 409);
    EXPECT_EQ(error_code, "conflict");
    EXPECT_TRUE(sessions.get_session(created.session_id).has_value());
}

TEST(direct_session_rejects_non_local_uri_host) {
    const auto db_path = make_temp_db_path();
    SchemaStore store(db_path);
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

    SessionRecord created;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(!sessions.create_direct_session("insightos://not-local/web-camera/720p_30",
                                                false,
                                                created,
                                                error_status,
                                                error_code,
                                                error_message));
    EXPECT_EQ(error_status, 422);
    EXPECT_EQ(error_code, "invalid_input");
}

TEST(direct_sessions_share_serving_runtime_and_upgrade_rtsp_additively) {
    const auto db_path = make_temp_db_path();
    SchemaStore store(db_path);
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

    int error_status = 0;
    std::string error_code;
    std::string error_message;

    SessionRecord first;
    EXPECT_TRUE(sessions.create_direct_session("insightos://localhost/web-camera/720p_30",
                                               false,
                                               first,
                                               error_status,
                                               error_code,
                                               error_message));
    EXPECT_TRUE(first.serving_runtime.has_value());
    EXPECT_EQ(first.serving_runtime->consumer_count, 1);
    EXPECT_TRUE(!first.serving_runtime->shared);
    EXPECT_TRUE(!first.serving_runtime->rtsp_enabled);

    SessionRecord second;
    EXPECT_TRUE(sessions.create_direct_session("insightos://localhost/web-camera/720p_30",
                                               true,
                                               second,
                                               error_status,
                                               error_code,
                                               error_message));
    EXPECT_TRUE(second.serving_runtime.has_value());
    EXPECT_EQ(second.serving_runtime->consumer_count, 2);
    EXPECT_TRUE(second.serving_runtime->shared);
    EXPECT_TRUE(second.serving_runtime->rtsp_enabled);
    EXPECT_TRUE(second.serving_runtime->rtsp_publication.has_value());
    EXPECT_EQ(second.serving_runtime->rtsp_publication->url,
              "rtsp://127.0.0.1:8554/web-camera/720p_30");
    EXPECT_EQ(second.serving_runtime->rtsp_publication->publication_profile, "default");
    EXPECT_EQ(second.serving_runtime->rtsp_publication->transport, "rtsp");
    EXPECT_EQ(second.serving_runtime->rtsp_publication->promised_format, "h264");
    EXPECT_EQ(second.serving_runtime->rtsp_publication->actual_format, "mjpeg");

    const auto reloaded_first = sessions.get_session(first.session_id);
    EXPECT_TRUE(reloaded_first.has_value());
    EXPECT_TRUE(reloaded_first->serving_runtime.has_value());
    EXPECT_EQ(reloaded_first->serving_runtime->consumer_count, 2);
    EXPECT_TRUE(reloaded_first->serving_runtime->rtsp_enabled);
    EXPECT_TRUE(reloaded_first->serving_runtime->rtsp_publication.has_value());

    auto status = sessions.runtime_status();
    EXPECT_EQ(status.total_serving_runtimes, 1);
    EXPECT_EQ(status.serving_runtimes.size(), 1u);
    EXPECT_EQ(status.serving_runtimes[0].consumer_count, 2);
    EXPECT_TRUE(status.serving_runtimes[0].rtsp_enabled);
    EXPECT_TRUE(status.serving_runtimes[0].rtsp_publication.has_value());
    EXPECT_EQ(status.serving_runtimes[0].rtsp_publication->url,
              "rtsp://127.0.0.1:8554/web-camera/720p_30");
    EXPECT_TRUE(std::find(status.serving_runtimes[0].consumer_session_ids.begin(),
                          status.serving_runtimes[0].consumer_session_ids.end(),
                          first.session_id) !=
                status.serving_runtimes[0].consumer_session_ids.end());
    EXPECT_TRUE(std::find(status.serving_runtimes[0].consumer_session_ids.begin(),
                          status.serving_runtimes[0].consumer_session_ids.end(),
                          second.session_id) !=
                status.serving_runtimes[0].consumer_session_ids.end());

    SessionRecord stopped;
    EXPECT_TRUE(sessions.stop_session(second.session_id,
                                      stopped,
                                      error_status,
                                      error_code,
                                      error_message));

    status = sessions.runtime_status();
    EXPECT_EQ(status.total_serving_runtimes, 1);
    EXPECT_EQ(status.serving_runtimes[0].consumer_count, 1);
    EXPECT_TRUE(!status.serving_runtimes[0].rtsp_enabled);
    EXPECT_EQ(status.serving_runtimes[0].consumer_session_ids[0], first.session_id);

    EXPECT_TRUE(sessions.stop_session(first.session_id,
                                      stopped,
                                      error_status,
                                      error_code,
                                      error_message));
    status = sessions.runtime_status();
    EXPECT_EQ(status.total_serving_runtimes, 0);
    EXPECT_TRUE(status.serving_runtimes.empty());
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "session_service_test: " << tests().size()
              << " test(s) passed\n";
    return 0;
}
