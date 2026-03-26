// role: focused schema bootstrap test for the standalone insight-io backend.
// revision: 2026-03-26 selector-schema-review
// major changes: verifies the checked-in seven-table schema is applied and the
// reviewed per-device selector uniqueness is present.

#include "insightio/backend/schema_store.hpp"

#include <sqlite3.h>

#include <cstdlib>
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

std::string make_temp_db_path() {
    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("insight-io-schema-test-" + std::to_string(::getpid()) +
                       "-" + std::to_string(counter++) + ".sqlite3");
    return path.string();
}

std::set<std::string> list_tables(sqlite3* db) {
    std::set<std::string> tables;
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT name FROM sqlite_master "
        "WHERE type = 'table' AND name NOT LIKE 'sqlite_%'";
    EXPECT_EQ(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr), SQLITE_OK);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (name != nullptr) {
            tables.emplace(name);
        }
    }
    sqlite3_finalize(statement);
    return tables;
}

std::set<std::string> list_columns(sqlite3* db, const char* table) {
    std::set<std::string> columns;
    sqlite3_stmt* statement = nullptr;
    const auto sql = std::string("PRAGMA table_info(") + table + ")";
    EXPECT_EQ(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr), SQLITE_OK);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name != nullptr) {
            columns.emplace(name);
        }
    }
    sqlite3_finalize(statement);
    return columns;
}

TEST(initialize_creates_the_documented_tables) {
    insightio::backend::SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    const auto tables = list_tables(store.db());
    EXPECT_TRUE(tables.contains("devices"));
    EXPECT_TRUE(tables.contains("streams"));
    EXPECT_TRUE(tables.contains("apps"));
    EXPECT_TRUE(tables.contains("app_routes"));
    EXPECT_TRUE(tables.contains("app_sources"));
    EXPECT_TRUE(tables.contains("sessions"));
    EXPECT_TRUE(tables.contains("session_logs"));
}

TEST(streams_table_uses_device_scoped_selector_uniqueness) {
    insightio::backend::SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    const auto columns = list_columns(store.db(), "streams");
    EXPECT_TRUE(columns.contains("selector"));
    EXPECT_TRUE(!columns.contains("selector_key"));
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "schema_store_test: " << tests().size() << " test(s) passed\n";
    return 0;
}
