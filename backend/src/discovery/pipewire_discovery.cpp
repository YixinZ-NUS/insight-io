/// InsightOS backend — PipeWire audio device discovery.
///
/// Ported from donor src/pool/pipewire_discovery.cpp (commit 4032eb4).
///
/// Key changes from donor:
///   - Free function discover_pipewire() instead of DevicePool member
///   - Namespace insightos::backend (was iocontroller)
///   - Uses types.hpp DeviceIdentity.kind_str (was .kind)
///   - No spdlog / env.hpp dependency
///   - Preserves event-loop pattern for registry enumeration

#ifdef INSIGHTOS_HAS_PIPEWIRE

#include "insightos/backend/discovery.hpp"

#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <pipewire/pipewire.h>
#pragma GCC diagnostic pop

namespace insightos::backend {
namespace {

// ─── PipeWire discovery context ─────────────────────────────────────────

struct PwDiscoveryContext {
    std::mutex mutex;
    std::unordered_map<uint32_t, DeviceInfo> devices;
    std::atomic<bool> done{false};
    pw_main_loop* loop{nullptr};
    pw_context* context{nullptr};
    pw_core* core{nullptr};
    pw_registry* registry{nullptr};
    spa_hook registry_listener{};
    spa_hook core_listener{};
    int sync_seq{0};
};

// ─── Format helpers ─────────────────────────────────────────────────────

std::string pw_format_to_name(const char* format) {
    if (!format) return "unknown";
    std::string f(format);
    if (f == "S16LE" || f == "S16_LE") return "s16le";
    if (f == "S24LE" || f == "S24_LE") return "s24le";
    if (f == "S32LE" || f == "S32_LE") return "s32le";
    if (f == "F32LE" || f == "F32_LE") return "f32le";
    if (f == "S16BE" || f == "S16_BE") return "s16be";
    if (f == "S24BE" || f == "S24_BE") return "s24be";
    if (f == "S32BE" || f == "S32_BE") return "s32be";
    if (f == "F32BE" || f == "F32_BE") return "f32be";
    if (f == "U8") return "u8";
    for (auto& c : f) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return f;
}

bool is_audio_source(const char* media_class) {
    if (!media_class) return false;
    return (std::strstr(media_class, "Audio/Source") != nullptr ||
            std::strstr(media_class, "Audio/Duplex") != nullptr);
}

std::string pipewire_persistent_key(const spa_dict* props) {
    if (!props) return {};

    const char* candidates[] = {
        spa_dict_lookup(props, PW_KEY_DEVICE_SERIAL),
        spa_dict_lookup(props, PW_KEY_DEVICE_BUS_PATH),
        spa_dict_lookup(props, PW_KEY_OBJECT_PATH),
        spa_dict_lookup(props, PW_KEY_DEVICE_NAME),
        spa_dict_lookup(props, PW_KEY_NODE_NAME),
    };

    for (const char* candidate : candidates) {
        if (candidate && candidate[0] != '\0') {
            return candidate;
        }
    }
    return {};
}

/// Formats that the capture worker supports.
static const std::unordered_set<std::string> kSupportedPwFormats = {
    "s16le", "s24le", "s32le", "f32le"};

/// Build ResolvedCaps from PipeWire node properties.
/// Audio caps: width = sample_rate, height = channels, fps = 0.
std::vector<ResolvedCaps> build_caps_pipewire(const spa_dict* props) {
    std::vector<std::string> formats = {"s16le", "s24le", "s32le", "f32le"};
    std::vector<std::uint32_t> sample_rates = {44100, 48000};
    std::vector<std::uint32_t> channels = {1, 2};

    if (props) {
        if (const char* format = spa_dict_lookup(props, "audio.format")) {
            std::string fmt = pw_format_to_name(format);
            if (kSupportedPwFormats.count(fmt)) {
                formats = {fmt};
            }
        }
        if (const char* rate = spa_dict_lookup(props, "audio.rate")) {
            try {
                sample_rates = {static_cast<std::uint32_t>(std::stoul(rate))};
            } catch (...) {
            }
        }
        if (const char* ch = spa_dict_lookup(props, "audio.channels")) {
            try {
                channels = {static_cast<std::uint32_t>(std::stoul(ch))};
            } catch (...) {
            }
        }
    }

    std::vector<ResolvedCaps> caps_list;
    std::uint32_t cap_index = 0;
    for (const auto& fmt : formats) {
        for (auto rate : sample_rates) {
            for (auto ch : channels) {
                ResolvedCaps rc;
                rc.index = cap_index++;
                rc.format = fmt;
                rc.width = rate;   // sample_rate stored in width
                rc.height = ch;    // channels stored in height
                rc.fps = 0;        // audio has no fps
                caps_list.push_back(std::move(rc));
            }
        }
    }
    return caps_list;
}

// ─── Registry callbacks ─────────────────────────────────────────────────

void on_registry_global(void* data, uint32_t id, uint32_t /*permissions*/,
                        const char* type, uint32_t /*version*/,
                        const struct spa_dict* props) {
    auto* ctx = static_cast<PwDiscoveryContext*>(data);

    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
    if (!props) return;

    const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!media_class) return;
    if (!is_audio_source(media_class)) return;

