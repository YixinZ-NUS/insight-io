#pragma once

// role: standalone HTTP surface for the current backend slices.
// revision: 2026-03-26 direct-session-slice
// major changes: exposes catalog, direct-session, and runtime-status endpoints
// over cpp-httplib.

#include "insightio/backend/catalog.hpp"
#include "insightio/backend/schema_store.hpp"
#include "insightio/backend/session_service.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace insightio::backend {

class RestServer {
public:
    RestServer(SchemaStore& store,
               CatalogService& catalog,
               SessionService& sessions,
               std::string frontend_dir = "");
    ~RestServer();

    bool start(const std::string& host, uint16_t port);
    void stop();

private:
    SchemaStore& store_;
    CatalogService& catalog_;
    SessionService& sessions_;
    std::string frontend_dir_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void setup_routes();
};

}  // namespace insightio::backend
