// role: PipeWire discovery for the standalone backend.
// revision: 2026-03-26 catalog-discovery-slice
// major changes: enumerates audio-source PipeWire nodes and exposes them as raw
// discovery devices for later catalog shaping.

#ifdef INSIGHTIO_HAS_PIPEWIRE

#include "insightio/backend/discovery.hpp"

#include <atomic>
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

namespace insightio::backend {

namespace {

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

std::string pw_format_to_name(const char* format) {
    if (format == nullptr) {
        return "unknown";
    }
    std::string result(format);
    if (result == "S16LE" || result == "S16_LE") return "s16le";
    if (result == "S24LE" || result == "S24_LE") return "s24le";
    if (result == "S32LE" || result == "S32_LE") return "s32le";
    if (result == "F32LE" || result == "F32_LE") return "f32le";
    if (result == "U8") return "u8";
    return result;
}

bool is_audio_source(const char* media_class) {
    if (media_class == nullptr) {
        return false;
    }
    return std::strstr(media_class, "Audio/Source") != nullptr ||
           std::strstr(media_class, "Audio/Duplex") != nullptr;
}

std::string pipewire_persistent_key(const spa_dict* props) {
    if (props == nullptr) {
        return {};
    }

    const char* candidates[] = {
        spa_dict_lookup(props, PW_KEY_DEVICE_SERIAL),
        spa_dict_lookup(props, PW_KEY_DEVICE_BUS_PATH),
        spa_dict_lookup(props, PW_KEY_OBJECT_PATH),
        spa_dict_lookup(props, PW_KEY_DEVICE_NAME),
        spa_dict_lookup(props, PW_KEY_NODE_NAME),
    };

    for (const char* candidate : candidates) {
        if (candidate != nullptr && candidate[0] != '\0') {
            return candidate;
        }
    }
    return {};
}

std::vector<ResolvedCaps> build_caps(const spa_dict* props) {
    std::vector<std::string> formats = {"s16le", "s24le", "s32le", "f32le"};
    std::vector<std::uint32_t> sample_rates = {44100, 48000};
    std::vector<std::uint32_t> channels = {1, 2};

    if (props != nullptr) {
        if (const char* format = spa_dict_lookup(props, "audio.format")) {
            formats = {pw_format_to_name(format)};
        }
        if (const char* rate = spa_dict_lookup(props, "audio.rate")) {
            try {
                sample_rates = {static_cast<std::uint32_t>(std::stoul(rate))};
            } catch (...) {
            }
        }
        if (const char* channel_count = spa_dict_lookup(props, "audio.channels")) {
            try {
                channels = {static_cast<std::uint32_t>(std::stoul(channel_count))};
            } catch (...) {
            }
        }
    }

    std::vector<ResolvedCaps> caps;
    std::uint32_t index = 0;
    for (const auto& format : formats) {
        for (const auto rate : sample_rates) {
            for (const auto channel_count : channels) {
                caps.push_back(ResolvedCaps{
                    .index = index++,
                    .format = format,
                    .width = rate,
                    .height = channel_count,
                    .fps = 0,
                });
            }
        }
    }
    return caps;
}

void on_registry_global(void* data,
                        uint32_t id,
                        uint32_t,
                        const char* type,
                        uint32_t,
                        const spa_dict* props) {
    auto* context = static_cast<PwDiscoveryContext*>(data);

    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || props == nullptr) {
        return;
    }

    const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!is_audio_source(media_class)) {
        return;
    }

    const char* description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    if (description == nullptr) {
        description = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    }
    if (description == nullptr) {
        description = "Audio Device";
    }

    StreamInfo stream;
    stream.stream_id = "audio";
    stream.name = default_stream_name(DeviceKind::kPipeWire, stream.stream_id);
    stream.supported_caps = build_caps(props);

    DeviceInfo device;
    device.uri = "pw:" + std::to_string(id);
    device.kind = DeviceKind::kPipeWire;
    device.name = description;
    device.identity.device_uri = device.uri;
    device.identity.device_id = std::to_string(id);
    device.identity.kind_str = "pipewire";
    device.identity.hardware_name = device.name;
    device.identity.persistent_key = pipewire_persistent_key(props);
    if (const char* serial = spa_dict_lookup(props, PW_KEY_DEVICE_SERIAL);
        serial != nullptr && serial[0] != '\0') {
        device.identity.usb_serial = serial;
    }
    device.description = device.name + " (pw-" + device.identity.device_id + ")";
    device.streams = {std::move(stream)};

    std::lock_guard<std::mutex> lock(context->mutex);
    context->devices[id] = std::move(device);
}

void on_registry_global_remove(void* data, uint32_t id) {
    auto* context = static_cast<PwDiscoveryContext*>(data);
    std::lock_guard<std::mutex> lock(context->mutex);
    context->devices.erase(id);
}

const pw_registry_events registry_events = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
};

void on_core_done(void* data, uint32_t id, int seq) {
    auto* context = static_cast<PwDiscoveryContext*>(data);
    if (id == PW_ID_CORE && seq == context->sync_seq) {
        context->done.store(true);
        pw_main_loop_quit(context->loop);
    }
}

void on_core_error(void*, uint32_t, int, int, const char*) {}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const pw_core_events core_events = {
    .version = PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
    .error = on_core_error,
};
#pragma GCC diagnostic pop

}  // namespace

std::vector<DeviceInfo> discover_pipewire() {
    std::vector<DeviceInfo> devices;

    static std::once_flag once;
    std::call_once(once, []() { pw_init(nullptr, nullptr); });

    PwDiscoveryContext context;
    context.loop = pw_main_loop_new(nullptr);
    if (context.loop == nullptr) {
        return devices;
    }

    context.context =
        pw_context_new(pw_main_loop_get_loop(context.loop), nullptr, 0);
    if (context.context == nullptr) {
        pw_main_loop_destroy(context.loop);
        return devices;
    }

    context.core = pw_context_connect(context.context, nullptr, 0);
    if (context.core == nullptr) {
        pw_context_destroy(context.context);
        pw_main_loop_destroy(context.loop);
        return devices;
    }

    context.registry = pw_core_get_registry(context.core, PW_VERSION_REGISTRY, 0);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    pw_registry_add_listener(context.registry,
                             &context.registry_listener,
                             &registry_events,
                             &context);
    pw_core_add_listener(context.core,
                         &context.core_listener,
                         &core_events,
                         &context);
    context.sync_seq = pw_core_sync(context.core, PW_ID_CORE, 0);
#pragma GCC diagnostic pop

    pw_main_loop_run(context.loop);

    {
        std::lock_guard<std::mutex> lock(context.mutex);
        for (auto& [id, device] : context.devices) {
            (void)id;
            devices.push_back(std::move(device));
        }
    }

    spa_hook_remove(&context.registry_listener);
    spa_hook_remove(&context.core_listener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(context.registry));
    pw_core_disconnect(context.core);
    pw_context_destroy(context.context);
    pw_main_loop_destroy(context.loop);

    return devices;
}

}  // namespace insightio::backend

#endif