    const char* description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    if (!description) description = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    if (!description) description = "Audio Device";

    auto caps_list = build_caps_pipewire(props);
    StreamInfo stream;
    stream.stream_id = "audio";
    stream.name = default_stream_name(DeviceKind::kPipeWire, stream.stream_id);
    stream.data_kind = DataKind::kFrame;
    stream.supported_caps = std::move(caps_list);

    DeviceInfo dev;
    dev.uri = "pw:" + std::to_string(id);
    dev.kind = DeviceKind::kPipeWire;
    dev.name = description;
    dev.state = DeviceState::kDiscovered;
    dev.streams = {std::move(stream)};

    dev.identity.device_uri = dev.uri;
    dev.identity.device_id = std::to_string(id);
    dev.identity.kind_str = "pw";
    dev.identity.hardware_name = dev.name;
    dev.identity.persistent_key = pipewire_persistent_key(props);
    if (const char* device_serial = spa_dict_lookup(props, PW_KEY_DEVICE_SERIAL);
        device_serial && device_serial[0] != '\0') {
        dev.identity.usb_serial = device_serial;
    }
    dev.description = dev.name + " (pw-" + dev.identity.device_id + ")";

    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->devices[id] = std::move(dev);
}

void on_registry_global_remove(void* data, uint32_t id) {
    auto* ctx = static_cast<PwDiscoveryContext*>(data);
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->devices.erase(id);
}

const pw_registry_events registry_events = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
};

// ─── Core callbacks ─────────────────────────────────────────────────────

void on_core_done(void* data, uint32_t id, int seq) {
    auto* ctx = static_cast<PwDiscoveryContext*>(data);
    if (id == PW_ID_CORE && seq == ctx->sync_seq) {
        ctx->done.store(true);
        pw_main_loop_quit(ctx->loop);
    }
}

void on_core_error(void* /*data*/, uint32_t /*id*/, int /*seq*/, int /*res*/,
                   const char* /*message*/) {
    // Silently ignore — errors are not fatal for discovery
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const pw_core_events core_events = {
    .version = PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
    .error = on_core_error,
};
#pragma GCC diagnostic pop

}  // namespace

// ─── Public entry point ─────────────────────────────────────────────────

std::vector<DeviceInfo> discover_pipewire() {
    std::vector<DeviceInfo> devices;

    static std::once_flag pw_once;
    std::call_once(pw_once, []() { pw_init(nullptr, nullptr); });

    PwDiscoveryContext ctx;
    ctx.loop = pw_main_loop_new(nullptr);
    if (!ctx.loop) return devices;

    ctx.context = pw_context_new(pw_main_loop_get_loop(ctx.loop), nullptr, 0);
    if (!ctx.context) {
        pw_main_loop_destroy(ctx.loop);
        return devices;
    }

    ctx.core = pw_context_connect(ctx.context, nullptr, 0);
    if (!ctx.core) {
        pw_context_destroy(ctx.context);
        pw_main_loop_destroy(ctx.loop);
        return devices;
    }

    ctx.registry =
        pw_core_get_registry(ctx.core, PW_VERSION_REGISTRY, 0);

    // PipeWire's C macros use GCC statement-expressions which trigger
    // -Wpedantic in C++.  Suppress around all macro call sites.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    pw_registry_add_listener(ctx.registry, &ctx.registry_listener,
                             &registry_events, &ctx);
    pw_core_add_listener(ctx.core, &ctx.core_listener, &core_events, &ctx);

    ctx.sync_seq = pw_core_sync(ctx.core, PW_ID_CORE, 0);
#pragma GCC diagnostic pop
    pw_main_loop_run(ctx.loop);

    {
        std::lock_guard<std::mutex> lock(ctx.mutex);
        for (auto& [_, dev] : ctx.devices) {
            devices.push_back(std::move(dev));
        }
    }

    if (ctx.registry)
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(ctx.registry));
    if (ctx.core) pw_core_disconnect(ctx.core);
    if (ctx.context) pw_context_destroy(ctx.context);
    if (ctx.loop) pw_main_loop_destroy(ctx.loop);

    return devices;
}

}  // namespace insightos::backend

#endif  // INSIGHTOS_HAS_PIPEWIRE
