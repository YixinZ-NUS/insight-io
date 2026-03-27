#pragma once

// role: shared discovery and catalog types for the standalone insight-io backend.
// revision: 2026-03-26 catalog-discovery-slice
// major changes: introduces stable device identity helpers and raw discovery
// capability types for the persisted source catalog.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace insightio::backend {

enum class DeviceKind { kV4l2, kPipeWire, kOrbbec };
enum class DataKind { kFrame, kMessage };

struct ResolvedCaps {
    std::uint32_t index{0};
    std::string format;
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t fps{0};

    [[nodiscard]] bool is_audio() const;
    [[nodiscard]] std::uint32_t sample_rate() const;
    [[nodiscard]] std::uint32_t channels() const;
    [[nodiscard]] std::string to_named() const;
};

struct StreamInfo {
    std::string stream_id;
    std::string name;
    DataKind data_kind{DataKind::kFrame};
    std::vector<ResolvedCaps> supported_caps;
};

struct DeviceIdentity {
    std::string device_uri;
    std::string device_id;
    std::string kind_str;
    std::string hardware_name;
    std::string persistent_key;
    std::string usb_vendor_id;
    std::string usb_product_id;
    std::string usb_serial;
};

struct DeviceInfo {
    std::string uri;
    DeviceKind kind{DeviceKind::kV4l2};
    std::string name;
    std::vector<StreamInfo> streams;
    DeviceIdentity identity;
    std::string description;
};

[[nodiscard]] std::string to_string(DeviceKind kind);
[[nodiscard]] std::string slugify(std::string_view input);
[[nodiscard]] std::string stable_device_key(const DeviceIdentity& identity);
[[nodiscard]] std::string stable_device_key(const DeviceInfo& device);
[[nodiscard]] std::string public_device_id_base(const DeviceInfo& device,
                                                int fallback_index = 0);
[[nodiscard]] std::string default_stream_name(DeviceKind kind,
                                              std::string_view stream_id);
[[nodiscard]] bool is_audio_format(std::string_view format);
[[nodiscard]] bool is_compressed_audio(std::string_view format);
[[nodiscard]] bool is_compressed_video(std::string_view format);
[[nodiscard]] bool is_depth_format(std::string_view format);

}  // namespace insightio::backend
