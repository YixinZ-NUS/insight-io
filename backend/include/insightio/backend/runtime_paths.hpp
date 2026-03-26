#pragma once

// role: default filesystem locations for the standalone backend.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: provides a stable default SQLite database path.

#include <filesystem>
#include <string>

namespace insightio::backend {

inline std::string default_database_path() {
    return (std::filesystem::temp_directory_path() / "insight-io.sqlite3").string();
}

}  // namespace insightio::backend
