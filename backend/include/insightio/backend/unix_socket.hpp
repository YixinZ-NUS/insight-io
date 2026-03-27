#pragma once

// role: unix domain socket helpers for IPC descriptor passing.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor seqpacket control-socket helpers used to pass
// memfd and eventfd descriptors to local consumers.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/result.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace insightio::backend::ipc {

struct SocketMessage {
    std::string payload;
    std::vector<int> fds;
};

Result<int> create_listen_socket(const std::string& path);
Result<int> accept_socket(int listen_fd);
Result<int> connect_socket(const std::string& path);
Result<void> send_message(int fd, const std::string& payload, const std::vector<int>& fds);
Result<SocketMessage> recv_message(int fd, size_t max_payload, size_t max_fds);

}  // namespace insightio::backend::ipc
