#pragma once

// role: PipeWire audio capture worker for task-7 local attach runtime.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor PipeWire input worker so audio sessions can
// publish PCM frames into the repo-native IPC runtime when available.
// See docs/past-tasks.md for verification history.

#ifdef INSIGHTIO_HAS_PIPEWIRE

#include "worker.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace insightio::backend {

struct PipeWireWorkerConfig {
    std::string name;
    uint32_t node_id{0};
    ResolvedCaps caps;
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

}  // namespace insightio::backend

#endif  // INSIGHTIO_HAS_PIPEWIRE
