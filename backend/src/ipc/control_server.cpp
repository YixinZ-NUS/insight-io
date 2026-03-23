#include "insightos/backend/ipc_control_server.hpp"

#include "insightos/backend/unix_socket.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace insightos::backend {

IpcConnectionTracker::IpcConnectionTracker() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
}

IpcConnectionTracker::~IpcConnectionTracker() {
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
}

bool IpcConnectionTracker::register_connection(const ActiveIpcConsumer& conn) {
    const int fd = conn.client_fd;
    if (epoll_fd_ < 0) return false;

    std::lock_guard lock(mutex_);
    connections_[fd] = conn;

    epoll_event ev{};
    ev.events = EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    ev.data.fd = fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        connections_.erase(fd);
        return false;
    }
    return true;
}

void IpcConnectionTracker::unregister_connection(int client_fd,
                                                 SessionManager& mgr) {
    std::lock_guard lock(mutex_);
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) return;

    mgr.detach_ipc_consumer(it->second.key, it->second.writer_eventfd);
    if (epoll_fd_ >= 0) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    }
    connections_.erase(it);
}

void IpcConnectionTracker::poll_disconnects(SessionManager& mgr) {
    if (epoll_fd_ < 0) return;

    constexpr int kMaxEvents = 16;
    epoll_event events[kMaxEvents];
    const int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, 0);
    if (n <= 0) return;

    std::lock_guard lock(mutex_);
    for (int i = 0; i < n; ++i) {
        if ((events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) == 0) {
            continue;
        }

        auto it = connections_.find(events[i].data.fd);
        if (it == connections_.end()) continue;
        cleanup_connection(it->second, mgr);
        connections_.erase(it);
    }
}

void IpcConnectionTracker::cleanup_all(SessionManager& mgr) {
    std::lock_guard lock(mutex_);
    for (auto& [_, conn] : connections_) {
        cleanup_connection(conn, mgr);
    }
    connections_.clear();
}

void IpcConnectionTracker::cleanup_connection(const ActiveIpcConsumer& conn,
                                              SessionManager& mgr) {
    mgr.detach_ipc_consumer(conn.key, conn.writer_eventfd);
    if (epoll_fd_ >= 0) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn.client_fd, nullptr);
    }
    ::close(conn.client_fd);
}

IpcControlServer::IpcControlServer(SessionManager& mgr) : mgr_(mgr) {}

IpcControlServer::~IpcControlServer() {
    stop();
}

bool IpcControlServer::start(const std::string& socket_path, std::string& err) {
    if (running_.load()) return true;

    auto parent = std::filesystem::path(socket_path).parent_path();
    if (!parent.empty()) {
        std::error_code fs_err;
        std::filesystem::create_directories(parent, fs_err);
        if (fs_err) {
            err = fs_err.message();
            return false;
        }
    }

    auto result = ipc::create_listen_socket(socket_path);
    if (!result.ok()) {
        err = result.error().message;
        return false;
    }

    listen_fd_ = result.value();
    socket_path_ = socket_path;
    stop_requested_.store(false);
    running_.store(true);
    accept_thread_ = std::thread([this]() { accept_loop(); });
    return true;
}

void IpcControlServer::stop() {
    if (!running_.load()) return;

    stop_requested_.store(true);
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    tracker_.cleanup_all(mgr_);
    running_.store(false);

    if (!socket_path_.empty()) {
        ::unlink(socket_path_.c_str());
    }
}

bool IpcControlServer::is_running() const {
    return running_.load();
}

void IpcControlServer::accept_loop() {
    while (!stop_requested_.load()) {
        tracker_.poll_disconnects(mgr_);

        pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;

        const int ret = ::poll(&pfd, 1, 200);
        if (ret < 0) {
            if (errno == EINTR) continue;
            if (stop_requested_.load()) break;
            break;
        }
        if (ret == 0) continue;

        auto accept_res = ipc::accept_socket(listen_fd_);
        if (!accept_res.ok()) {
            if (accept_res.error().code == "socket_eagain") continue;
            if (stop_requested_.load()) break;
            continue;
        }

        const int client_fd = accept_res.value();
        if (!handle_client(client_fd)) {
            ::close(client_fd);
        }
    }
}

bool IpcControlServer::handle_client(int client_fd) {
    auto msg_res = ipc::recv_message(client_fd, 4096, 0);
    if (!msg_res.ok() || msg_res.value().payload.empty()) {
        return false;
    }

    nlohmann::json req;
    try {
        req = nlohmann::json::parse(msg_res.value().payload);
    } catch (const nlohmann::json::exception&) {
        nlohmann::json resp = {{"status", "error"},
                               {"error", "invalid JSON"}};
        ipc::send_message(client_fd, resp.dump(), {});
        return false;
    }

    const std::string session_id = req.value("session_id", "");
    const std::string stream_name = req.value("stream_name", "");
    if (session_id.empty()) {
        nlohmann::json resp = {{"status", "error"},
                               {"error", "missing session_id"}};
        ipc::send_message(client_fd, resp.dump(), {});
        return false;
    }

    auto lease_res = mgr_.attach_ipc_consumer(session_id, stream_name);
    if (!lease_res.ok()) {
        nlohmann::json resp = {
            {"status", "error"},
            {"error", lease_res.error().message},
            {"code", lease_res.error().code},
        };
        ipc::send_message(client_fd, resp.dump(), {});
        return false;
    }

    auto lease = std::move(lease_res.value());
    nlohmann::json stream = {
        {"stream_id", lease.stream_state.stream_id},
        {"stream_name", lease.stream_state.stream_name},
        {"promised_format", lease.stream_state.promised_format},
        {"actual_format", lease.stream_state.actual_format},
    };
    if (lease.stream_state.fps == 0 ||
        is_audio_format(lease.stream_state.actual_format) ||
        is_compressed_audio(lease.stream_state.actual_format)) {
        stream["sample_rate"] = lease.stream_state.sample_rate;
        stream["channels"] = lease.stream_state.channels;
    } else {
        stream["actual_width"] = lease.stream_state.actual_width;
        stream["actual_height"] = lease.stream_state.actual_height;
        stream["fps"] = lease.stream_state.fps;
    }

    nlohmann::json resp = {
        {"status", "ok"},
        {"channel_id", lease.channel_id},
        {"stream", std::move(stream)},
    };

    if (!tracker_.register_connection(
            ActiveIpcConsumer{lease.key, lease.writer_eventfd, client_fd})) {
        mgr_.detach_ipc_consumer(lease.key, lease.writer_eventfd);
        ::close(lease.memfd);
        ::close(lease.eventfd);
        nlohmann::json error = {
            {"status", "error"},
            {"code", "ipc_registration_failed"},
            {"error", "failed to track IPC consumer connection"},
        };
        ipc::send_message(client_fd, error.dump(), {});
        return false;
    }

    auto send_res = ipc::send_message(client_fd, resp.dump(),
                                      {lease.memfd, lease.eventfd});
    ::close(lease.memfd);
    ::close(lease.eventfd);

    if (!send_res.ok()) {
        tracker_.unregister_connection(client_fd, mgr_);
        return false;
    }
    return true;
}

}  // namespace insightos::backend
