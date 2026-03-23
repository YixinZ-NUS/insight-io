#pragma once

/// InsightOS backend — Core types.
///
/// Adapted from donor iocontroller/types.hpp (commit 4032eb4).
/// Key changes from donor:
///   - Namespace: insightos::backend (was iocontroller)
///   - DeliveryMode replaces StreamMode; kPassthrough replaces kCompressed
///   - D2CMode replaces D2CAlignMode with clearer value names
///   - DeviceIdentity.kind_str is now a string field (was "kind")
///   - Removed kReconfiguring from DeviceState (handled at session layer)
///   - No fmt dependency — pure C++ standard library

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace insightos::backend {

// ─── Device classification ──────────────────────────────────────────────

enum class DeviceKind { kV4l2, kPipeWire, kOrbbec };

// ─── Stream data type ───────────────────────────────────────────────────

enum class DataKind { kFrame, kMessage };

// ─── Delivery mode (replaces donor StreamMode) ──────────────────────────

enum class DeliveryMode {
    kPassthrough,  // source-native format (replaces kCompressed)
    kRaw,          // decoded (rgb24, z16, pcm)
    kRtsp,         // publish via RTSP
};

// ─── Device lifecycle ───────────────────────────────────────────────────

enum class DeviceState {
    kDiscovered,
    kActivating,
    kActive,
    kHeld,
    kError,
};

// ─── D2C alignment for Orbbec ───────────────────────────────────────────

enum class D2CMode { kOff, kHardware, kSoftware };

// ─── Resolved capability (typed, not JSON) ──────────────────────────────

/// Canonical caps — the single internal representation.
/// All caps are resolved to this form at discovery time.
struct ResolvedCaps {
    std::uint32_t index{0};
    std::string format;       // "mjpeg", "yuyv", "h264", "y16", "s16le", etc.
    std::uint32_t width{0};   // video: pixels, audio: sample_rate
    std::uint32_t height{0};  // video: pixels, audio: channels
    std::uint32_t fps{0};     // video: framerate, audio: 0

    bool is_audio() const;
    std::uint32_t sample_rate() const;
    std::uint32_t channels() const;

    /// "mjpeg_640x480_30" or "s16le_48000x2"
    std::string to_named() const;

    /// Parse named caps string back to fields.
    static std::optional<ResolvedCaps> from_named(std::string_view named);
};

// ─── Stream info ────────────────────────────────────────────────────────

struct StreamInfo {
    std::string stream_id;  // canonical donor-facing id: "image", "color", ...
    std::string name;       // current public name: "frame", "color", ...
    DataKind data_kind{DataKind::kFrame};
    std::vector<ResolvedCaps> supported_caps;
};

// ─── Device identity ────────────────────────────────────────────────────

struct DeviceIdentity {
    std::string device_uri;     // "v4l2:/dev/video0"
    std::string device_id;      // "video0", "AY27552002M", "50"
    std::string kind_str;       // "v4l2", "orbbec", "pw"
    std::string hardware_name;  // from driver/SDK
    std::string persistent_key; // stable discovery identity when available
    std::string usb_vendor_id;
    std::string usb_product_id;
    std::string usb_serial;
};

// ─── Complete device descriptor ─────────────────────────────────────────

struct DeviceInfo {
    std::string device_key;  // opaque backend-owned identifier
    std::string default_public_id;  // discovered-name-derived default slug
    std::string public_id;   // current public URI / REST identifier
    std::string uri;  // "v4l2:/dev/video0", "orbbec://AY27552002M", "pw:50"
    DeviceKind kind;
    std::string name;
    std::vector<StreamInfo> streams;
    DeviceIdentity identity;
    DeviceState state{DeviceState::kDiscovered};
    std::string description;
};

// ─── String conversions ─────────────────────────────────────────────────

std::string to_string(DeviceKind k);
std::string to_string(DeliveryMode m);
std::string to_string(DeviceState s);
std::string to_string(D2CMode m);

std::optional<DeviceKind> device_kind_from_string(std::string_view s);
std::optional<DeliveryMode> delivery_mode_from_string(std::string_view s);
std::optional<D2CMode> d2c_mode_from_string(std::string_view s);

/// Best-effort stable, deterministic UUID for a discovered device.
/// Prefers hardware identifiers when discovery exposes them and falls back
/// to runtime-local identifiers only when no better identity exists.
std::string stable_device_key(const DeviceIdentity& identity);
std::string stable_device_key(const DeviceInfo& device);
std::string stable_device_uuid(const DeviceIdentity& identity);
std::string stable_device_uuid(const DeviceInfo& device);

/// Prefix-free public device id seed derived from discovery metadata.
/// The result is human-readable and collision-resistant enough to be combined
/// with a numeric suffix when multiple devices still resolve to the same base.
std::string public_device_id_base(const DeviceInfo& device,
                                  int fallback_index = 0);

/// Default public stream name for one canonical stream id on a device kind.
std::string default_stream_name(DeviceKind kind, std::string_view stream_id);

// ─── Format classification helpers (ported from donor types.hpp) ────────

inline bool is_audio_format(std::string_view fmt) {
    return fmt == "s16le" || fmt == "s24le" || fmt == "s32le" || fmt == "f32le" ||
           fmt == "s16be" || fmt == "s24be" || fmt == "s32be" || fmt == "f32be" ||
           fmt == "u8";
}

inline bool is_compressed_audio(std::string_view fmt) {
    return fmt == "aac" || fmt == "mp3" || fmt == "opus";
}

inline bool is_compressed_video(std::string_view fmt) {
    return fmt == "mjpeg" || fmt == "jpeg" || fmt == "h264" ||
           fmt == "h265" || fmt == "hevc";
}

inline bool is_depth_format(std::string_view fmt) {
    return fmt == "y16" || fmt == "z16" || fmt == "gray16";
}

inline bool is_mjpeg_format(std::string_view fmt) {
    return fmt == "mjpeg" || fmt == "jpeg";
}

// ─── Utility ────────────────────────────────────────────────────────────

/// Lowercase, replace non-alnum with '-', collapse multiple dashes.
std::string slugify(std::string_view input);

}  // namespace insightos::backend
