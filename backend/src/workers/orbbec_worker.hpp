#pragma once

/// InsightOS backend — Orbbec depth-camera capture worker.
///
/// Ported from donor src/workers/orbbec_capture_worker.hpp (commit 4032eb4).
/// Conditionally compiled when INSIGHTOS_HAS_ORBBEC is defined.
/// Uses callback-based frame delivery. Supports multi-stream (color/depth/IR).

#ifdef INSIGHTOS_HAS_ORBBEC

#include "insightos/backend/types.hpp"
#include "insightos/backend/worker.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace insightos::backend {

struct OrbbecStreamConfig {
    std::string name;       // "color", "depth", "ir"
    ResolvedCaps caps;
};

struct OrbbecWorkerConfig {
    std::string name;       // Worker name prefix
    std::string uri;        // "orbbec://SERIAL" or "orbbec://0"
    std::vector<OrbbecStreamConfig> streams;
    D2CMode d2c{D2CMode::kOff};
};

class OrbbecWorker final : public CaptureWorker {
public:
    explicit OrbbecWorker(OrbbecWorkerConfig cfg);

    /// Request adding streams to a running worker. Thread-safe.
    void request_add_streams(std::vector<OrbbecStreamConfig> new_streams);

    /// Update D2C alignment mode. Thread-safe.
    void set_d2c(D2CMode mode);

    /// Query active stream names. Thread-safe.
    std::vector<std::string> active_stream_names() const;

protected:
    std::optional<std::string> setup() override;
    void run() override;
    void cleanup() override;

private:
    OrbbecWorkerConfig cfg_;
    mutable std::mutex pending_mutex_;
    std::vector<OrbbecStreamConfig> pending_streams_;
};

}  // namespace insightos::backend

#endif  // INSIGHTOS_HAS_ORBBEC
