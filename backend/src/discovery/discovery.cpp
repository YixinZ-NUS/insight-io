// role: aggregate host-device discovery for the standalone backend.
// revision: 2026-03-26 catalog-discovery-slice
// major changes: merges optional Orbbec and PipeWire discovery with V4L2 while
// skipping V4L2 nodes that belong to discovered Orbbec hardware.

#include "insightio/backend/discovery.hpp"

namespace insightio::backend {

DiscoveryResult discover_all() {
    DiscoveryResult result;
    std::set<std::string> skip_vendor_ids;

#ifdef INSIGHTIO_HAS_ORBBEC
    try {
        skip_vendor_ids = get_orbbec_vendor_ids();
        auto devices = discover_orbbec();
        result.devices.insert(result.devices.end(), devices.begin(), devices.end());
    } catch (const std::exception& error) {
        result.errors.push_back(std::string("Orbbec discovery failed: ") + error.what());
    }
#endif

    try {
        auto devices = discover_v4l2(skip_vendor_ids);
        result.devices.insert(result.devices.end(), devices.begin(), devices.end());
    } catch (const std::exception& error) {
        result.errors.push_back(std::string("V4L2 discovery failed: ") + error.what());
    }

#ifdef INSIGHTIO_HAS_PIPEWIRE
    try {
        auto devices = discover_pipewire();
        result.devices.insert(result.devices.end(), devices.begin(), devices.end());
    } catch (const std::exception& error) {
        result.errors.push_back(std::string("PipeWire discovery failed: ") + error.what());
    }
#endif

    return result;
}

}  // namespace insightio::backend
