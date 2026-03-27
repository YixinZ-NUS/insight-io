#pragma once

// role: Orbbec capture worker for task-7 grouped and exact local attach.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor Orbbec pipeline worker into repo-native
// runtime form so exact and grouped RGBD sessions can publish into IPC.
// See docs/past-tasks.md for verification history.

#ifdef INSIGHTIO_HAS_ORBBEC

#include "worker.hpp"

#include <string>
#include <vector>

namespace insightio::backend {

enum class D2CMode { kOff, kHardware, kSoftware };

struct OrbbecStreamConfig {
    std::string name;
    ResolvedCaps caps;
};

struct OrbbecWorkerConfig {
    std::string name;
    std::string uri;
    std::vector<OrbbecStreamConfig> streams;
    D2CMode d2c{D2CMode::kOff};
};

class OrbbecWorker final : public CaptureWorker {
public:
    explicit OrbbecWorker(OrbbecWorkerConfig cfg);

protected:
    std::optional<std::string> setup() override;
    void run() override;
    void cleanup() override;

private:
    OrbbecWorkerConfig cfg_;
};

}  // namespace insightio::backend

#endif  // INSIGHTIO_HAS_ORBBEC
