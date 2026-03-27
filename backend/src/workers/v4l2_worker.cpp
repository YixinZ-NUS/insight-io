// role: V4L2 mmap capture worker implementation for task-7 runtime.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: ports the donor callback-based V4L2 worker so direct and
// app-owned exact sessions can publish captured frames into IPC.
// See docs/past-tasks.md for verification history.

#include "v4l2_worker.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace insightio::backend {

uint32_t V4l2Worker::fourcc_from_format(const std::string& fmt) {
    if (fmt == "mjpeg") {
        return V4L2_PIX_FMT_MJPEG;
    }
    if (fmt == "yuyv") {
        return V4L2_PIX_FMT_YUYV;
    }
    if (fmt == "uyvy") {
        return V4L2_PIX_FMT_UYVY;
    }
    if (fmt == "nv12") {
        return V4L2_PIX_FMT_NV12;
    }
    if (fmt == "nv21") {
        return V4L2_PIX_FMT_NV21;
    }
    if (fmt == "yuv420") {
        return V4L2_PIX_FMT_YUV420;
    }
    if (fmt == "yvu420") {
        return V4L2_PIX_FMT_YVU420;
    }
    if (fmt == "rgb24") {
        return V4L2_PIX_FMT_RGB24;
    }
    if (fmt == "bgr24") {
        return V4L2_PIX_FMT_BGR24;
    }
    if (fmt == "gray8") {
        return V4L2_PIX_FMT_GREY;
    }
    if (fmt == "h264") {
        return V4L2_PIX_FMT_H264;
    }
    if (fmt == "h265" || fmt == "hevc") {
        return V4L2_PIX_FMT_HEVC;
    }
    return 0;
}

int V4l2Worker::xioctl(int fd, unsigned long request, void* arg) {
    int result = -1;
    do {
        result = ::ioctl(fd, request, arg);
    } while (result == -1 && errno == EINTR);
    return result;
}

V4l2Worker::V4l2Worker(V4l2WorkerConfig cfg)
    : CaptureWorker(cfg.name),
      cfg_(std::move(cfg)) {}

std::optional<std::string> V4l2Worker::setup() {
    if (cfg_.device_path.empty()) {
        return "device_path is empty";
    }
    if (cfg_.caps.width == 0 || cfg_.caps.height == 0 || cfg_.caps.fps == 0) {
        return "invalid width/height/fps";
    }

    const uint32_t pixfmt = fourcc_from_format(cfg_.caps.format);
    if (pixfmt == 0) {
        return "unsupported v4l2 format: " + cfg_.caps.format;
    }

    fd_ = ::open(cfg_.device_path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        return "open(" + cfg_.device_path + "): " + std::string(std::strerror(errno));
    }

    v4l2_capability capability {};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) {
        return "VIDIOC_QUERYCAP: " + std::string(std::strerror(errno));
    }

    const bool is_capture =
        (capability.device_caps & V4L2_CAP_VIDEO_CAPTURE) != 0 ||
        (capability.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    if (!is_capture) {
        cleanup();
        return "not a V4L2 video capture device";
    }
    if ((capability.device_caps & V4L2_CAP_STREAMING) == 0) {
        cleanup();
        return "no streaming I/O";
    }

    v4l2_format format {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = cfg_.caps.width;
    format.fmt.pix.height = cfg_.caps.height;
    format.fmt.pix.pixelformat = pixfmt;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(fd_, VIDIOC_S_FMT, &format) < 0) {
        cleanup();
        return "VIDIOC_S_FMT: " + std::string(std::strerror(errno));
    }
    actual_width_ = format.fmt.pix.width;
    actual_height_ = format.fmt.pix.height;

    v4l2_streamparm parm {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = cfg_.caps.fps;
    if (xioctl(fd_, VIDIOC_S_PARM, &parm) < 0) {
        cleanup();
        return "VIDIOC_S_PARM: " + std::string(std::strerror(errno));
    }

    constexpr uint32_t kBufferCount = 4;
    v4l2_requestbuffers request {};
    request.count = kBufferCount;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
        cleanup();
        return "VIDIOC_REQBUFS: " + std::string(std::strerror(errno));
    }
    if (request.count == 0) {
        cleanup();
        return "VIDIOC_REQBUFS returned 0 buffers";
    }

    buffers_.resize(request.count);
    for (uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
            cleanup();
            return "VIDIOC_QUERYBUF: " + std::string(std::strerror(errno));
        }
        void* start = ::mmap(nullptr,
                             buffer.length,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED,
                             fd_,
                             buffer.m.offset);
        if (start == MAP_FAILED) {
            cleanup();
            return "mmap: " + std::string(std::strerror(errno));
        }
        buffers_[index] = MmapBuffer{start, buffer.length};
    }

    for (uint32_t index = 0; index < buffers_.size(); ++index) {
        std::string err;
        if (!queue_buffer(static_cast<int>(index), err)) {
            cleanup();
            return err;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        cleanup();
        return "VIDIOC_STREAMON: " + std::string(std::strerror(errno));
    }
    streaming_ = true;

    std::fprintf(stderr,
                 "V4L2 worker '%s' started: %s %ux%u fps=%u buffers=%zu\n",
                 cfg_.name.c_str(),
                 cfg_.device_path.c_str(),
                 actual_width_,
                 actual_height_,
                 cfg_.caps.fps,
                 buffers_.size());
    return std::nullopt;
}

