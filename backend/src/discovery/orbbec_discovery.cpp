// role: Orbbec SDK discovery for the standalone backend.
// revision: 2026-03-27 donor-orbbec-depth-format-parity
// major changes: keeps donor-style depth-family format mapping for
// Y10/Y11/Y12/Y14 profiles, restores raw IR sensor enumeration alongside
// color/depth discovery, falls back to pipeline profile enumeration when the
// sensor list is incomplete, and emits USB vendor metadata so aggregate
// discovery can suppress duplicate V4L2 nodes only after usable SDK-backed
// Orbbec devices were found. See docs/past-tasks.md.

#ifdef INSIGHTIO_HAS_ORBBEC

#include "insightio/backend/discovery.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
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

std::shared_ptr<ob::VideoStreamProfile> first_video_profile(
    const std::shared_ptr<ob::StreamProfileList>& profiles) {
    if (!profiles) {
        return nullptr;
    }
    for (std::uint32_t index = 0; index < profiles->count(); ++index) {
        auto profile = profiles->getProfile(index);
        if (!profile || !profile->is<ob::VideoStreamProfile>()) {
            continue;
        }
        return profile->as<ob::VideoStreamProfile>();
    }
    return nullptr;
}

bool stop_pipeline_quietly(ob::Pipeline& pipeline) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            pipeline.stop();
            return true;
        } catch (...) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return false;
}

bool probe_runtime_capture(const std::shared_ptr<ob::Device>& handle) {
    if (!handle) {
        return false;
    }

    try {
        const auto sensors = handle->getSensorList();
        auto color_profiles = get_pipeline_profiles(handle, OB_SENSOR_COLOR);
        if (!color_profiles) {
            if (auto sensor = get_sensor(sensors, OB_SENSOR_COLOR)) {
                color_profiles = sensor->getStreamProfileList();
            }
        }

        auto depth_profiles = get_pipeline_profiles(handle, OB_SENSOR_DEPTH);
        if (!depth_profiles) {
            if (auto sensor = get_sensor(sensors, OB_SENSOR_DEPTH)) {
                try {
                    depth_profiles = sensor->getStreamProfileList();
                } catch (...) {
                    depth_profiles = nullptr;
                }
            }
        }

        auto profile = first_video_profile(color_profiles);
        if (!profile) {
            profile = first_video_profile(depth_profiles);
        }
        if (!profile) {
            return false;
        }

        ob::Pipeline pipeline(handle);
        auto config = std::make_shared<ob::Config>();
        config->enableStream(profile);
        pipeline.start(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return stop_pipeline_quietly(pipeline);
    } catch (...) {
        return false;
    }
}

}  // namespace

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
            for (const auto sensor_type :
                 {OB_SENSOR_IR, OB_SENSOR_IR_LEFT, OB_SENSOR_IR_RIGHT}) {
                if (has_stream(device, "ir")) {
                    break;
                }
                try {
                    if (auto sensor = get_sensor(sensors, sensor_type)) {
                        add_stream(device, "ir", sensor->getStreamProfileList());
                    }
                } catch (...) {
                }
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

            if (!probe_runtime_capture(handle)) {
                std::fprintf(stderr,
                             "Orbbec discovery skipped unusable SDK-backed device '%s' (%s)\n",
                             device.name.c_str(),
                             device.uri.c_str());
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
