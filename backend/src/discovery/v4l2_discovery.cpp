// role: Linux V4L2 discovery for the standalone backend.
// revision: 2026-03-26 catalog-discovery-slice
// major changes: discovers real capture-capable V4L2 nodes and skips vendor
// IDs already claimed by Orbbec discovery.

#include "insightio/backend/discovery.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace insightio::backend {

namespace {

std::string read_sysfs_attr(const std::string& video_name, const std::string& attr) {
    const std::string path =
        "/sys/class/video4linux/" + video_name + "/device/../" + attr;
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }

    std::string value;
    std::getline(input, value);
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string read_sysfs_device_path(const std::string& video_name) {
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(
        std::filesystem::path("/sys/class/video4linux") / video_name / "device",
        error);
    if (error) {
        return {};
    }
    return lower_copy(path.string());
}

}  // namespace

std::vector<DeviceInfo> discover_v4l2(const std::set<std::string>& skip_vendor_ids) {
    std::vector<DeviceInfo> devices;

    DIR* directory = opendir("/dev");
    if (directory == nullptr) {
        return devices;
    }

    dirent* entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        const std::string name = entry->d_name;
        if (name.rfind("video", 0) != 0) {
            continue;
        }

        if (!skip_vendor_ids.empty()) {
            const auto vendor = read_sysfs_attr(name, "idVendor");
            if (!vendor.empty() && skip_vendor_ids.contains(vendor)) {
                continue;
            }
        }

        const std::string path = "/dev/" + name;
        const int file_descriptor = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (file_descriptor < 0) {
            continue;
        }

        v4l2_capability capability{};
        if (::ioctl(file_descriptor, VIDIOC_QUERYCAP, &capability) < 0) {
            ::close(file_descriptor);
            continue;
        }

        if (!(capability.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
            ::close(file_descriptor);
            continue;
        }

        DeviceInfo device;
        device.uri = "v4l2:" + path;
        device.kind = DeviceKind::kV4l2;
        device.name = reinterpret_cast<const char*>(capability.card);
        device.identity.device_uri = device.uri;
        device.identity.device_id = name;
        device.identity.kind_str = "v4l2";
        device.identity.hardware_name = device.name;
        device.identity.persistent_key = read_sysfs_device_path(name);
        device.identity.usb_vendor_id = read_sysfs_attr(name, "idVendor");
        device.identity.usb_product_id = read_sysfs_attr(name, "idProduct");
        device.identity.usb_serial = read_sysfs_attr(name, "serial");
        device.description = device.name + " (" + device.identity.device_id + ")";

        StreamInfo stream;
        stream.stream_id = "image";
        stream.name = default_stream_name(device.kind, stream.stream_id);
        stream.data_kind = DataKind::kFrame;

        v4l2_fmtdesc format_desc{};
        format_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        std::uint32_t cap_index = 0;
        while (::ioctl(file_descriptor, VIDIOC_ENUM_FMT, &format_desc) == 0) {
            std::string format;
            switch (format_desc.pixelformat) {
                case V4L2_PIX_FMT_MJPEG:
                    format = "mjpeg";
                    break;
                case V4L2_PIX_FMT_YUYV:
                    format = "yuyv";
                    break;
                case V4L2_PIX_FMT_H264:
                    format = "h264";
                    break;
#ifdef V4L2_PIX_FMT_HEVC
                case V4L2_PIX_FMT_HEVC:
                    format = "hevc";
                    break;
#endif
                default:
                    ++format_desc.index;
                    continue;
            }

            v4l2_frmsizeenum frame_size{};
            frame_size.pixel_format = format_desc.pixelformat;
            while (::ioctl(file_descriptor, VIDIOC_ENUM_FRAMESIZES, &frame_size) == 0) {
                if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    v4l2_frmivalenum frame_interval{};
                    frame_interval.pixel_format = format_desc.pixelformat;
                    frame_interval.width = frame_size.discrete.width;
                    frame_interval.height = frame_size.discrete.height;

                    while (::ioctl(file_descriptor, VIDIOC_ENUM_FRAMEINTERVALS,
                                   &frame_interval) == 0) {
                        std::uint32_t fps = 0;
                        if (frame_interval.type == V4L2_FRMIVAL_TYPE_DISCRETE &&
                            frame_interval.discrete.numerator > 0) {
                            fps = frame_interval.discrete.denominator /
                                  frame_interval.discrete.numerator;
                        }
                        if (fps > 0) {
                            stream.supported_caps.push_back(ResolvedCaps{
                                .index = cap_index++,
                                .format = format,
                                .width = frame_size.discrete.width,
                                .height = frame_size.discrete.height,
                                .fps = fps,
                            });
                        }
                        ++frame_interval.index;
                    }
                }
                ++frame_size.index;
            }
            ++format_desc.index;
        }

        ::close(file_descriptor);
        if (stream.supported_caps.empty()) {
            continue;
        }

        device.streams.push_back(std::move(stream));
        devices.push_back(std::move(device));
    }

    closedir(directory);
    return devices;
}

}  // namespace insightio::backend
