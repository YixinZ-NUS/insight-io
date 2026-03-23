#pragma once

/// InsightOS backend — PipeWire audio capture worker.
///
/// Ported from donor src/workers/pipewire_capture_worker.hpp (commit 4032eb4).
/// Conditionally compiled when INSIGHTOS_HAS_PIPEWIRE is defined.
/// Uses callback-based frame delivery.

#ifdef INSIGHTOS_HAS_PIPEWIRE

#include "insightos/backend/types.hpp"
#include "insightos/backend/worker.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace insightos::backend {

struct PipeWireWorkerConfig {
    std::string name;       // Worker/stream name
    uint32_t node_id{0};    // PipeWire node ID
    ResolvedCaps caps;      // format in .format, sample_rate in .width, channels in .height
};

class PipeWireWorker final : public CaptureWorker {
public:
    explicit PipeWireWorker(PipeWireWorkerConfig cfg);
    ~PipeWireWorker() override;

protected:
    std::optional<std::string> setup() override;
    void run() override;
    void cleanup() override;

private:
    struct PipeWireCapture;

    PipeWireWorkerConfig cfg_;
    std::unique_ptr<PipeWireCapture> capture_;
};

}  // namespace insightos::backend

#endif  // INSIGHTOS_HAS_PIPEWIRE
