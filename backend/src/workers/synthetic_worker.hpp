#pragma once

// role: synthetic capture worker for focused runtime and IPC tests.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: provides deterministic frame generation for `test:` device
// URIs so task-7 IPC attach can be verified without live hardware.
// See docs/past-tasks.md for verification history.

#include "worker.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace insightio::backend {

struct SyntheticStreamConfig {
    std::string stream_name;
    ResolvedCaps caps;
};

struct SyntheticWorkerConfig {
    std::string name;
    std::vector<SyntheticStreamConfig> streams;
};

class SyntheticWorker final : public CaptureWorker {
public:
    explicit SyntheticWorker(SyntheticWorkerConfig cfg);

protected:
    std::optional<std::string> setup() override;
    void run() override;
    void cleanup() override;

private:
    [[nodiscard]] std::chrono::milliseconds frame_period() const;
    [[nodiscard]] std::vector<std::uint8_t> make_payload(const SyntheticStreamConfig& stream) const;

    SyntheticWorkerConfig cfg_;
    std::vector<std::vector<std::uint8_t>> payloads_;
    std::uint64_t sequence_{0};
};

}  // namespace insightio::backend
