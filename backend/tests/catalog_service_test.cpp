// role: focused catalog service tests for the standalone backend.
// revision: 2026-03-27 live-orbbec-depth-catalog-followup
// major changes: verifies alias persistence, reviewed V4L2 selector naming,
// Orbbec selector shaping without a serial-specific allowlist, and the
// intentional omission of raw IR streams from the public v1 catalog contract.
// See docs/past-tasks.md for verification history.

#include "insightio/backend/catalog.hpp"
#include "insightio/backend/schema_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <unistd.h>
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

std::string make_temp_db_path() {
    static int counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("insight-io-catalog-test-" + std::to_string(::getpid()) +
                       "-" + std::to_string(counter++) + ".sqlite3");
    return path.string();
}

DeviceInfo make_webcam() {
    DeviceInfo device;
    device.uri = "v4l2:/dev/video0";
    device.kind = DeviceKind::kV4l2;
    device.name = "Web Camera";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "video0";
    device.identity.kind_str = "v4l2";
    device.identity.hardware_name = device.name;
    device.identity.usb_serial = "SN00009";

    StreamInfo stream;
    stream.stream_id = "image";
    stream.name = "frame";
    stream.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 1280, 720, 30});
    device.streams.push_back(std::move(stream));
    return device;
}

DeviceInfo make_orbbec() {
    DeviceInfo device;
    device.uri = "orbbec://ORB001";
    device.kind = DeviceKind::kOrbbec;
    device.name = "Desk RGBD";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "ORB001";
    device.identity.kind_str = "orbbec";
    device.identity.hardware_name = device.name;
    device.identity.usb_vendor_id = "2bc5";
    device.identity.usb_serial = "ORB001";

    StreamInfo color;
    color.stream_id = "color";
    color.name = "color";
    color.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 640, 480, 30});

    StreamInfo depth;
    depth.stream_id = "depth";
    depth.name = "depth";
    depth.supported_caps.push_back(ResolvedCaps{0, "y16", 640, 400, 30});

    device.streams.push_back(std::move(color));
    device.streams.push_back(std::move(depth));
    return device;
}

DeviceInfo make_color_only_orbbec() {
    DeviceInfo device;
    device.uri = "orbbec://COLOR001";
    device.kind = DeviceKind::kOrbbec;
    device.name = "Color Only RGBD";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "COLOR001";
    device.identity.kind_str = "orbbec";
    device.identity.hardware_name = device.name;
    device.identity.usb_vendor_id = "2bc5";
    device.identity.usb_serial = "COLOR001";

    StreamInfo color;
    color.stream_id = "color";
    color.name = "color";
    color.supported_caps.push_back(ResolvedCaps{0, "mjpeg", 640, 480, 30});

    device.streams.push_back(std::move(color));
    return device;
}

DeviceInfo make_orbbec_with_ir() {
    auto device = make_orbbec();

    StreamInfo ir;
    ir.stream_id = "ir";
    ir.name = "ir";
    ir.supported_caps.push_back(ResolvedCaps{0, "y16", 640, 400, 30});

    device.streams.push_back(std::move(ir));
    return device;
}

TEST(alias_persists_across_refresh) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_webcam()};
            return result;
        });
    EXPECT_TRUE(catalog.initialize());

    CatalogDevice updated;
    int error_status = 0;
    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(catalog.set_alias("web-camera",
                                  "front-camera",
                                  updated,
                                  error_status,
                                  error_code,
                                  error_message));
    EXPECT_EQ(updated.public_name, "front-camera");

    EXPECT_TRUE(catalog.refresh());
    const auto device = catalog.get_device("front-camera");
    EXPECT_TRUE(device.has_value());
    EXPECT_EQ(device->default_name, "web-camera");
}

TEST(v4l2_uses_plain_resolution_selectors) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_webcam()};
            return result;
        });
    EXPECT_TRUE(catalog.initialize());

    const auto device = catalog.get_device("web-camera");
    EXPECT_TRUE(device.has_value());
    EXPECT_EQ(device->sources.size(), 1u);
    EXPECT_EQ(device->sources[0].selector, "720p_30");
    EXPECT_EQ(device->sources[0].uri,
              "insightos://localhost/web-camera/720p_30");
}

TEST(orbbec_adds_aligned_depth_and_grouped_preset) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_orbbec()};
            return result;
        });
    EXPECT_TRUE(catalog.initialize());

    const auto device = catalog.get_device("desk-rgbd");
    EXPECT_TRUE(device.has_value());

    std::set<std::string> selectors;
    for (const auto& source : device->sources) {
        selectors.insert(source.selector);
    }
    EXPECT_TRUE(selectors.contains("orbbec/depth/400p_30"));
    EXPECT_TRUE(selectors.contains("orbbec/depth/480p_30"));
    EXPECT_TRUE(selectors.contains("orbbec/preset/480p_30"));
}

TEST(color_only_orbbec_does_not_publish_synthetic_depth_or_grouped_preset) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_color_only_orbbec()};
            return result;
        });
    EXPECT_TRUE(catalog.initialize());

    const auto device = catalog.get_device("color-only-rgbd");
    EXPECT_TRUE(device.has_value());

    std::set<std::string> selectors;
    for (const auto& source : device->sources) {
        selectors.insert(source.selector);
    }
    EXPECT_TRUE(selectors.contains("orbbec/color/480p_30"));
    EXPECT_TRUE(!selectors.contains("orbbec/depth/400p_30"));
    EXPECT_TRUE(!selectors.contains("orbbec/depth/480p_30"));
    EXPECT_TRUE(!selectors.contains("orbbec/preset/480p_30"));
}

TEST(orbbec_ir_streams_are_not_published_in_current_v1_catalog) {
    SchemaStore store(make_temp_db_path());
    EXPECT_TRUE(store.initialize());

    CatalogService catalog(
        store,
        []() {
            DiscoveryResult result;
            result.devices = {make_orbbec_with_ir()};
            return result;
        });
    EXPECT_TRUE(catalog.initialize());

    const auto device = catalog.get_device("desk-rgbd");
    EXPECT_TRUE(device.has_value());

    std::set<std::string> selectors;
    for (const auto& source : device->sources) {
        selectors.insert(source.selector);
    }
    EXPECT_TRUE(selectors.contains("orbbec/color/480p_30"));
    EXPECT_TRUE(selectors.contains("orbbec/depth/400p_30"));
    EXPECT_TRUE(!selectors.contains("orbbec/ir/400p_30"));
}

}  // namespace

int main() {
    for (const auto& test : tests()) {
        test.fn();
    }
    std::cout << "catalog_service_test: " << tests().size()
              << " test(s) passed\n";
    return 0;
}
