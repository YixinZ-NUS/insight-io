// role: unix seqpacket helper implementation for task-7 IPC attach.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor unix-socket FD-passing helpers into the
// standalone insight-io backend namespace.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/unix_socket.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace insightio::backend::ipc {

namespace {

Result<int> make_socket() {
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return Result<int>::err(
            {"socket_error", std::string("socket: ") + std::strerror(errno)});
    }
    return Result<int>::ok(fd);
}

Result<sockaddr_un> make_sockaddr(const std::string& path) {
    sockaddr_un addr {};
    if (path.size() >= sizeof(addr.sun_path)) {
        return Result<sockaddr_un>::err(
            {"socket_error", "unix socket path too long: " + path});
    }

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return Result<sockaddr_un>::ok(addr);
}

}  // namespace

Result<int> create_listen_socket(const std::string& path) {
    auto fd_res = make_socket();
    if (!fd_res.ok()) {
        return fd_res;
    }

    const int fd = fd_res.value();
    ::unlink(path.c_str());

    auto addr_res = make_sockaddr(path);
    if (!addr_res.ok()) {
        ::close(fd);
        return Result<int>::err(addr_res.error());
    }
    auto addr = addr_res.value();

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return Result<int>::err(
            {"socket_error", std::string("bind: ") + std::strerror(errno)});
    }
    if (::listen(fd, 16) < 0) {
        ::close(fd);
        return Result<int>::err(
            {"socket_error", std::string("listen: ") + std::strerror(errno)});
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return Result<int>::ok(fd);
}

Result<int> accept_socket(int listen_fd) {
    int fd = -1;
    do {
        fd = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Result<int>::err(
                {"socket_eagain", std::string("accept: ") + std::strerror(errno)});
        }
        return Result<int>::err(
            {"socket_error", std::string("accept: ") + std::strerror(errno)});
    }
    return Result<int>::ok(fd);
}

Result<int> connect_socket(const std::string& path) {
    auto fd_res = make_socket();
    if (!fd_res.ok()) {
        return fd_res;
    }

    const int fd = fd_res.value();
    auto addr_res = make_sockaddr(path);
    if (!addr_res.ok()) {
        ::close(fd);
        return Result<int>::err(addr_res.error());
    }
    auto addr = addr_res.value();

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return Result<int>::err(
            {"socket_error", std::string("connect: ") + std::strerror(errno)});
    }
    return Result<int>::ok(fd);
}

Result<void> send_message(int fd, const std::string& payload, const std::vector<int>& fds) {
    if (payload.size() > 65535) {
        return Result<void>::err({"socket_error", "payload too large"});
    }

    const uint32_t length = static_cast<uint32_t>(payload.size());
    std::string buffer;
    buffer.resize(sizeof(length) + payload.size());
    std::memcpy(buffer.data(), &length, sizeof(length));
    if (!payload.empty()) {
        std::memcpy(buffer.data() + sizeof(length), payload.data(), payload.size());
    }

    iovec iov {};
    iov.iov_base = buffer.data();
    iov.iov_len = buffer.size();

    msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    std::vector<char> control;
    if (!fds.empty()) {
        control.resize(CMSG_SPACE(sizeof(int) * fds.size()));
        msg.msg_control = control.data();
        msg.msg_controllen = control.size();

        auto* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
        std::memcpy(CMSG_DATA(cmsg), fds.data(), sizeof(int) * fds.size());
    }

    const ssize_t sent = ::sendmsg(fd, &msg, 0);
    if (sent < 0) {
        return Result<void>::err(
            {"socket_error", std::string("sendmsg: ") + std::strerror(errno)});
    }
    return Result<void>::success();
}

Result<SocketMessage> recv_message(int fd, size_t max_payload, size_t max_fds) {
    SocketMessage message;

    std::string buffer;
    buffer.resize(sizeof(uint32_t) + max_payload);

    std::vector<char> control;
    if (max_fds > 0) {
        control.resize(CMSG_SPACE(sizeof(int) * max_fds));
    }

    iovec iov {};
    iov.iov_base = buffer.data();
    iov.iov_len = buffer.size();

    msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (!control.empty()) {
        msg.msg_control = control.data();
        msg.msg_controllen = control.size();
    }

    const ssize_t received = ::recvmsg(fd, &msg, 0);
    if (received < 0) {
        return Result<SocketMessage>::err(
            {"socket_error", std::string("recvmsg: ") + std::strerror(errno)});
    }
    if (received == 0) {
        return Result<SocketMessage>::err(
            {"socket_error", "recvmsg: EOF (peer closed connection)"});
    }
    if ((msg.msg_flags & MSG_TRUNC) != 0) {
        return Result<SocketMessage>::err(
            {"socket_error", "recvmsg: payload truncated (MSG_TRUNC)"});
    }
    if ((msg.msg_flags & MSG_CTRUNC) != 0) {
        return Result<SocketMessage>::err(
            {"socket_error", "recvmsg: control data truncated (MSG_CTRUNC)"});
    }
    if (static_cast<size_t>(received) < sizeof(uint32_t)) {
        return Result<SocketMessage>::err({"socket_error", "message too short"});
    }

    uint32_t length = 0;
    std::memcpy(&length, buffer.data(), sizeof(length));
    if (length > max_payload) {
        return Result<SocketMessage>::err(
            {"socket_error", "payload exceeds max size"});
    }
    if (sizeof(uint32_t) + length > static_cast<size_t>(received)) {
        return Result<SocketMessage>::err({"socket_error", "payload truncated"});
    }

    if (length > 0) {
        message.payload.assign(buffer.data() + sizeof(length), length);
    }

    for (auto* cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const int* recv_fds = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (size_t index = 0; index < count; ++index) {
            message.fds.push_back(recv_fds[index]);
        }
    }

    return Result<SocketMessage>::ok(std::move(message));
}

}  // namespace insightio::backend::ipc
