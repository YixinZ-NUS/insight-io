/// InsightOS backend — EndpointCatalog unit tests.

#include "insightos/backend/catalog.hpp"
#include "insightos/backend/types.hpp"

#include <cstdlib>
#include <iostream>
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

#define EXPECT_EQ(a, b)                                                    \
    do {                                                                   \
        if ((a) != (b)) {                                                  \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected: " << (b)                         \
                      << "\n    got:      " << (a) << "\n";                \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define EXPECT_TRUE(x)                                                     \
    do {                                                                   \
        if (!(x)) {                                                        \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected true\n";                          \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

using namespace insightos::backend;

DeviceInfo make_camera(std::string public_id = "web-camera") {
    DeviceInfo device;
    device.device_key = "dev_camera_001";
    device.public_id = std::move(public_id);
    device.uri = "v4l2:/dev/video0";
    device.kind = DeviceKind::kV4l2;
    device.name = "Web Camera";
    device.identity.device_uri = device.uri;
    device.identity.device_id = "video0";
    device.identity.kind_str = "v4l2";
    device.identity.hardware_name = device.name;
    device.identity.usb_serial = "SN001";

    StreamInfo image;
    image.stream_id = "image";
    image.name = "frame";
    image.data_kind = DataKind::kFrame;
    image.supported_caps.push_back(
        ResolvedCaps{0, "mjpeg", 1280, 720, 30});
    image.supported_caps.push_back(
        ResolvedCaps{1, "h264", 1280, 720, 30});
    device.streams.push_back(std::move(image));
    return device;
}

TEST(build_uses_stored_public_id_and_device_key) {
    EndpointCatalog catalog;
    auto device = make_camera();
    device.default_public_id = "web-camera";
    catalog.build_from_discovery({device});

    const auto* endpoint = catalog.find_endpoint("web-camera");
    EXPECT_TRUE(endpoint != nullptr);
    EXPECT_EQ(endpoint->name, "web-camera");
    EXPECT_EQ(endpoint->default_name, "web-camera");
    EXPECT_EQ(endpoint->device_key, "dev_camera_001");
}

TEST(build_exposes_default_device_id_when_public_id_is_aliased) {
    EndpointCatalog catalog;
    auto device = make_camera("front-camera");
    device.default_public_id = "web-camera";
    catalog.build_from_discovery({device});

    const auto* endpoint = catalog.find_endpoint("front-camera");
    EXPECT_TRUE(endpoint != nullptr);
    EXPECT_EQ(endpoint->name, "front-camera");
    EXPECT_EQ(endpoint->default_name, "web-camera");
}

TEST(resolve_rejects_legacy_prefixed_device_ids) {
    EndpointCatalog catalog;
    auto device = make_camera();
    catalog.build_from_discovery({device});

    StreamRequest request;
    request.selector.name = "cam-web-camera";
    request.preset_name = "720p_30";
    request.delivery_name = "mjpeg";

    auto result = catalog.resolve(request, {device});
    auto* error = std::get_if<ResolutionError>(&result);
    EXPECT_TRUE(error != nullptr);
    EXPECT_EQ(error->code, "device_not_found");
}

TEST(resolve_requires_exact_device_and_uuid_match) {
    EndpointCatalog catalog;
    auto device = make_camera();
    catalog.build_from_discovery({device});

    StreamRequest request;
    request.selector.name = "other-device";
    request.selector.device_uuid = stable_device_uuid(device);
    request.preset_name = "720p_30";
    request.delivery_name = "mjpeg";

    auto result = catalog.resolve(request, {device});
    auto* error = std::get_if<ResolutionError>(&result);
    EXPECT_TRUE(error != nullptr);
    EXPECT_EQ(error->code, "device_identity_mismatch");
}

TEST(stable_identity_ignores_hardware_name_with_persistent_key) {
    DeviceIdentity lhs;
    lhs.kind_str = "v4l2";
    lhs.persistent_key = "pci-0000:03:00.0-usb-0:2.1:1.0";
    lhs.hardware_name = "Web Camera";

    DeviceIdentity rhs = lhs;
    rhs.hardware_name = "USB Camera HD";

    EXPECT_EQ(stable_device_key(lhs), stable_device_key(rhs));
    EXPECT_EQ(stable_device_uuid(lhs), stable_device_uuid(rhs));
}

TEST(stable_identity_ignores_hardware_name_with_usb_serial) {
    DeviceIdentity lhs;
    lhs.kind_str = "orbbec";
    lhs.usb_vendor_id = "2bc5";
    lhs.usb_product_id = "060f";
    lhs.usb_serial = "AY27552002M";
    lhs.hardware_name = "SV1301S_U3";

    DeviceIdentity rhs = lhs;
    rhs.hardware_name = "Orbbec Camera";

    EXPECT_EQ(stable_device_key(lhs), stable_device_key(rhs));
    EXPECT_EQ(stable_device_uuid(lhs), stable_device_uuid(rhs));
}

}  // namespace

int main() {
    int passed = 0;
    for (const auto& test : tests()) {
        std::cout << "  " << test.name << " ... ";
        test.fn();
        std::cout << "ok\n";
        ++passed;
    }
    std::cout << "\n" << passed << " tests passed.\n";
    return 0;
}
