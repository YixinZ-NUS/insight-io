#pragma once

// role: host-device discovery surface for the standalone insight-io backend.
// revision: 2026-03-26 catalog-discovery-slice
// major changes: exposes V4L2, optional Orbbec, and optional PipeWire
// discovery used by the persisted catalog.

#include "insightio/backend/types.hpp"

#include <set>
#include <string>
#include <vector>

namespace insightio::backend {

std::vector<DeviceInfo> discover_v4l2(
    const std::set<std::string>& skip_vendor_ids = {});

#ifdef INSIGHTIO_HAS_ORBBEC
std::vector<DeviceInfo> discover_orbbec();
std::set<std::string> get_orbbec_vendor_ids();
#endif

#ifdef INSIGHTIO_HAS_PIPEWIRE
std::vector<DeviceInfo> discover_pipewire();
#endif

struct DiscoveryResult {
    std::vector<DeviceInfo> devices;
    std::vector<std::string> errors;
};

DiscoveryResult discover_all();

}  // namespace insightio::backend
