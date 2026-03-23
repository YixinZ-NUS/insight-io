/// InsightOS backend — Aggregate device discovery.
///
/// Calls all available discovery backends (Orbbec, V4L2, PipeWire) and
/// merges results into a single DiscoveryResult.

#include "insightos/backend/discovery.hpp"

#include <stdexcept>

namespace insightos::backend {

DiscoveryResult discover_all() {
    DiscoveryResult result;
    std::set<std::string> skip_vids;

#ifdef INSIGHTOS_HAS_ORBBEC
    try {
        skip_vids = get_orbbec_vendor_ids();
        auto orbbec_devs = discover_orbbec();
        result.devices.insert(result.devices.end(), orbbec_devs.begin(),
                              orbbec_devs.end());
    } catch (const std::exception& e) {
        result.errors.push_back(std::string("Orbbec discovery failed: ") +
                                e.what());
    }
#endif

    try {
        auto v4l2_devs = discover_v4l2(skip_vids);
        result.devices.insert(result.devices.end(), v4l2_devs.begin(),
                              v4l2_devs.end());
    } catch (const std::exception& e) {
        result.errors.push_back(std::string("V4L2 discovery failed: ") +
                                e.what());
    }

#ifdef INSIGHTOS_HAS_PIPEWIRE
    try {
        auto pw_devs = discover_pipewire();
        result.devices.insert(result.devices.end(), pw_devs.begin(),
                              pw_devs.end());
    } catch (const std::exception& e) {
        result.errors.push_back(std::string("PipeWire discovery failed: ") +
                                e.what());
    }
#endif

    return result;
}

}  // namespace insightos::backend
