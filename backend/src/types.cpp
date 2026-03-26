// role: shared discovery and catalog type helpers for the standalone backend.
// revision: 2026-03-26 catalog-discovery-slice
// major changes: adds slug, stable identity, and caps naming helpers reused by
// discovery and persisted catalog generation.

#include "insightio/backend/types.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace insightio::backend {

namespace {

std::uint64_t fnv1a64(std::string_view input, std::uint64_t seed) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = seed;
    for (const unsigned char character : input) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= kPrime;
    }
    return hash;
}

std::string stable_identity_key(const DeviceIdentity& identity) {
    std::ostringstream stream;
    stream << identity.kind_str << '\n';

    if (!identity.persistent_key.empty()) {
        stream << identity.persistent_key;
        return stream.str();
    }

    if (!identity.usb_serial.empty()) {
        stream << identity.usb_vendor_id << '\n'
               << identity.usb_product_id << '\n'
               << identity.usb_serial;
        return stream.str();
    }

    stream << identity.usb_vendor_id << '\n'
           << identity.usb_product_id << '\n'
           << identity.device_id << '\n'
           << identity.device_uri;
    if (!identity.hardware_name.empty()) {
        stream << '\n' << identity.hardware_name;
    }
    return stream.str();
}

std::string hex_identifier(std::string_view prefix,
                           std::uint64_t high,
                           std::uint64_t low) {
    std::ostringstream stream;
    stream << prefix << std::hex << std::setfill('0')
           << std::setw(16) << high
           << std::setw(16) << low;
    return stream.str();
}

bool is_audio_format(std::string_view format) {
    return format == "s16le" || format == "s24le" || format == "s32le" ||
           format == "f32le" || format == "s16be" || format == "s24be" ||
           format == "s32be" || format == "f32be" || format == "u8";
}

}  // namespace

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
    std::string name = format + "_" + std::to_string(width) + "x" +
                       std::to_string(height);
    if (fps > 0) {
        name += "_" + std::to_string(fps);
    }
    return name;
}

std::string to_string(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::kV4l2:
            return "v4l2";
        case DeviceKind::kPipeWire:
            return "pipewire";
        case DeviceKind::kOrbbec:
            return "orbbec";
    }
    return "unknown";
}

std::string slugify(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    bool previous_dash = false;

    for (const char character : input) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            result += static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
            previous_dash = false;
        } else if (!previous_dash && !result.empty()) {
            result += '-';
            previous_dash = true;
        }
    }

    if (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result;
}

std::string stable_device_key(const DeviceIdentity& identity) {
    const auto key = stable_identity_key(identity);
    const std::uint64_t high = fnv1a64(key, 0xcbf29ce484222325ULL);
    const std::uint64_t low = fnv1a64(key, 0x9e3779b185ebca87ULL);
    return hex_identifier("dev_", high, low);
}

std::string stable_device_key(const DeviceInfo& device) {
    return stable_device_key(device.identity);
}

std::string public_device_id_base(const DeviceInfo& device, int fallback_index) {
    std::string base;
    const auto colon = device.name.find(':');
    if (colon != std::string::npos) {
        base = slugify(device.name.substr(0, colon));
    }
    if (base.empty()) {
        base = slugify(device.name);
    }
    if (base.empty()) {
        base = slugify(device.identity.hardware_name);
    }
    if (base.empty()) {
        base = slugify(device.identity.device_id);
    }
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

}  // namespace insightio::backend
