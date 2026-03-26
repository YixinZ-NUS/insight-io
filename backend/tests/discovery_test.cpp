// role: focused aggregate-discovery tests for the standalone backend.
// revision: 2026-03-26 orbbec-v4l2-fallback-fix
// major changes: verifies duplicate Orbbec suppression only activates after
// usable SDK-backed Orbbec discovery and preserves V4L2 fallback when SDK
// discovery is empty or fails.

#include "insightio/backend/discovery.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

#define TEST(name)                                                         \
    void test_##name();                                                    \
    struct Register_##name {                                               \
        Register_##name() { tests().push_back({#name, test_##name}); }     \
    } register_##name;                                                     \
    void test_##name()

struct TestEntry {
    const char* name;
    void (*fn)();
};

std::vector<TestEntry>& tests() {
    static std::vector<TestEntry> entries;
    return entries;
}

#define EXPECT_TRUE(value)                                                 \
    do {                                                                   \
        if (!(value)) {                                                    \
            std::cerr << "FAIL at " << __FILE__ << ":" << __LINE__         \
                      << "\nexpected true\n";                              \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define EXPECT_EQ(actual, expected)                                        \
    do {                                                                   \
        if ((actual) != (expected)) {                                      \
            std::cerr << "FAIL at " << __FILE__ << ":" << __LINE__         \
                      << "\nexpected: " << (expected)                      \
                      << "\ngot:      " << (actual) << "\n";               \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

using namespace insightio::backend;

DeviceInfo make_orbbec_device() {
    DeviceInfo device;
    device.uri = "orbbec://ORB001";
    device.kind = DeviceKind::kOrbbec;
    device.name = "Desk RGBD";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "ORB001";
    device.identity.kind_str = "orbbec";
    device.identity.hardware_name = device.name;
    device.identity.usb_vendor_id = "2bc5";
    device.identity.usb_product_id = "0614";
    return device;
}

DeviceInfo make_v4l2_device() {
    DeviceInfo device;
    device.uri = "v4l2:/dev/video0";
    device.kind = DeviceKind::kV4l2;
    device.name = "Fallback Camera";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "video0";
    device.identity.kind_str = "v4l2";
    device.identity.hardware_name = device.name;
    device.identity.usb_vendor_id = "2bc5";
    return device;
}

TEST(orbbec_empty_discovery_keeps_v4l2_fallback_visible) {
    std::set<std::string> seen_skip_vendor_ids;

    DiscoveryHooks hooks;
    hooks.discover_orbbec = []() { return std::vector<DeviceInfo>{}; };
    hooks.discover_v4l2 = [&](const std::set<std::string>& skip_vendor_ids) {
        seen_skip_vendor_ids = skip_vendor_ids;
        return std::vector<DeviceInfo>{make_v4l2_device()};
    };

    const auto result = discover_all(hooks);
    EXPECT_TRUE(seen_skip_vendor_ids.empty());
    EXPECT_EQ(result.devices.size(), 1u);
    EXPECT_TRUE(result.devices[0].kind == DeviceKind::kV4l2);
    EXPECT_TRUE(result.errors.empty());
}

TEST(orbbec_discovery_failure_keeps_v4l2_fallback_visible) {
    std::set<std::string> seen_skip_vendor_ids;

    DiscoveryHooks hooks;
    hooks.discover_orbbec = []() -> std::vector<DeviceInfo> {
        throw std::runtime_error("probe failed");
    };
    hooks.discover_v4l2 = [&](const std::set<std::string>& skip_vendor_ids) {
        seen_skip_vendor_ids = skip_vendor_ids;
        return std::vector<DeviceInfo>{make_v4l2_device()};
    };

    const auto result = discover_all(hooks);
    EXPECT_TRUE(seen_skip_vendor_ids.empty());
    EXPECT_EQ(result.devices.size(), 1u);
    EXPECT_TRUE(result.devices[0].kind == DeviceKind::kV4l2);
    EXPECT_EQ(result.errors.size(), 1u);
}

TEST(usable_orbbec_discovery_enables_duplicate_v4l2_suppression) {
    std::set<std::string> seen_skip_vendor_ids;

    DiscoveryHooks hooks;
    hooks.discover_orbbec = []() {
        return std::vector<DeviceInfo>{make_orbbec_device()};
    };
    hooks.discover_v4l2 = [&](const std::set<std::string>& skip_vendor_ids) {
        seen_skip_vendor_ids = skip_vendor_ids;
        return std::vector<DeviceInfo>{};
    };

    const auto result = discover_all(hooks);
    EXPECT_TRUE(seen_skip_vendor_ids.contains("2bc5"));
    EXPECT_EQ(result.devices.size(), 1u);
    EXPECT_TRUE(result.devices[0].kind == DeviceKind::kOrbbec);
    EXPECT_TRUE(result.errors.empty());
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "discovery_test: " << tests().size() << " test(s) passed\n";
    return 0;
}
