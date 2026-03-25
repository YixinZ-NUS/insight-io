// role: standalone backend entrypoint for the bootstrap insight-io slice.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: starts the SQLite-backed health server and keeps process
// lifecycle minimal while later features are reintroduced incrementally.

#include "insightio/backend/rest_server.hpp"
#include "insightio/backend/runtime_paths.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/version.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

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
        }
    }

    std::cout << "insightiod " << kVersion << "\n";

    try {
        SchemaStore store(db_path);
        if (!store.initialize()) {
            std::cerr << "Failed to initialize SQLite schema at " << db_path << "\n";
            return 1;
        }

        RestServer server(store, frontend_dir);
        if (!server.start(host, port)) {
            std::cerr << "Failed to start REST server on " << host << ":" << port << "\n";
            return 1;
        }

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        std::cout << "REST API listening on " << host << ":" << port << "\n";
        std::cout << "Device store: " << db_path << "\n";

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
