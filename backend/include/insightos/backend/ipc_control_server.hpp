#pragma once

#include "insightos/backend/session.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace insightos::backend {

struct ActiveIpcConsumer {
    DeliveryKey key;
    int writer_eventfd{-1};
    int client_fd{-1};
};

class IpcConnectionTracker {
public:
    IpcConnectionTracker();
    ~IpcConnectionTracker();

    IpcConnectionTracker(const IpcConnectionTracker&) = delete;
    IpcConnectionTracker& operator=(const IpcConnectionTracker&) = delete;

    bool register_connection(const ActiveIpcConsumer& conn);
    void unregister_connection(int client_fd, SessionManager& mgr);
    void poll_disconnects(SessionManager& mgr);
    void cleanup_all(SessionManager& mgr);

private:
    void cleanup_connection(const ActiveIpcConsumer& conn, SessionManager& mgr);

    mutable std::mutex mutex_;
    int epoll_fd_{-1};
    std::unordered_map<int, ActiveIpcConsumer> connections_;
};

class IpcControlServer {
public:
    explicit IpcControlServer(SessionManager& mgr);
    ~IpcControlServer();

    IpcControlServer(const IpcControlServer&) = delete;
    IpcControlServer& operator=(const IpcControlServer&) = delete;

    bool start(const std::string& socket_path, std::string& err);
    void stop();
    bool is_running() const;
    const std::string& socket_path() const { return socket_path_; }

private:
    void accept_loop();
    bool handle_client(int client_fd);

    SessionManager& mgr_;
    int listen_fd_{-1};
    std::atomic_bool running_{false};
    std::atomic_bool stop_requested_{false};
    std::thread accept_thread_;
    std::string socket_path_;
    IpcConnectionTracker tracker_;
};

}  // namespace insightos::backend
