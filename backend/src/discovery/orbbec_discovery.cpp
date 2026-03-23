/// InsightOS backend — Orbbec depth-camera discovery.
///
/// Ported from donor src/pool/orbbec_discovery.cpp and
/// src/pool/orbbec_common.hpp (commit 4032eb4).
///
/// Key changes from donor:
///   - Free functions instead of DevicePool members
///   - Namespace insightos::backend (was iocontroller)
///   - Uses types.hpp DeviceIdentity.kind_str (was .kind)
///   - No spdlog / env.hpp dependency
///   - init_orbbec_logging() inlined here (was orbbec_common.hpp)
///   - get_orbbec_vendor_ids() provides the USB VID skip set

#ifdef INSIGHTOS_HAS_ORBBEC

#include "insightos/backend/discovery.hpp"

#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <libobsensor/ObSensor.hpp>
#include <libobsensor/hpp/Error.hpp>
#include <libobsensor/hpp/Sensor.hpp>
#include <libobsensor/hpp/StreamProfile.hpp>

namespace insightos::backend {
namespace {

// ─── Orbbec format mapping ──────────────────────────────────────────────

std::string ob_format_to_string(OBFormat fmt) {
    switch (fmt) {
        case OB_FORMAT_YUYV: return "yuyv";
        case OB_FORMAT_UYVY: return "uyvy";
        case OB_FORMAT_NV12: return "nv12";
        case OB_FORMAT_NV21: return "nv21";
        case OB_FORMAT_MJPG: return "mjpeg";
        case OB_FORMAT_H264: return "h264";
        case OB_FORMAT_H265: return "h265";
        case OB_FORMAT_RGB:  return "rgb24";
        case OB_FORMAT_BGR:  return "bgr24";
        case OB_FORMAT_BGRA: return "bgra";
        case OB_FORMAT_RGBA: return "rgba";
        case OB_FORMAT_Y8:
        case OB_FORMAT_GRAY:
            return "gray8";
        case OB_FORMAT_Y10:
        case OB_FORMAT_Y11:
        case OB_FORMAT_Y12:
        case OB_FORMAT_Y14:
        case OB_FORMAT_Y16:
            return "y16";
        case OB_FORMAT_Z16:
            return "z16";
        default:
            return "unknown";
    }
}

// ─── Orbbec SDK config resolution ───────────────────────────────────────

std::string resolve_orbbec_config_path() {
    if (const char* env = std::getenv("ORBBEC_SDK_CONFIG")) {
        return std::string(env);
    }

    std::error_code ec;
    auto exe_path = std::filesystem::read_symlink("/proc/self/exe", ec);
    std::string exe_dir = ec ? "." : exe_path.parent_path().string();

    const std::vector<std::string> candidates = {
        "OrbbecSDKConfig_v1.0.xml",
        exe_dir + "/OrbbecSDKConfig_v1.0.xml",
        exe_dir + "/../OrbbecSDKConfig_v1.0.xml",
        exe_dir + "/../third_party/orbbec_sdk/SDK/config/OrbbecSDKConfig_v1.0.xml",
        exe_dir + "/../../third_party/orbbec_sdk/SDK/config/OrbbecSDKConfig_v1.0.xml",
        exe_dir + "/../../../third_party/orbbec_sdk/SDK/config/OrbbecSDKConfig_v1.0.xml",
    };

    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }

