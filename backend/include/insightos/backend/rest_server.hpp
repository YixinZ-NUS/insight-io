#pragma once

/// InsightOS backend — REST API server.
///
/// Thin HTTP layer over SessionManager using cpp-httplib.
/// Provides JSON endpoints for catalog browsing, session lifecycle,
/// and status queries.
///
/// Donor reference: iocontroller/src/rest/rest_server.cpp (commit 4032eb4)
/// uses the same cpp-httplib library and JSON response pattern.

#include "insightos/backend/session.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace httplib { class Server; }

namespace insightos::backend {

class RestServer {
public:
    /// @param frontend_dir Path to directory containing frontend static files.
    explicit RestServer(SessionManager& mgr,
                        const std::string& frontend_dir = "");
    ~RestServer();

    /// Start listening; returns false on bind failure.
    bool start(const std::string& host, uint16_t port);

    /// Stop the server and join the listener thread.
    void stop();

private:
    SessionManager& mgr_;
    std::string frontend_dir_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void setup_routes();
};

}  // namespace insightos::backend
