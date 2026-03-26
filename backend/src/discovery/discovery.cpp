// role: aggregate host-device discovery for the standalone backend.
// revision: 2026-03-26 catalog-discovery-slice
// major changes: merges optional Orbbec and PipeWire discovery with V4L2 while
// only suppressing duplicate Orbbec V4L2 nodes after usable SDK-backed Orbbec
// devices were actually discovered.

#include "insightio/backend/discovery.hpp"

namespace insightio::backend {

namespace {

std::set<std::string> skip_vendor_ids_for(const std::vector<DeviceInfo>& devices) {
    std::set<std::string> skip_vendor_ids;
    for (const auto& device : devices) {
        if (!device.identity.usb_vendor_id.empty()) {
            skip_vendor_ids.insert(device.identity.usb_vendor_id);
        }
    }
    return skip_vendor_ids;
}

}  // namespace

DiscoveryResult discover_all(const DiscoveryHooks& hooks) {
    DiscoveryResult result;
    std::set<std::string> skip_vendor_ids;

    if (hooks.discover_orbbec) {
        try {
            auto devices = hooks.discover_orbbec();
            if (!devices.empty()) {
                skip_vendor_ids = skip_vendor_ids_for(devices);
            }
            result.devices.insert(result.devices.end(), devices.begin(), devices.end());
        } catch (const std::exception& error) {
            result.errors.push_back(std::string("Orbbec discovery failed: ") + error.what());
        }
    }

    if (hooks.discover_v4l2) {
        try {
            auto devices = hooks.discover_v4l2(skip_vendor_ids);
            result.devices.insert(result.devices.end(), devices.begin(), devices.end());
        } catch (const std::exception& error) {
            result.errors.push_back(std::string("V4L2 discovery failed: ") + error.what());
        }
    }

    if (hooks.discover_pipewire) {
        try {
            auto devices = hooks.discover_pipewire();
            result.devices.insert(result.devices.end(), devices.begin(), devices.end());
        } catch (const std::exception& error) {
            result.errors.push_back(std::string("PipeWire discovery failed: ") + error.what());
        }
    }

    return result;
}

DiscoveryResult discover_all() {
    DiscoveryHooks hooks;
#ifdef INSIGHTIO_HAS_ORBBEC
    hooks.discover_orbbec = discover_orbbec;
#endif
    hooks.discover_v4l2 = discover_v4l2;
#ifdef INSIGHTIO_HAS_PIPEWIRE
    hooks.discover_pipewire = discover_pipewire;
#endif
    return discover_all(hooks);
}

}  // namespace insightio::backend
