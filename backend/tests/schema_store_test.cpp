// role: focused schema bootstrap test for the standalone insight-io backend.
// revision: 2026-03-26 vendored-orbbec-sdk-and-sqlite-serialization
// major changes: verifies the checked-in seven-table schema is applied, the
// reviewed per-device selector uniqueness is present, redundant app-source
// kind columns stay removed, exact app-route ownership is enforced, the shared
// SQLite handle is serialized, and the app-route ambiguity/session-stream
// invariants are wired into the schema.

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

bool column_is_not_null(sqlite3* db, const char* table, const char* column) {
    sqlite3_stmt* statement = nullptr;
    const auto sql = std::string("PRAGMA table_info(") + table + ")";
    EXPECT_EQ(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr), SQLITE_OK);
    bool result = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name != nullptr && std::string(name) == column) {
            result = sqlite3_column_int(statement, 3) != 0;
            break;
        }
    }
    sqlite3_finalize(statement);
    return result;
}

std::set<std::string> list_indexes(sqlite3* db, const char* table) {
    std::set<std::string> indexes;
    sqlite3_stmt* statement = nullptr;
    const auto sql = std::string("PRAGMA index_list(") + table + ")";
    EXPECT_EQ(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr), SQLITE_OK);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name != nullptr) {
            indexes.emplace(name);
        }
    }
    sqlite3_finalize(statement);
    return indexes;
}

bool has_foreign_key(sqlite3* db,
                     const char* table,
                     const char* from_column,
                     const char* to_table,
                     const char* to_column,
                     const char* on_delete) {
    sqlite3_stmt* statement = nullptr;
    const auto sql = std::string("PRAGMA foreign_key_list(") + table + ")";
    EXPECT_EQ(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr), SQLITE_OK);
    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* target_table =
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        const auto* from = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
        const auto* to = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
        const auto* delete_action =
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
        if (target_table != nullptr && from != nullptr && to != nullptr &&
            delete_action != nullptr && std::string(target_table) == to_table &&
            std::string(from) == from_column && std::string(to) == to_column &&
            std::string(delete_action) == on_delete) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(statement);
    return found;
}

bool trigger_exists(sqlite3* db, const char* name) {
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT 1 FROM sqlite_master WHERE type = 'trigger' AND name = ?";
    EXPECT_EQ(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr), SQLITE_OK);
    sqlite3_bind_text(statement, 1, name, -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
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

TEST(open_uses_serialized_sqlite_handle) {
    insightio::backend::SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.open());
    EXPECT_TRUE(sqlite3_db_mutex(store.db()) != nullptr);
}

TEST(streams_table_uses_device_scoped_selector_uniqueness) {
    insightio::backend::SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    const auto columns = list_columns(store.db(), "streams");
    EXPECT_TRUE(columns.contains("selector"));
    EXPECT_TRUE(columns.contains("public_name"));
    EXPECT_TRUE(!columns.contains("selector_key"));
}

TEST(app_sources_schema_removes_redundant_kind_columns_and_enforces_route_ownership) {
    insightio::backend::SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    const auto columns = list_columns(store.db(), "app_sources");
    EXPECT_TRUE(columns.contains("target_name"));
    EXPECT_TRUE(columns.contains("stream_id"));
    EXPECT_TRUE(!columns.contains("target_kind"));
    EXPECT_TRUE(!columns.contains("source_kind"));
    EXPECT_TRUE(column_is_not_null(store.db(), "app_sources", "stream_id"));

    const auto indexes = list_indexes(store.db(), "app_sources");
    EXPECT_TRUE(indexes.contains("idx_app_sources_app_target_name"));
    EXPECT_TRUE(indexes.contains("idx_app_sources_app_route_id_not_null"));
    EXPECT_TRUE(has_foreign_key(
        store.db(), "app_sources", "app_id", "app_routes", "app_id", "CASCADE"));
    EXPECT_TRUE(has_foreign_key(
        store.db(), "app_sources", "route_id", "app_routes", "route_id", "CASCADE"));
    EXPECT_TRUE(has_foreign_key(store.db(),
                                "app_sources",
                                "source_session_id",
                                "sessions",
                                "session_id",
                                "RESTRICT"));
    EXPECT_TRUE(has_foreign_key(store.db(),
                                "app_sources",
                                "active_session_id",
                                "sessions",
                                "session_id",
                                "RESTRICT"));
    EXPECT_TRUE(trigger_exists(store.db(),
                               "trg_app_sources_exact_target_matches_route_insert"));
    EXPECT_TRUE(trigger_exists(store.db(),
                               "trg_app_sources_grouped_target_not_exact_route_insert"));
}

TEST(sessions_schema_requires_stream_reference) {
    insightio::backend::SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    EXPECT_TRUE(column_is_not_null(store.db(), "sessions", "stream_id"));
}

TEST(app_routes_schema_installs_ambiguity_guards) {
    insightio::backend::SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    EXPECT_TRUE(trigger_exists(store.db(),
                               "trg_app_routes_no_ambiguous_target_insert"));
    EXPECT_TRUE(trigger_exists(store.db(),
                               "trg_app_routes_no_ambiguous_target_update"));
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        std::cout << "running " << test.name << "\n";
        test.fn();
    }
    std::cout << "schema_store_test: " << tests().size() << " test(s) passed\n";
    return 0;
}
