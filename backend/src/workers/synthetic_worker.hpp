#pragma once

/// Synthetic capture worker used only by backend and SDK test binaries.
///
/// Production discovery never returns `test:` devices. SessionManager wires
/// this worker only for explicit `test:` URIs so integration tests can drive
/// deterministic multi-source app flows without live hardware.

#include "insightos/backend/worker.hpp"

namespace insightos::backend {

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
    std::chrono::milliseconds frame_period() const;
    std::vector<std::uint8_t> make_payload(const SyntheticStreamConfig& stream) const;

    SyntheticWorkerConfig cfg_;
    std::vector<std::vector<std::uint8_t>> payloads_;
    std::uint64_t sequence_{0};
};

}  // namespace insightos::backend
