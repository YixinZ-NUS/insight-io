// role: Orbbec SDK discovery for the standalone backend.
// revision: 2026-03-26 vendored-orbbec-sdk-and-sqlite-serialization
// major changes: discovers Orbbec color and depth capabilities, falls back to
// pipeline profile enumeration when the sensor list is incomplete, and
// supplies the Orbbec USB vendor skip set used by V4L2 discovery.

#ifdef INSIGHTIO_HAS_ORBBEC

#include "insightio/backend/discovery.hpp"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <libobsensor/ObSensor.hpp>
#include <libobsensor/hpp/Error.hpp>
#include <libobsensor/hpp/Pipeline.hpp>
#include <libobsensor/hpp/Sensor.hpp>
#include <libobsensor/hpp/StreamProfile.hpp>

namespace insightio::backend {

namespace {

std::string ob_format_to_string(OBFormat format) {
    switch (format) {
        case OB_FORMAT_YUYV:
            return "yuyv";
        case OB_FORMAT_UYVY:
            return "uyvy";
        case OB_FORMAT_NV12:
            return "nv12";
        case OB_FORMAT_NV21:
            return "nv21";
        case OB_FORMAT_MJPG:
            return "mjpeg";
        case OB_FORMAT_H264:
            return "h264";
        case OB_FORMAT_H265:
            return "h265";
        case OB_FORMAT_RGB:
            return "rgb24";
        case OB_FORMAT_BGR:
            return "bgr24";
        case OB_FORMAT_BGRA:
            return "bgra";
        case OB_FORMAT_RGBA:
            return "rgba";
        case OB_FORMAT_Y8:
        case OB_FORMAT_GRAY:
            return "gray8";
        case OB_FORMAT_Y16:
            return "y16";
        case OB_FORMAT_Z16:
            return "z16";
        default:
            return "unknown";
    }
}

std::string hex_usb_identifier(int value) {
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << std::setw(4) << std::setfill('0')
           << (value & 0xffff);
    return stream.str();
}

std::string resolve_orbbec_config_path() {
    if (const char* env = std::getenv("ORBBEC_SDK_CONFIG")) {
        return std::string(env);
    }
#ifdef INSIGHTIO_ORBBEC_CONFIG_PATH
    return INSIGHTIO_ORBBEC_CONFIG_PATH;
#else
    return {};
#endif
}

void init_orbbec_logging() {
    std::filesystem::create_directories("Log/orbbec");
    ob::Context::setLoggerToConsole(OB_LOG_SEVERITY_OFF);
    ob::Context::setLoggerToFile(OB_LOG_SEVERITY_WARN, "Log/orbbec/");
}

std::vector<ResolvedCaps> build_caps(
    const std::shared_ptr<ob::StreamProfileList>& profiles) {
    std::vector<ResolvedCaps> caps;
    if (!profiles) {
        return caps;
    }

    std::set<std::string> seen;
    std::uint32_t index = 0;
    for (std::uint32_t i = 0; i < profiles->count(); ++i) {
        auto profile = profiles->getProfile(i);
        if (!profile || !profile->is<ob::VideoStreamProfile>()) {
            continue;
        }

        auto video = profile->as<ob::VideoStreamProfile>();
        const auto format = ob_format_to_string(video->format());
        if (format == "unknown") {
            continue;
        }

        const auto key = format + ":" + std::to_string(video->width()) + "x" +
                         std::to_string(video->height()) + "@" +
                         std::to_string(video->fps());
        if (!seen.insert(key).second) {
            continue;
        }

        caps.push_back(ResolvedCaps{
            .index = index++,
            .format = format,
            .width = video->width(),
            .height = video->height(),
            .fps = video->fps(),
        });
    }
    return caps;
}

std::shared_ptr<ob::Sensor> get_sensor(
    const std::shared_ptr<ob::SensorList>& sensors,
    OBSensorType type) {
    if (!sensors) {
        return nullptr;
    }
    try {
        return sensors->getSensor(type);
    } catch (...) {
        return nullptr;
    }
}

void add_stream(DeviceInfo& device,
                const std::string& stream_id,
                const std::shared_ptr<ob::StreamProfileList>& profiles) {
    auto caps = build_caps(profiles);
    if (caps.empty()) {
        return;
    }

    StreamInfo stream;
    stream.stream_id = stream_id;
    stream.name = default_stream_name(device.kind, stream.stream_id);
    stream.data_kind = DataKind::kFrame;
    stream.supported_caps = std::move(caps);
    device.streams.push_back(std::move(stream));
}

bool has_stream(const DeviceInfo& device, std::string_view stream_id) {
    for (const auto& stream : device.streams) {
        if (stream.stream_id == stream_id) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<ob::StreamProfileList> get_pipeline_profiles(
    const std::shared_ptr<ob::Device>& handle,
    OBSensorType type) {
    try {
        ob::Pipeline pipeline(handle);
        return pipeline.getStreamProfileList(type);
    } catch (...) {
        return nullptr;
    }
}

}  // namespace

std::set<std::string> get_orbbec_vendor_ids() {
    return {"2bc5"};
}

std::vector<DeviceInfo> discover_orbbec() {
    std::vector<DeviceInfo> devices;

    try {
        init_orbbec_logging();

        const auto config_path = resolve_orbbec_config_path();
        ob::Context context(config_path.empty() ? "" : config_path.c_str());
        auto list = context.queryDeviceList();
        if (!list) {
            return devices;
        }

        for (std::uint32_t index = 0; index < list->deviceCount(); ++index) {
            auto handle = list->getDevice(index);
            if (!handle) {
                continue;
            }

            auto info = handle->getDeviceInfo();
            const std::string name = info ? info->name() : "Orbbec";
            const std::string serial = info ? info->serialNumber() : "";
            const std::string uid = info ? info->uid() : "";
            const std::string device_token =
                !serial.empty() ? serial
                                : (!uid.empty() ? uid
                                                : std::to_string(index));

            DeviceInfo device;
            device.kind = DeviceKind::kOrbbec;
            device.name = name;
            device.uri = "orbbec://" + device_token;
            device.identity.device_uri = device.uri;
            device.identity.device_id = device_token;
            device.identity.kind_str = "orbbec";
            device.identity.hardware_name = name;
            device.identity.usb_vendor_id =
                info ? hex_usb_identifier(info->vid()) : "2bc5";
            device.identity.usb_product_id =
                info ? hex_usb_identifier(info->pid()) : "";
            device.identity.usb_serial = serial;
            device.description = name + (serial.empty() ? "" : " (S/N " + serial + ")");

            const auto sensors = handle->getSensorList();
            try {
                if (auto sensor = get_sensor(sensors, OB_SENSOR_COLOR)) {
                    add_stream(device, "color", sensor->getStreamProfileList());
                }
            } catch (...) {
            }
            try {
                if (auto sensor = get_sensor(sensors, OB_SENSOR_DEPTH)) {
                    add_stream(device, "depth", sensor->getStreamProfileList());
                }
            } catch (...) {
            }

            if (!has_stream(device, "color")) {
                add_stream(device,
                           "color",
                           get_pipeline_profiles(handle, OB_SENSOR_COLOR));
            }
            if (!has_stream(device, "depth")) {
                add_stream(device,
                           "depth",
                           get_pipeline_profiles(handle, OB_SENSOR_DEPTH));
            }

            if (device.streams.empty()) {
                continue;
            }
            devices.push_back(std::move(device));
        }
    } catch (const ob::Error& error) {
        throw std::runtime_error(std::string("Orbbec SDK: ") + error.getName() +
                                 ": " + error.getMessage());
    }

    return devices;
}

}  // namespace insightio::backend

#endif