void V4l2Worker::run() {
    while (!stop_requested()) {
        pollfd descriptor {};
        descriptor.fd = fd_;
        descriptor.events = POLLIN;
        const int poll_result = ::poll(&descriptor, 1, 200);
        if (poll_result == 0) {
            continue;
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr,
                         "V4L2 worker '%s' poll error: %s\n",
                         cfg_.name.c_str(),
                         std::strerror(errno));
            break;
        }
        if ((descriptor.revents & POLLIN) == 0) {
            continue;
        }

        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            std::fprintf(stderr,
                         "V4L2 worker '%s' DQBUF error: %s\n",
                         cfg_.name.c_str(),
                         std::strerror(errno));
            break;
        }

        const int index = static_cast<int>(buffer.index);
        if (index < 0 || static_cast<size_t>(index) >= buffers_.size()) {
            std::fprintf(stderr,
                         "V4L2 worker '%s' invalid buffer index\n",
                         cfg_.name.c_str());
            break;
        }

        const uint64_t monotonic_ns =
            static_cast<uint64_t>(buffer.timestamp.tv_sec) * 1'000'000'000ULL +
            static_cast<uint64_t>(buffer.timestamp.tv_usec) * 1'000ULL;

        const auto* data = static_cast<const uint8_t*>(buffers_[index].start);
        deliver_frame("image",
                      data,
                      buffer.bytesused,
                      static_cast<int64_t>(monotonic_ns));
        requeue_buffer_best_effort(index);
    }
}

void V4l2Worker::cleanup() {
    if (fd_ >= 0 && streaming_) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
    for (auto& buffer : buffers_) {
        if (buffer.start != nullptr && buffer.length > 0) {
            ::munmap(buffer.start, buffer.length);
        }
        buffer = MmapBuffer{};
    }
    buffers_.clear();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    actual_width_ = 0;
    actual_height_ = 0;
}

bool V4l2Worker::queue_buffer(int index, std::string& err) noexcept {
    v4l2_buffer buffer {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = static_cast<uint32_t>(index);
    if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
        err = "VIDIOC_QBUF: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

void V4l2Worker::requeue_buffer_best_effort(int index) noexcept {
    std::string err;
    if (!queue_buffer(index, err)) {
        std::fprintf(stderr,
                     "V4L2 requeue failed (index=%d): %s\n",
                     index,
                     err.c_str());
    }
}

}  // namespace insightio::backend
