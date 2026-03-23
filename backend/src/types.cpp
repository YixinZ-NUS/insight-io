/// InsightOS backend — Core type implementations.
///
/// Adapted from donor iocontroller/types.hpp (commit 4032eb4).
/// from_named / to_named logic mirrors the donor's ResolvedCaps but uses
/// std::to_string and std::from_chars instead of fmt::format.

#include "insightos/backend/types.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace insightos::backend {

namespace {

std::uint64_t fnv1a64(std::string_view input, std::uint64_t seed) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = seed;
    for (unsigned char c : input) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::string stable_identity_key(const DeviceIdentity& identity) {
    std::ostringstream oss;
    oss << identity.kind_str << '\n';

    if (!identity.persistent_key.empty()) {
        oss << identity.persistent_key;
        return oss.str();
    }

    if (!identity.usb_serial.empty()) {
        oss << identity.usb_vendor_id << '\n'
            << identity.usb_product_id << '\n'
            << identity.usb_serial;
        return oss.str();
    }

    oss << identity.usb_vendor_id << '\n'
        << identity.usb_product_id << '\n'
        << identity.device_id << '\n'
        << identity.device_uri;
    if (!identity.hardware_name.empty()) {
        oss << '\n' << identity.hardware_name;
    }
    return oss.str();
}

std::string hex_identifier(std::string_view prefix,
                           std::uint64_t hi, std::uint64_t lo) {
    std::ostringstream oss;
    oss << prefix << std::hex << std::setfill('0')
        << std::setw(16) << hi
        << std::setw(16) << lo;
    return oss.str();
}

}  // namespace

// ─── ResolvedCaps ───────────────────────────────────────────────────────

bool ResolvedCaps::is_audio() const {
    return fps == 0 && is_audio_format(format);
}

std::uint32_t ResolvedCaps::sample_rate() const {
    return is_audio() ? width : 0;
}

std::uint32_t ResolvedCaps::channels() const {
    return is_audio() ? height : 0;
}

std::string ResolvedCaps::to_named() const {
    // Video: "mjpeg_640x480_30"   Audio: "s16le_48000x2"
    std::string result = format + "_" +
                         std::to_string(width) + "x" +
                         std::to_string(height);
    if (fps > 0) {
        result += "_" + std::to_string(fps);
    }
    return result;
}

std::optional<ResolvedCaps> ResolvedCaps::from_named(std::string_view named) {
    // Expected: "format_WxH_fps" or "format_WxH"
    auto underscore = named.find('_');
    if (underscore == std::string_view::npos || underscore == 0)
        return std::nullopt;

    auto x_pos = named.find('x', underscore + 1);
    if (x_pos == std::string_view::npos)
        return std::nullopt;

    auto fps_sep = named.find('_', x_pos + 1);

    auto parse_u32 = [](std::string_view sv) -> std::optional<std::uint32_t> {
        std::uint32_t val = 0;
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
        if (ec != std::errc{} || ptr != sv.data() + sv.size())
            return std::nullopt;
        return val;
    };

    ResolvedCaps rc;
    rc.format = std::string(named.substr(0, underscore));

    auto w = parse_u32(named.substr(underscore + 1, x_pos - underscore - 1));
    if (!w) return std::nullopt;
    rc.width = *w;

    if (fps_sep != std::string_view::npos) {
        auto h = parse_u32(named.substr(x_pos + 1, fps_sep - x_pos - 1));
        if (!h) return std::nullopt;
        rc.height = *h;

        auto f = parse_u32(named.substr(fps_sep + 1));
        if (!f) return std::nullopt;
        rc.fps = *f;
    } else {
        auto h = parse_u32(named.substr(x_pos + 1));
        if (!h) return std::nullopt;
        rc.height = *h;
    }

    if (rc.width == 0 || rc.height == 0) return std::nullopt;
    return rc;
}

// ─── to_string conversions ──────────────────────────────────────────────

std::string to_string(DeviceKind k) {
    switch (k) {
        case DeviceKind::kV4l2:     return "v4l2";
        case DeviceKind::kPipeWire: return "pw";
        case DeviceKind::kOrbbec:   return "orbbec";
    }
    return "unknown";
}

