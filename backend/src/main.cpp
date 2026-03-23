#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

#include "insightos/backend/ipc_control_server.hpp"
#include "insightos/backend/runtime_paths.hpp"
#include "insightos/backend/version.hpp"
#include "insightos/backend/session.hpp"
#include "insightos/backend/rest_server.hpp"

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running.store(false);
}

int main(int argc, char* const argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 18180;
    std::string frontend_dir;
    std::string ipc_socket_path = insightos::backend::default_ipc_socket_path();
    std::string db_path = insightos::backend::default_database_path();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc)
            host = argv[++i];
        else if (arg == "--port" && i + 1 < argc)
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--frontend" && i + 1 < argc)
            frontend_dir = argv[++i];
        else if (arg == "--ipc-socket" && i + 1 < argc)
            ipc_socket_path = argv[++i];
        else if (arg == "--db-path" && i + 1 < argc)
            db_path = argv[++i];
    }

    // Auto-detect frontend directory if not specified.
    if (frontend_dir.empty()) {
        std::error_code ec;
        auto bin_dir = std::filesystem::canonical("/proc/self/exe", ec).parent_path();
        if (!ec) {
            auto candidate = bin_dir / ".." / ".." / "frontend";
            if (std::filesystem::is_directory(candidate))
                frontend_dir = std::filesystem::canonical(candidate).string();
        }
    }

    std::cout << "insightosd " << insightos::backend::kVersion << "\n";

    try {
        insightos::backend::SessionManager mgr(db_path);
        if (!mgr.initialize()) {
            throw std::runtime_error(
                "failed to persist discovered devices into " + mgr.database_path());
        }
        mgr.set_ipc_socket_path(ipc_socket_path);

        std::cout << "Discovered " << mgr.devices().size() << " devices, "
                  << mgr.catalog().endpoints().size() << " catalog devices\n";

        for (const auto& dev : mgr.devices()) {
            std::cout << "  " << dev.uri << " [" << to_string(dev.kind) << "] "
                      << "'" << dev.name << "' "
                      << "default=" << dev.default_public_id
                      << " current=" << dev.public_id << " "
                      << dev.streams.size() << " streams\n";
            for (const auto& s : dev.streams) {
                std::cout << "    " << s.name << ": " << s.supported_caps.size() << " caps";
                if (!s.supported_caps.empty()) {
                    std::cout << " (first: " << s.supported_caps[0].to_named() << ")";
                }
                std::cout << "\n";
            }
        }
        for (const auto& ep : mgr.catalog().endpoints()) {
            std::cout << "  device " << ep.name << " -> " << ep.device_uri << "\n";
        }

        insightos::backend::RestServer api(mgr, frontend_dir);
        if (!api.start(host, port)) {
            std::cerr << "Failed to start REST server on "
                      << host << ":" << port << "\n";
            return 1;
        }
        std::cout << "REST API listening on " << host << ":" << port << "\n";
        if (!frontend_dir.empty())
            std::cout << "Frontend: " << frontend_dir << "\n";
        std::cout << "Device store: " << mgr.database_path() << "\n";

        insightos::backend::IpcControlServer ipc_server(mgr);
        std::string ipc_err;
        if (!ipc_server.start(ipc_socket_path, ipc_err)) {
            std::cerr << "Failed to start IPC control server on "
                      << ipc_socket_path << ": " << ipc_err << "\n";
            api.stop();
            return 1;
        }
        std::cout << "IPC control socket: " << ipc_socket_path << "\n";

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\nShutting down...\n";
        ipc_server.stop();
        api.stop();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to initialize backend: " << ex.what() << "\n";
        return 1;
    }
}
