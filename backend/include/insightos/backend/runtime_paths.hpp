#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace insightos::backend {

inline std::string default_runtime_root() {
    if (const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
        runtime_dir != nullptr && runtime_dir[0] != '\0') {
        return (std::filesystem::path(runtime_dir) / "insightos").string();
    }
    return "/tmp/insightos";
}

inline std::string default_state_root() {
    if (const char* state_dir = std::getenv("XDG_STATE_HOME");
        state_dir != nullptr && state_dir[0] != '\0') {
        return (std::filesystem::path(state_dir) / "insightos").string();
    }
    if (const char* home = std::getenv("HOME");
        home != nullptr && home[0] != '\0') {
        return (std::filesystem::path(home) / ".local" / "state" / "insightos")
            .string();
    }
    return (std::filesystem::path(default_runtime_root()) / "state").string();
}

inline std::string default_ipc_socket_path() {
    return (std::filesystem::path(default_runtime_root()) / "ipc.sock").string();
}

inline std::string default_database_path() {
    return (std::filesystem::path(default_state_root()) / "insightos.db").string();
}

}  // namespace insightos::backend
