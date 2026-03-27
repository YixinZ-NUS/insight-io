// role: local IPC attach probe for task-7 runtime verification.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: adds a small operator-facing probe that attaches to the unix
// control socket, receives memfd plus eventfd descriptors, and prints one
// delivered frame summary for the requested session and optional stream.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/ipc.hpp"
#include "insightio/backend/unix_socket.hpp"

#include <nlohmann/json.hpp>

#include <poll.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "usage: insightio_ipc_probe <socket> <session_id> [stream_name]\n";
        return 2;
    }

    const std::string socket_path = argv[1];
    const auto session_id = std::stoll(argv[2]);
    const std::string stream_name = argc > 3 ? argv[3] : "";

    auto connection = insightio::backend::ipc::connect_socket(socket_path);
    if (!connection.ok()) {
        std::cerr << connection.error().message << "\n";
        return 1;
    }
    const int control_fd = connection.value();

    nlohmann::json request = {
        {"session_id", session_id},
    };
    if (!stream_name.empty()) {
        request["stream_name"] = stream_name;
    }

    auto send = insightio::backend::ipc::send_message(control_fd, request.dump(), {});
    if (!send.ok()) {
        std::cerr << send.error().message << "\n";
        ::close(control_fd);
        return 1;
    }

    auto reply = insightio::backend::ipc::recv_message(control_fd, 4096, 2);
    if (!reply.ok()) {
        std::cerr << reply.error().message << "\n";
        ::close(control_fd);
        return 1;
    }

    const auto payload = nlohmann::json::parse(reply.value().payload);
    if (payload.value("status", std::string{"error"}) != "ok") {
        std::cout << payload.dump(2) << "\n";
        ::close(control_fd);
        return 1;
    }
    if (reply.value().fds.size() != 2) {
        std::cerr << "expected 2 fds, got " << reply.value().fds.size() << "\n";
        ::close(control_fd);
        return 1;
    }

    auto reader =
        insightio::backend::ipc::attach_reader(reply.value().fds[0], reply.value().fds[1]);
    if (!reader.ok()) {
        std::cerr << reader.error().message << "\n";
        ::close(control_fd);
        return 1;
    }

    for (int attempt = 0; attempt < 20; ++attempt) {
        pollfd descriptor{};
        descriptor.fd = reader.value()->event_fd();
        descriptor.events = POLLIN;
        const int polled = ::poll(&descriptor, 1, 500);
        if (polled < 0) {
            std::perror("poll");
            ::close(control_fd);
            return 1;
        }
        if ((descriptor.revents & POLLIN) != 0) {
            reader.value()->clear_event();
        }

        if (auto frame = reader.value()->read()) {
            nlohmann::json summary = {
                {"channel_id", payload.at("channel_id")},
                {"stream", payload.at("stream")},
                {"frame_size", frame->size},
                {"pts_ns", frame->pts_ns},
                {"flags", frame->flags},
                {"first_byte", frame->size > 0
                                   ? static_cast<int>(frame->data[0])
                                   : -1},
            };
            std::cout << summary.dump(2) << "\n";
            ::close(control_fd);
            return 0;
        }
    }

    std::cerr << "timed out waiting for frame\n";
    ::close(control_fd);
    return 1;
}
