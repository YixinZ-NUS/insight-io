// role: bootstrap REST server implementation for the standalone insight-io backend.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: exposes the first runtime-tested HTTP surface, GET /api/health.

#include "insightio/backend/rest_server.hpp"

#include "insightio/backend/version.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace insightio::backend {

RestServer::RestServer(SchemaStore& store, std::string frontend_dir)
    : store_(store), frontend_dir_(std::move(frontend_dir)) {}

RestServer::~RestServer() {
    stop();
}

bool RestServer::start(const std::string& host, uint16_t port) {
    if (running_.load()) {
        return true;
    }

    server_ = std::make_unique<httplib::Server>();
    setup_routes();

    if (!server_->bind_to_port(host, port)) {
        server_.reset();
        return false;
    }

    running_.store(true);
    thread_ = std::thread([this]() {
        server_->listen_after_bind();
        running_.store(false);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return running_.load();
}

void RestServer::stop() {
    if (server_) {
        server_->stop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    server_.reset();
    running_.store(false);
}

void RestServer::setup_routes() {
    server_->Get("/api/health", [this](const httplib::Request&, httplib::Response& response) {
        nlohmann::json body = {
            {"status", "ok"},
            {"version", kVersion},
            {"db_path", store_.database_path()},
            {"frontend_path", frontend_dir_},
        };
        response.set_content(body.dump(2), "application/json");
    });
}

}  // namespace insightio::backend
