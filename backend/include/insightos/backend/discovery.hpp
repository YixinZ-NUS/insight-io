#pragma once

/// InsightOS backend — Device discovery layer.
///
/// Probes the system for V4L2, PipeWire, and Orbbec devices and returns
/// DeviceInfo vectors.  Adapted from donor iocontroller device_pool.cpp,
/// pipewire_discovery.cpp, and orbbec_discovery.cpp (commit 4032eb4).

#include "insightos/backend/types.hpp"

#include <set>
#include <string>
#include <vector>

namespace insightos::backend {

// V4L2 discovery — always available on Linux.
std::vector<DeviceInfo> discover_v4l2(
    const std::set<std::string>& skip_vendor_ids = {});

// PipeWire discovery — conditional on INSIGHTOS_HAS_PIPEWIRE.
#ifdef INSIGHTOS_HAS_PIPEWIRE
std::vector<DeviceInfo> discover_pipewire();
#endif

// Orbbec discovery — conditional on INSIGHTOS_HAS_ORBBEC.
#ifdef INSIGHTOS_HAS_ORBBEC
std::vector<DeviceInfo> discover_orbbec();
std::set<std::string> get_orbbec_vendor_ids();
#endif

// Aggregate discovery — calls all available backends.
struct DiscoveryResult {
    std::vector<DeviceInfo> devices;
    std::vector<std::string> errors;
};

DiscoveryResult discover_all();

}  // namespace insightos::backend
