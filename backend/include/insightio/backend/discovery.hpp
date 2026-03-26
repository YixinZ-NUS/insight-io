#pragma once

// role: host-device discovery surface for the standalone insight-io backend.
// revision: 2026-03-26 orbbec-v4l2-fallback-fix
// major changes: exposes aggregate discovery hooks so tests can verify the
// Orbbec-to-V4L2 fallback boundary, and keeps duplicate suppression contingent
// on usable SDK-backed Orbbec discovery. See docs/past-tasks.md.

#include "insightio/backend/types.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace insightio::backend {

std::vector<DeviceInfo> discover_v4l2(
    const std::set<std::string>& skip_vendor_ids = {});

#ifdef INSIGHTIO_HAS_ORBBEC
std::vector<DeviceInfo> discover_orbbec();
#endif

#ifdef INSIGHTIO_HAS_PIPEWIRE
std::vector<DeviceInfo> discover_pipewire();
#endif

struct DiscoveryResult {
    std::vector<DeviceInfo> devices;
    std::vector<std::string> errors;
};

struct DiscoveryHooks {
    std::function<std::vector<DeviceInfo>()> discover_orbbec;
    std::function<std::vector<DeviceInfo>(const std::set<std::string>&)>
        discover_v4l2;
    std::function<std::vector<DeviceInfo>()> discover_pipewire;
};

DiscoveryResult discover_all();
DiscoveryResult discover_all(const DiscoveryHooks& hooks);

}  // namespace insightio::backend
