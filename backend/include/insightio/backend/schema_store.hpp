#pragma once

// role: lightweight SQLite bootstrap layer for the route-based backend.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: opens the database and applies the checked-in seven-table
// schema from backend/schema/001_initial.sql.

#include <sqlite3.h>

#include <string>

namespace insightio::backend {

class SchemaStore {
public:
    explicit SchemaStore(std::string db_path);
    ~SchemaStore();

    SchemaStore(const SchemaStore&) = delete;
    SchemaStore& operator=(const SchemaStore&) = delete;

    bool open();
    bool initialize();
    void close();

    [[nodiscard]] sqlite3* db() const { return db_; }
    [[nodiscard]] const std::string& database_path() const { return db_path_; }

private:
    std::string db_path_;
    sqlite3* db_{nullptr};

    bool exec_sql(const std::string& sql) const;
    bool apply_schema() const;
};

}  // namespace insightio::backend
