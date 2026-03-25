// role: focused REST health test for the standalone insight-io backend.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: verifies the bootstrap server starts and answers GET /api/health.

#include "insightio/backend/rest_server.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/version.hpp"

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

std::string make_temp_db_path() {
    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("insight-io-rest-test-" + std::to_string(::getpid()) +
                       "-" + std::to_string(counter++) + ".sqlite3");
    return path.string();
}

uint16_t start_test_server(insightio::backend::RestServer& server) {
    for (uint16_t port = 29580; port < 29590; ++port) {
        if (server.start("127.0.0.1", port)) {
            return port;
        }
    }
    return 0;
}

TEST(health_endpoint_returns_status_and_version) {
    const auto db_path = make_temp_db_path();
    insightio::backend::SchemaStore store(db_path);
    EXPECT_TRUE(store.initialize());

    insightio::backend::RestServer server(store, "/tmp/frontend");
    const auto port = start_test_server(server);
    EXPECT_TRUE(port != 0);

    httplib::Client client("127.0.0.1", port);
    const auto response = client.Get("/api/health");
    EXPECT_TRUE(response);
    EXPECT_EQ(response->status, 200);

    const auto json = nlohmann::json::parse(response->body);
    EXPECT_EQ(json.at("status").get<std::string>(), "ok");
    EXPECT_EQ(json.at("version").get<std::string>(), std::string(insightio::backend::kVersion));
    EXPECT_EQ(json.at("db_path").get<std::string>(), db_path);
    EXPECT_EQ(json.at("frontend_path").get<std::string>(), "/tmp/frontend");

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
