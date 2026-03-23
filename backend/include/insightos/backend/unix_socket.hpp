#pragma once

#include "insightos/backend/result.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace insightos::backend::ipc {

struct SocketMessage {
    std::string payload;
    std::vector<int> fds;
};

Result<int> create_listen_socket(const std::string& path);
Result<int> accept_socket(int listen_fd);
Result<int> connect_socket(const std::string& path);

Result<void> send_message(int fd, const std::string& payload,
                          const std::vector<int>& fds);
Result<SocketMessage> recv_message(int fd, size_t max_payload, size_t max_fds);

}  // namespace insightos::backend::ipc
