// role: standalone backend entrypoint for the current insight-io slices.
// revision: 2026-03-27 task8-rtsp-runtime-validation
// major changes: starts the SQLite-backed catalog, session, and durable
// app/route/source services while keeping media-runtime realization
// intentionally lightweight, and binds the aggregate discovery entrypoint
// through an overload-safe callback while surfacing the local IPC attach
// socket plus the mediamtx-compatible RTSP host contract. See
// docs/past-tasks.md.

#include "insightio/backend/app_service.hpp"
#include "insightio/backend/catalog.hpp"
#include "insightio/backend/discovery.hpp"
#include "insightio/backend/rest_server.hpp"
#include "insightio/backend/runtime_paths.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"
#include "insightio/backend/version.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running.store(false);
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace insightio::backend;

    std::string host = "127.0.0.1";
    uint16_t port = 18180;
    std::string frontend_dir;
    std::string db_path = default_database_path();
    std::string rtsp_host = "127.0.0.1";
    uint16_t rtsp_port = 8554;
    bool rtsp_port_overridden = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--host" && index + 1 < argc) {
            host = argv[++index];
        } else if (arg == "--port" && index + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++index]));
        } else if (arg == "--frontend" && index + 1 < argc) {
            frontend_dir = argv[++index];
        } else if (arg == "--db-path" && index + 1 < argc) {
            db_path = argv[++index];
        } else if (arg == "--rtsp-host" && index + 1 < argc) {
            rtsp_host = argv[++index];
            if (!rtsp_port_overridden) {
                const auto delimiter = rtsp_host.rfind(':');
                if (delimiter != std::string::npos && delimiter + 1 < rtsp_host.size()) {
                    try {
                        rtsp_port = static_cast<uint16_t>(
                            std::stoi(rtsp_host.substr(delimiter + 1)));
                        rtsp_host = rtsp_host.substr(0, delimiter);
                    } catch (...) {
                    }
                }
            }
        } else if (arg == "--rtsp-port" && index + 1 < argc) {
            rtsp_port = static_cast<uint16_t>(std::stoi(argv[++index]));
            rtsp_port_overridden = true;
        }
    }
    const std::string rtsp_endpoint = rtsp_host + ":" + std::to_string(rtsp_port);

    std::cout << "insightiod " << kVersion << "\n";

    try {
        SchemaStore store(db_path);
        if (!store.initialize()) {
            std::cerr << "Failed to initialize SQLite schema at " << db_path << "\n";
            return 1;
        }

        CatalogService catalog(
            store,
            []() { return discover_all(); },
            "localhost",
            rtsp_endpoint);
        if (!catalog.initialize()) {
            std::cerr << "Failed to initialize discovery catalog\n";
            return 1;
        }

        SessionService sessions(store, "localhost", rtsp_endpoint);
        if (!sessions.initialize()) {
            std::cerr << "Failed to initialize direct session service\n";
            return 1;
        }

        AppService apps(store, sessions, "localhost", rtsp_endpoint);
        if (!apps.initialize()) {
            std::cerr << "Failed to initialize durable app service\n";
            return 1;
        }

        RestServer server(store, catalog, sessions, apps, frontend_dir);
        if (!server.start(host, port)) {
            std::cerr << "Failed to start REST server on " << host << ":" << port << "\n";
            return 1;
        }

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        std::cout << "REST API listening on " << host << ":" << port << "\n";
        std::cout << "Device store: " << db_path << "\n";
        std::cout << "IPC attach socket: " << sessions.ipc_socket_path() << "\n";

        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        server.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Failed to initialize backend: " << error.what() << "\n";
        return 1;
    }
}