    return "";
}

/// Initialize Orbbec SDK logging (adapted from donor orbbec_common.hpp).
void init_orbbec_logging() {
    std::filesystem::create_directories("Log/orbbec");
    ob::Context::setLoggerToConsole(OB_LOG_SEVERITY_OFF);
    ob::Context::setLoggerToFile(OB_LOG_SEVERITY_WARN, "Log/orbbec/");
}

// ─── Stream profile enumeration ─────────────────────────────────────────

/// Build ResolvedCaps from Orbbec sensor stream profiles.
std::vector<ResolvedCaps> build_caps(
    const std::shared_ptr<ob::StreamProfileList>& list) {
    std::vector<ResolvedCaps> caps;
    if (!list) return caps;

    std::set<std::string> seen;
    std::uint32_t idx = 0;
    for (std::uint32_t i = 0; i < list->count(); ++i) {
        auto profile = list->getProfile(i);
        if (!profile || !profile->is<ob::VideoStreamProfile>()) continue;
        auto video = profile->as<ob::VideoStreamProfile>();

        const std::uint32_t w = video->width();
        const std::uint32_t h = video->height();
        const std::uint32_t fps = video->fps();
        const std::string fmt = ob_format_to_string(video->format());
        if (fmt == "unknown") continue;

        // Deduplicate by format:resolution@fps
        std::string key = fmt + ":" + std::to_string(w) + "x" +
                          std::to_string(h) + "@" + std::to_string(fps);
        if (!seen.insert(key).second) continue;

        ResolvedCaps rc;
        rc.index = idx++;
        rc.format = fmt;
        rc.width = w;
        rc.height = h;
        rc.fps = fps;
        caps.push_back(rc);
    }
    return caps;
}

/// Add a stream to a DeviceInfo if profiles are non-empty.
void add_stream(DeviceInfo& dev, const std::string& name,
                const std::shared_ptr<ob::StreamProfileList>& profiles) {
    auto caps = build_caps(profiles);
    if (caps.empty()) return;

    StreamInfo stream;
    stream.stream_id = name;
    stream.name = default_stream_name(dev.kind, stream.stream_id);
    stream.data_kind = DataKind::kFrame;
    stream.supported_caps = std::move(caps);
    dev.streams.push_back(std::move(stream));
}

std::shared_ptr<ob::Sensor> get_sensor(
    const std::shared_ptr<ob::SensorList>& list, OBSensorType type) {
    if (!list) return nullptr;
    try {
        return list->getSensor(type);
    } catch (...) {
        return nullptr;
    }
}

}  // namespace

// ─── Public entry points ────────────────────────────────────────────────

std::set<std::string> get_orbbec_vendor_ids() {
    // Orbbec USB vendor ID (0x2bc5)
    return {"2bc5"};
}

std::vector<DeviceInfo> discover_orbbec() {
    std::vector<DeviceInfo> devices;

    try {
        init_orbbec_logging();

        const std::string config_path = resolve_orbbec_config_path();
        ob::Context ctx(config_path.empty() ? "" : config_path.c_str());

        auto list = ctx.queryDeviceList();
        if (!list) return devices;

        for (std::uint32_t i = 0; i < list->deviceCount(); ++i) {
            auto dev = list->getDevice(i);
            if (!dev) continue;

            auto info = dev->getDeviceInfo();
            const std::string name = info ? info->name() : "Orbbec";
            const std::string serial = info ? info->serialNumber() : "";

            DeviceInfo device;
            device.kind = DeviceKind::kOrbbec;
            device.name = name;
            device.uri = serial.empty() ? ("orbbec://" + std::to_string(i))
                                        : ("orbbec://" + serial);
            device.state = DeviceState::kDiscovered;

            device.identity.device_uri = device.uri;
            device.identity.device_id =
                serial.empty() ? std::to_string(i) : serial;
            device.identity.kind_str = "orbbec";
            device.identity.hardware_name = name;
            device.identity.usb_vendor_id = "2bc5";
            device.identity.usb_serial = serial;
            device.description =
                name + (serial.empty() ? "" : " (S/N " + serial + ")");

            // Enumerate streams (per-sensor try-catch)
            auto sensors = dev->getSensorList();

            auto try_add_stream = [&](const std::string& sname,
                                      OBSensorType stype) {
                try {
                    if (auto s = get_sensor(sensors, stype)) {
                        add_stream(device, sname, s->getStreamProfileList());
                    }
                } catch (const ob::Error&) {
                } catch (const std::exception&) {
                }
            };

            try_add_stream("color", OB_SENSOR_COLOR);
            try_add_stream("depth", OB_SENSOR_DEPTH);

            // IR: try generic, then left, then right
            bool has_ir = false;
            for (auto ir_type :
                 {OB_SENSOR_IR, OB_SENSOR_IR_LEFT, OB_SENSOR_IR_RIGHT}) {
                if (has_ir) break;
                try {
                    if (auto s = get_sensor(sensors, ir_type)) {
                        add_stream(device, "ir", s->getStreamProfileList());
                        has_ir = !device.streams.empty() &&
                                 device.streams.back().name == "ir";
                    }
                } catch (const ob::Error&) {
                } catch (...) {
                }
            }

            if (device.streams.empty()) continue;

            devices.push_back(std::move(device));
        }
    } catch (const ob::Error& e) {
        throw std::runtime_error(std::string("Orbbec SDK: ") +
                                 e.getName() + ": " + e.getMessage());
    }

    return devices;
}

}  // namespace insightos::backend

#endif  // INSIGHTOS_HAS_ORBBEC
