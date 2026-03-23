/// InsightOS backend — V4L2 device discovery.
///
/// Ported from donor src/pool/device_pool.cpp discover_v4l2() and
/// read_v4l2_usb_vid() (commit 4032eb4).
///
/// Key changes from donor:
///   - Free function instead of DevicePool member method
///   - Namespace insightos::backend (was iocontroller)
///   - Uses types.hpp DeviceIdentity.kind_str instead of .kind
///   - Added HEVC format mapping
///   - USB product ID and serial read from sysfs
///   - No spdlog dependency — silent operation

#include "insightos/backend/discovery.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>

namespace insightos::backend {
namespace {

/// Read a single sysfs attribute for a video device, trimmed and lowercased.
std::string read_sysfs_attr(const std::string& video_name,
                            const std::string& attr) {
    const std::string path =
        "/sys/class/video4linux/" + video_name + "/device/../" + attr;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return {};
    std::string val;
    std::getline(ifs, val);
    while (!val.empty() &&
           (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) {
        val.pop_back();
    }
    std::transform(val.begin(), val.end(), val.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return val;
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

std::string read_sysfs_device_path(const std::string& video_name) {
    std::error_code ec;
    auto path = std::filesystem::weakly_canonical(
        std::filesystem::path("/sys/class/video4linux") / video_name / "device", ec);
    if (ec) return {};
    return lower_copy(path.string());
}

}  // namespace

std::vector<DeviceInfo> discover_v4l2(
    const std::set<std::string>& skip_vendor_ids) {
    std::vector<DeviceInfo> devices;

    DIR* dir = opendir("/dev");
    if (!dir) return devices;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.rfind("video", 0) != 0) continue;

        // Filter by USB vendor ID
        if (!skip_vendor_ids.empty()) {
            std::string vid = read_sysfs_attr(name, "idVendor");
            if (!vid.empty() && skip_vendor_ids.count(vid)) {
                continue;
            }
        }

        std::string path = "/dev/" + name;
        int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        struct v4l2_capability cap {};
        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            ::close(fd);
            continue;
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            ::close(fd);
            continue;
        }

        DeviceInfo dev;
        dev.uri = "v4l2:" + path;
        dev.kind = DeviceKind::kV4l2;
        dev.name = reinterpret_cast<const char*>(cap.card);
        dev.state = DeviceState::kDiscovered;

        // Identity enrichment
        dev.identity.device_uri = dev.uri;
        dev.identity.device_id = name;
        dev.identity.kind_str = "v4l2";
        dev.identity.hardware_name = dev.name;
        dev.identity.persistent_key = read_sysfs_device_path(name);
        if (dev.identity.persistent_key.empty()) {
            dev.identity.persistent_key =
                lower_copy(reinterpret_cast<const char*>(cap.bus_info));
        }
        dev.identity.usb_vendor_id = read_sysfs_attr(name, "idVendor");
        dev.identity.usb_product_id = read_sysfs_attr(name, "idProduct");
        dev.identity.usb_serial = read_sysfs_attr(name, "serial");
        dev.description = dev.name + " (" + dev.identity.device_id + ")";

        // Enumerate supported formats
        StreamInfo stream;
        stream.stream_id = "image";
        stream.name = default_stream_name(dev.kind, stream.stream_id);
        stream.data_kind = DataKind::kFrame;

        struct v4l2_fmtdesc fmt_desc {};
        fmt_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        std::uint32_t cap_index = 0;
        while (::ioctl(fd, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
            std::string format;
            switch (fmt_desc.pixelformat) {
                case V4L2_PIX_FMT_MJPEG: format = "mjpeg"; break;
                case V4L2_PIX_FMT_YUYV:  format = "yuyv";  break;
                case V4L2_PIX_FMT_H264:  format = "h264";  break;
#ifdef V4L2_PIX_FMT_HEVC
                case V4L2_PIX_FMT_HEVC:  format = "hevc";  break;
#endif
                default:
                    fmt_desc.index++;
                    continue;
            }

            // Enumerate frame sizes for this format
            struct v4l2_frmsizeenum frmsize {};
            frmsize.pixel_format = fmt_desc.pixelformat;
            while (::ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
                if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    // Enumerate frame intervals for this size
                    struct v4l2_frmivalenum frmival {};
                    frmival.pixel_format = fmt_desc.pixelformat;
                    frmival.width = frmsize.discrete.width;
                    frmival.height = frmsize.discrete.height;
                    while (::ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS,
                                   &frmival) == 0) {
                        std::uint32_t fps = 0;
                        if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE &&
                            frmival.discrete.numerator > 0) {
                            fps = frmival.discrete.denominator /
                                  frmival.discrete.numerator;
                        }
                        if (fps > 0) {
                            ResolvedCaps rc;
                            rc.index = cap_index++;
                            rc.format = format;
                            rc.width = frmsize.discrete.width;
                            rc.height = frmsize.discrete.height;
                            rc.fps = fps;
                            stream.supported_caps.push_back(rc);
                        }
                        frmival.index++;
                    }
                }
                frmsize.index++;
            }
            fmt_desc.index++;
        }

        // Skip devices with no usable capture capabilities (e.g. metadata nodes)
        if (stream.supported_caps.empty()) {
            ::close(fd);
            continue;
        }

        dev.streams.push_back(std::move(stream));
        devices.push_back(std::move(dev));
        ::close(fd);
    }
    closedir(dir);

    return devices;
}

}  // namespace insightos::backend
