// role: SQLite bootstrap implementation for the standalone insight-io backend.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: opens the database, enables safe SQLite pragmas, and applies
// the checked-in seven-table schema. See docs/past-tasks.md.

#include "insightio/backend/schema_store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace insightio::backend {

namespace {

std::string schema_path() {
#ifdef INSIGHTIO_SCHEMA_PATH
    return INSIGHTIO_SCHEMA_PATH;
#else
    return "backend/schema/001_initial.sql";
#endif
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

SchemaStore::SchemaStore(std::string db_path) : db_path_(std::move(db_path)) {}

SchemaStore::~SchemaStore() {
    close();
}

bool SchemaStore::open() {
    if (db_) {
        return true;
    }
    if (db_path_.empty()) {
        std::cerr << "[SchemaStore] database path is empty\n";
        return false;
    }

    std::error_code ec;
    const auto parent = std::filesystem::path(db_path_).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            std::cerr << "[SchemaStore] failed to create parent directory '"
                      << parent.string() << "': " << ec.message() << "\n";
            return false;
        }
    }

    const int rc = sqlite3_open_v2(
        db_path_.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[SchemaStore] sqlite3_open failed: "
                  << (db_ ? sqlite3_errmsg(db_) : "unknown") << "\n";
        close();
        return false;
    }

    return exec_sql("PRAGMA foreign_keys = ON;") &&
           exec_sql("PRAGMA busy_timeout = 5000;") &&
           exec_sql("PRAGMA journal_mode = WAL;") &&
           exec_sql("PRAGMA synchronous = NORMAL;");
}

bool SchemaStore::initialize() {
    return open() && apply_schema();
}

void SchemaStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SchemaStore::exec_sql(const std::string& sql) const {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc == SQLITE_OK) {
        return true;
    }
    std::cerr << "[SchemaStore] sqlite3_exec failed: "
              << (error ? error : "unknown") << "\n";
    sqlite3_free(error);
    return false;
}

bool SchemaStore::apply_schema() const {
    const auto sql = read_file(schema_path());
    if (sql.empty()) {
        std::cerr << "[SchemaStore] failed to read schema at "
                  << schema_path() << "\n";
        return false;
    }
    return exec_sql(sql);
}

}  // namespace insightio::backend
