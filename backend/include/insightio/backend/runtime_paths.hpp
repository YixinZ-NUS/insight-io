#pragma once

// role: default filesystem locations for the standalone backend.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: provides stable default SQLite and local IPC socket paths.

#include <atomic>
#include <filesystem>
#include <functional>
#include <string_view>
#include <string>

#include <unistd.h>

namespace insightio::backend {

inline std::string default_database_path() {
    return (std::filesystem::temp_directory_path() / "insight-io.sqlite3").string();
}

inline std::string default_ipc_socket_path(std::string_view seed = {}) {
    static std::atomic_uint64_t counter{0};
    const auto hash = std::hash<std::string_view>{}(seed);
    return (std::filesystem::temp_directory_path() /
            ("insight-io-ipc-" + std::to_string(::getpid()) + "-" +
             std::to_string(hash) + "-" +
             std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) +
             ".sock"))
        .string();
}

}  // namespace insightio::backend
