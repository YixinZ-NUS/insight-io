#pragma once

// role: standalone HTTP surface for the bootstrap backend.
// revision: 2026-03-25 bootstrap-runtime-build
// major changes: exposes a versioned health endpoint over cpp-httplib.

#include "insightio/backend/catalog.hpp"
#include "insightio/backend/schema_store.hpp"

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
               std::string frontend_dir = "");
    ~RestServer();

    bool start(const std::string& host, uint16_t port);
    void stop();

private:
    SchemaStore& store_;
    CatalogService& catalog_;
    std::string frontend_dir_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void setup_routes();
};

}  // namespace insightio::backend