std::string to_string(DeliveryMode m) {
    switch (m) {
        case DeliveryMode::kPassthrough: return "passthrough";
        case DeliveryMode::kRaw:         return "raw";
        case DeliveryMode::kRtsp:        return "rtsp";
    }
    return "unknown";
}

std::string to_string(DeviceState s) {
    switch (s) {
        case DeviceState::kDiscovered:  return "discovered";
        case DeviceState::kActivating:  return "activating";
        case DeviceState::kActive:      return "active";
        case DeviceState::kHeld:        return "held";
        case DeviceState::kError:       return "error";
    }
    return "unknown";
}

std::string to_string(D2CMode m) {
    switch (m) {
        case D2CMode::kOff:      return "off";
        case D2CMode::kHardware: return "hw";
        case D2CMode::kSoftware: return "sw";
    }
    return "off";
}

// ─── from_string conversions ────────────────────────────────────────────

std::optional<DeviceKind> device_kind_from_string(std::string_view s) {
    if (s == "v4l2")                  return DeviceKind::kV4l2;
    if (s == "pw" || s == "pipewire") return DeviceKind::kPipeWire;
    if (s == "orbbec")                return DeviceKind::kOrbbec;
    return std::nullopt;
}

std::optional<DeliveryMode> delivery_mode_from_string(std::string_view s) {
    if (s == "passthrough") return DeliveryMode::kPassthrough;
    if (s == "raw")         return DeliveryMode::kRaw;
    if (s == "rtsp")        return DeliveryMode::kRtsp;
    return std::nullopt;
}

std::optional<D2CMode> d2c_mode_from_string(std::string_view s) {
    if (s == "off") return D2CMode::kOff;
    if (s == "hw")  return D2CMode::kHardware;
    if (s == "sw")  return D2CMode::kSoftware;
    return std::nullopt;
}

// ─── Utility ────────────────────────────────────────────────────────────

std::string slugify(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    bool prev_dash = false;

    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            prev_dash = false;
        } else {
            if (!prev_dash && !result.empty()) {
                result += '-';
                prev_dash = true;
            }
        }
    }

    // Trim trailing dash
    if (!result.empty() && result.back() == '-') {
        result.pop_back();
    }

    return result;
}

std::string stable_device_uuid(const DeviceIdentity& identity) {
    const auto key = stable_identity_key(identity);
    const std::uint64_t hi = fnv1a64(key, 14695981039346656037ULL);
    const std::uint64_t lo = fnv1a64(key, 1099511628211ULL);

    std::array<std::uint8_t, 16> bytes{};
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::uint8_t>((hi >> ((7 - i) * 8)) & 0xffU);
        bytes[8 + i] = static_cast<std::uint8_t>((lo >> ((7 - i) * 8)) & 0xffU);
    }

    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x50U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            oss << '-';
        }
    }
    return oss.str();
}

std::string stable_device_key(const DeviceIdentity& identity) {
    const auto key = stable_identity_key(identity);
    const std::uint64_t hi = fnv1a64(key, 0xcbf29ce484222325ULL);
    const std::uint64_t lo = fnv1a64(key, 0x9e3779b185ebca87ULL);
    return hex_identifier("dev_", hi, lo);
}

std::string stable_device_key(const DeviceInfo& device) {
    return stable_device_key(device.identity);
}

std::string stable_device_uuid(const DeviceInfo& device) {
    return stable_device_uuid(device.identity);
}

std::string public_device_id_base(const DeviceInfo& device, int fallback_index) {
    std::string base = slugify(device.name);
    if (base.empty()) base = slugify(device.identity.hardware_name);
    if (base.empty()) base = slugify(device.identity.device_id);
    if (!base.empty()) {
        return base;
    }

    return fallback_index > 0 ? "device-" + std::to_string(fallback_index)
                              : "device";
}

std::string default_stream_name(DeviceKind kind, std::string_view stream_id) {
    if (kind == DeviceKind::kV4l2 && stream_id == "image") {
        return "frame";
    }
    return std::string(stream_id);
}

}  // namespace insightos::backend
