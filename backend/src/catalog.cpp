/// InsightOS backend — Public device catalog implementation.
///
/// Builds a public device catalog from discovered devices and resolves
/// preset + delivery combinations into concrete stream capabilities.
///
/// Donor reference: replaces the per-device activate/configure path in
/// iocontroller (commit 4032eb4).  The donor resolved caps at activation
/// time with device-centric IDs; this layer resolves them through a
/// stable public device namespace instead.

#include "insightos/backend/catalog.hpp"
#include "insightos/backend/types.hpp"

#include <algorithm>
#include <array>

namespace insightos::backend {

// ─── Preset tables ──────────────────────────────────────────────────────

namespace {

std::vector<PresetSpec> v4l2_presets() {
    return {
        {.name = "720p_30",
         .width = 1280, .height = 720, .fps = 30,
         .orbbec_group = std::nullopt,
         .default_delivery = "hevc"},
        {.name = "1080p_30",
         .width = 1920, .height = 1080, .fps = 30,
         .orbbec_group = std::nullopt,
         .default_delivery = "hevc"},
        {.name = "4k_30",
         .width = 3840, .height = 2160, .fps = 30,
         .orbbec_group = std::nullopt,
         .default_delivery = "hevc"},
    };
}

std::vector<PresetSpec> pipewire_presets() {
    return {
        {.name = "mono",
         .channels = 1, .preferred_rate = 48000, .fallback_rate = 44100,
         .orbbec_group = std::nullopt,
         .default_delivery = "pcm"},
        {.name = "stereo",
         .channels = 2, .preferred_rate = 48000, .fallback_rate = 44100,
         .orbbec_group = std::nullopt,
         .default_delivery = "pcm"},
    };
}

std::vector<PresetSpec> orbbec_presets() {
    return {
        {.name = "480p_30",
         .orbbec_group = PresetSpec::OrbbecGroupSpec{
             .color_width = 640, .color_height = 480,
             .depth_width = 640, .depth_height = 400,
             .ir_width = 640, .ir_height = 400,
             .fps = 30,
             .d2c_default = D2CMode::kHardware},
         .default_delivery = "mjpeg"},
        {.name = "720p_30",
         .orbbec_group = PresetSpec::OrbbecGroupSpec{
             .color_width = 1280, .color_height = 720,
             .depth_width = 1280, .depth_height = 800,
             .ir_width = 1280, .ir_height = 800,
             .fps = 30,
             .d2c_default = D2CMode::kOff},
         .default_delivery = "mjpeg"},
    };
}

const std::vector<std::string> kV4l2Deliveries =
    {"mjpeg", "h264", "hevc", "rgb24", "rtsp"};

const std::vector<std::string> kPipeWireDeliveries =
    {"pcm", "aac"};

const std::vector<std::string> kOrbbecDeliveries =
    {"mjpeg", "rgb24", "rtsp"};

// V4L2 default delivery preference: cheapest passthrough first.
const std::array<std::string_view, 4> kV4l2FormatPreference =
    {"hevc", "h264", "mjpeg", "raw"};

// Map delivery name → ResolvedCaps format string for matching.
// "raw" maps to common uncompressed pixel formats.
bool format_matches_delivery(const std::string& format,
                             const std::string& delivery) {
    if (delivery == "mjpeg")  return format == "mjpeg";
    if (delivery == "h264")   return format == "h264";
    if (delivery == "hevc")   return format == "hevc";
    if (delivery == "rgb24")  return format == "rgb24" || format == "yuyv"
                                   || format == "nv12" || format == "nv21";
    if (delivery == "rtsp")   return true;  // any source format is publishable
    if (delivery == "pcm")    return format == "s16le" || format == "s32le"
                                   || format == "f32le";
    if (delivery == "aac")    return true;  // transcode from any PCM source
    if (delivery == "raw")    return format == "yuyv" || format == "rgb24"
                                   || format == "nv12" || format == "nv21";
    return false;
}

// Find a stream by canonical id in a device.
const StreamInfo* find_stream_by_id(const DeviceInfo& device,
                                    const std::string& stream_id) {
    for (auto& s : device.streams) {
        if (s.stream_id == stream_id) return &s;
    }
    return nullptr;
}

// Find the best cap matching a resolution/fps in a stream.
const ResolvedCaps* find_video_cap(const StreamInfo& stream,
                                   std::uint32_t w, std::uint32_t h,
                                   std::uint32_t fps,
                                   const std::string& preferred_format = "") {
    const ResolvedCaps* best = nullptr;
    for (auto& cap : stream.supported_caps) {
        if (cap.width == w && cap.height == h && cap.fps == fps) {
            if (!preferred_format.empty() && cap.format == preferred_format) {
                return &cap;  // exact match
            }
            if (!best) best = &cap;
        }
    }
    return best;
}

// Find best audio cap matching rate/channels in a stream.
const ResolvedCaps* find_audio_cap(const StreamInfo& stream,
                                   std::uint32_t rate,
                                   std::uint32_t channels) {
    for (auto& cap : stream.supported_caps) {
        if (cap.width == rate && cap.height == channels) {
            return &cap;
        }
    }
    return nullptr;
}

}  // namespace

// ─── generate_endpoint_id ───────────────────────────────────────────────

std::string EndpointCatalog::generate_endpoint_id(const DeviceInfo& device,
                                                  int index) {
    return public_device_id_base(device, index);
}

// ─── build_from_discovery ───────────────────────────────────────────────

void EndpointCatalog::build_from_discovery(
    const std::vector<DeviceInfo>& devices) {
    endpoints_.clear();

    // Track generated fallback IDs only when the DB did not populate
    // DeviceInfo::public_id yet.
    std::map<std::string, int> generated_id_counts;

    for (std::size_t i = 0; i < devices.size(); ++i) {
        auto& dev = devices[i];
        CatalogEndpoint ep;

        auto default_id = dev.default_public_id;
        if (default_id.empty()) {
            auto base_id = generate_endpoint_id(dev, static_cast<int>(i));
            auto& count = generated_id_counts[base_id];
            if (count > 0) {
                default_id = base_id + "-" + std::to_string(count);
            } else {
                default_id = base_id;
            }
            ++count;
        }

        ep.default_name = default_id;
        ep.name = dev.public_id.empty() ? default_id : dev.public_id;

        ep.device_key = dev.device_key.empty() ? stable_device_key(dev)
                                               : dev.device_key;
        ep.device_uuid = stable_device_uuid(dev);
        ep.device_kind = dev.kind;
        ep.device_uri = dev.uri;
        ep.hardware_name = dev.name.empty()
            ? dev.identity.hardware_name : dev.name;

        switch (dev.kind) {
            case DeviceKind::kV4l2:
                ep.presets = v4l2_presets();
                ep.supported_deliveries = kV4l2Deliveries;
                break;
            case DeviceKind::kPipeWire:
                ep.presets = pipewire_presets();
                ep.supported_deliveries = kPipeWireDeliveries;
                break;
            case DeviceKind::kOrbbec:
                ep.presets = orbbec_presets();
                ep.supported_deliveries = kOrbbecDeliveries;
                break;
        }

        endpoints_.push_back(std::move(ep));
    }
}

// ─── Accessors ──────────────────────────────────────────────────────────

const std::vector<CatalogEndpoint>& EndpointCatalog::endpoints() const {
    return endpoints_;
}

const CatalogEndpoint* EndpointCatalog::find_endpoint(
    std::string_view name) const {
    for (auto& ep : endpoints_) {
        if (ep.name == name) return &ep;
    }
    return nullptr;
}

const CatalogEndpoint* EndpointCatalog::find_by_key(
    std::string_view key) const {
    for (auto& ep : endpoints_) {
        if (ep.device_key == key) return &ep;
    }
    return nullptr;
}

const CatalogEndpoint* EndpointCatalog::find_by_uuid(
    std::string_view uuid) const {
    for (auto& ep : endpoints_) {
        if (ep.device_uuid == uuid) return &ep;
    }
    return nullptr;
}

// ─── resolve ────────────────────────────────────────────────────────────

std::variant<ResolvedSession, ResolutionError>
EndpointCatalog::resolve(const SessionRequest& request,
                         const std::vector<DeviceInfo>& devices) const {
    // 1. Find public device entry by human-readable id or stable UUID.
    const CatalogEndpoint* ep = nullptr;
    if (!request.selector.device_uuid.empty()) {
        ep = find_by_uuid(request.selector.device_uuid);
    }
    if (!ep && !request.selector.name.empty()) {
        ep = find_endpoint(request.selector.name);
    }
    if (!ep) {
        ResolutionError err;
        err.code = "device_not_found";
        if (!request.selector.device_uuid.empty()) {
            err.message =
                "No device with UUID '" + request.selector.device_uuid + "' in catalog";
        } else {
            err.message = "No device '" + request.selector.name + "' in catalog";
        }
        for (auto& e : endpoints_) {
            err.alternatives.push_back(e.name);
        }
        return err;
    }
    if (!request.selector.name.empty() && request.selector.name != ep->name) {
        ResolutionError err;
        err.code = "device_identity_mismatch";
        err.message = "name '" + request.selector.name +
                      "' does not match device_uuid '" +
                      request.selector.device_uuid + "'";
        err.alternatives.push_back(ep->name);
        return err;
    }
    if (!request.selector.device_uuid.empty() &&
        request.selector.device_uuid != ep->device_uuid) {
        ResolutionError err;
        err.code = "device_identity_mismatch";
        err.message = "device_uuid '" + request.selector.device_uuid +
                      "' does not match name '" + ep->name + "'";
        err.alternatives.push_back(ep->device_uuid);
        return err;
    }

    // 2. Find preset.
    const PresetSpec* preset = nullptr;
    for (auto& p : ep->presets) {
        if (p.name == request.preset_name) {
            preset = &p;
            break;
        }
    }
    if (!preset) {
        ResolutionError err;
        err.code = "preset_not_found";
        err.message = "No preset '" + request.preset_name +
                      "' for device '" + ep->name + "'";
        for (auto& p : ep->presets) {
            err.alternatives.push_back(p.name);
        }
        return err;
    }

    // 3. Determine delivery.
    std::string delivery = request.delivery_name.value_or("");
    if (delivery.empty()) {
        delivery = preset->default_delivery;
    } else {
        // Validate delivery is supported.
        auto it = std::find(ep->supported_deliveries.begin(),
                            ep->supported_deliveries.end(), delivery);
        if (it == ep->supported_deliveries.end()) {
            ResolutionError err;
            err.code = "delivery_not_supported";
            err.message = "Delivery '" + delivery +
                          "' not supported for device '" +
                          ep->name + "'";
            err.alternatives = ep->supported_deliveries;
            return err;
        }
    }

    // 4. Find the matching device.
    const DeviceInfo* device = nullptr;
    for (auto& d : devices) {
        if (d.uri == ep->device_uri) {
            device = &d;
            break;
        }
    }
    if (!device) {
        ResolutionError err;
        err.code = "device_offline";
        err.message = "Device '" + ep->device_uri +
                      "' for device '" + ep->name +
                      "' is not available";
        return err;
    }

    // 5. Delegate to type-specific resolver.
    std::optional<ResolvedSession> result;
    switch (ep->device_kind) {
        case DeviceKind::kV4l2:
            result = resolve_v4l2(*ep, *preset, delivery,
                                  request.overrides, *device);
            break;
        case DeviceKind::kPipeWire:
            result = resolve_pipewire(*ep, *preset, delivery,
                                      request.overrides, *device);
            break;
        case DeviceKind::kOrbbec:
            result = resolve_orbbec(*ep, *preset, delivery,
                                    request.overrides, *device);
            break;
    }

    if (!result) {
        ResolutionError err;
        err.code = "no_matching_caps";
        err.message = "No device capability matches preset '" +
                      preset->name + "' with delivery '" + delivery +
                      "' on '" + ep->name + "'";
        for (auto& p : ep->presets) {
            err.alternatives.push_back(p.name);
        }
        return err;
    }

    return *result;
}

// ─── resolve_v4l2 ───────────────────────────────────────────────────────

std::optional<ResolvedSession> EndpointCatalog::resolve_v4l2(
    const CatalogEndpoint& ep, const PresetSpec& preset,
    const std::string& delivery, const StreamOverrides& overrides,
    const DeviceInfo& device) const {

    auto* stream = find_stream_by_id(device, "image");
    if (!stream) return std::nullopt;

    std::uint32_t w = preset.width;
    std::uint32_t h = preset.height;
    std::uint32_t fps = preset.fps;

    // Determine the format to search for based on delivery.
    // For default delivery, walk preference order to find cheapest passthrough.
    std::string chosen_format;
    const ResolvedCaps* chosen_cap = nullptr;

    if (delivery == "rtsp" || delivery == "rgb24") {
        // Any source cap at the right resolution works.
        chosen_cap = find_video_cap(*stream, w, h, fps);
        if (chosen_cap) chosen_format = delivery;
    } else {
        // Try the requested delivery format directly.
        for (auto& cap : stream->supported_caps) {
            if (cap.width == w && cap.height == h && cap.fps == fps &&
                format_matches_delivery(cap.format, delivery)) {
                chosen_cap = &cap;
                chosen_format = delivery;
                break;
            }
        }

        // If no exact match and delivery is the preset default, walk the
        // preference chain to find any passthrough-capable format.
        if (!chosen_cap && delivery == preset.default_delivery) {
            for (auto pref : kV4l2FormatPreference) {
                for (auto& cap : stream->supported_caps) {
                    if (cap.width == w && cap.height == h && cap.fps == fps &&
                        format_matches_delivery(cap.format,
                                                std::string(pref))) {
                        chosen_cap = &cap;
                        chosen_format = std::string(pref);
                        break;
                    }
                }
                if (chosen_cap) break;
            }
        }
    }

    if (!chosen_cap) {
        if (overrides.must_match) return std::nullopt;
        // Fallback: take any cap at matching resolution.
        chosen_cap = find_video_cap(*stream, w, h, fps);
        if (!chosen_cap) return std::nullopt;
        chosen_format = delivery;
    }

    ResolvedSession session;
    session.name = ep.name;
    session.device_key = ep.device_key;
    session.device_uuid = ep.device_uuid;
    session.preset = preset.name;
    session.delivery = chosen_format;
    session.device_uri = ep.device_uri;
    session.device_kind = ep.device_kind;
    session.streams.push_back({
        .stream_id = stream->stream_id,
        .stream_name = stream->name,
        .chosen_caps = *chosen_cap,
        .promised_format = chosen_format,
    });
    return session;
}

// ─── resolve_pipewire ───────────────────────────────────────────────────

std::optional<ResolvedSession> EndpointCatalog::resolve_pipewire(
    const CatalogEndpoint& ep, const PresetSpec& preset,
    const std::string& delivery, const StreamOverrides& overrides,
    const DeviceInfo& device) const {

    auto* stream = find_stream_by_id(device, "audio");
    if (!stream) return std::nullopt;

    std::uint32_t channels = overrides.channels.value_or(preset.channels);
    std::uint32_t rate = overrides.audio_rate.value_or(preset.preferred_rate);

    // Try preferred rate first, then fallback.
    auto* cap = find_audio_cap(*stream, rate, channels);
    if (!cap && rate != preset.fallback_rate) {
        rate = preset.fallback_rate;
        cap = find_audio_cap(*stream, rate, channels);
    }

    if (!cap) {
        if (overrides.must_match) return std::nullopt;
        // Last resort: any cap with matching channel count.
        for (auto& c : stream->supported_caps) {
            if (c.height == channels) {
                cap = &c;
                break;
            }
        }
        if (!cap) return std::nullopt;
    }

    ResolvedSession session;
    session.name = ep.name;
    session.device_key = ep.device_key;
    session.device_uuid = ep.device_uuid;
    session.preset = preset.name;
    session.delivery = delivery;
    session.device_uri = ep.device_uri;
    session.device_kind = ep.device_kind;
    session.streams.push_back({
        .stream_id = stream->stream_id,
        .stream_name = stream->name,
        .chosen_caps = *cap,
        .promised_format = delivery,
    });
    return session;
}

// ─── resolve_orbbec ─────────────────────────────────────────────────────

std::optional<ResolvedSession> EndpointCatalog::resolve_orbbec(
    const CatalogEndpoint& ep, const PresetSpec& preset,
    const std::string& delivery, const StreamOverrides& overrides,
    const DeviceInfo& device) const {

    if (!preset.orbbec_group) return std::nullopt;
    const auto& grp = *preset.orbbec_group;

    auto* color_stream = find_stream_by_id(device, "color");
    auto* depth_stream = find_stream_by_id(device, "depth");
    auto* ir_stream = find_stream_by_id(device, "ir");
    if (!color_stream || !depth_stream) return std::nullopt;

    // Resolve color cap.
    auto* color_cap = find_video_cap(*color_stream,
                                     grp.color_width, grp.color_height,
                                     grp.fps);
    if (!color_cap && overrides.must_match) return std::nullopt;

    // Resolve depth cap.
    auto* depth_cap = find_video_cap(*depth_stream,
                                     grp.depth_width, grp.depth_height,
                                     grp.fps);
    if (!depth_cap && overrides.must_match) return std::nullopt;

    const ResolvedCaps* ir_cap = nullptr;
    if (ir_stream && grp.ir_width > 0 && grp.ir_height > 0) {
        ir_cap = find_video_cap(*ir_stream, grp.ir_width, grp.ir_height, grp.fps);
        if (!ir_cap && overrides.must_match) return std::nullopt;
    }

    if (!color_cap || !depth_cap) return std::nullopt;

    // Determine D2C mode: prefer override, then preset default.
    D2CMode d2c = grp.d2c_default;
    if (overrides.depth_alignment) {
        auto parsed = d2c_mode_from_string(*overrides.depth_alignment);
        if (parsed) d2c = *parsed;
    }

    ResolvedSession session;
    session.name = ep.name;
    session.device_key = ep.device_key;
    session.device_uuid = ep.device_uuid;
    session.preset = preset.name;
    session.delivery = delivery;
    session.device_uri = ep.device_uri;
    session.device_kind = ep.device_kind;
    session.d2c = d2c;
    session.streams.push_back({
        .stream_id = color_stream->stream_id,
        .stream_name = color_stream->name,
        .chosen_caps = *color_cap,
        .promised_format = delivery,
    });
    session.streams.push_back({
        .stream_id = depth_stream->stream_id,
        .stream_name = depth_stream->name,
        .chosen_caps = *depth_cap,
        .promised_format = delivery,
    });
    if (ir_cap) {
        session.streams.push_back({
            .stream_id = ir_stream->stream_id,
            .stream_name = ir_stream->name,
            .chosen_caps = *ir_cap,
            .promised_format = delivery,
        });
    }
    return session;
}

}  // namespace insightos::backend
