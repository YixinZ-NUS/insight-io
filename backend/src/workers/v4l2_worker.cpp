/// InsightOS backend — V4L2 capture worker implementation.
///
/// Ported from donor src/workers/v4l2_capture.cpp + v4l2_capture_worker.cpp
/// (commit 4032eb4). Combined into a single class. Delivers frames via
/// callback instead of directly writing to IPC.

#include "v4l2_worker.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace insightos::backend {

// ─── V4L2 fourcc mapping ────────────────────────────────────────────────

uint32_t V4l2Worker::fourcc_from_format(const std::string& fmt) {
    if (fmt == "mjpeg") return V4L2_PIX_FMT_MJPEG;
    if (fmt == "yuyv")  return V4L2_PIX_FMT_YUYV;
    if (fmt == "uyvy")  return V4L2_PIX_FMT_UYVY;
    if (fmt == "nv12")  return V4L2_PIX_FMT_NV12;
    if (fmt == "nv21")  return V4L2_PIX_FMT_NV21;
    if (fmt == "yuv420") return V4L2_PIX_FMT_YUV420;
    if (fmt == "yvu420") return V4L2_PIX_FMT_YVU420;
    if (fmt == "rgb24") return V4L2_PIX_FMT_RGB24;
    if (fmt == "bgr24") return V4L2_PIX_FMT_BGR24;
    if (fmt == "gray8") return V4L2_PIX_FMT_GREY;
    if (fmt == "h264")  return V4L2_PIX_FMT_H264;
    if (fmt == "h265" || fmt == "hevc") return V4L2_PIX_FMT_HEVC;
    return 0;
}

int V4l2Worker::xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}

// ─── Constructor ────────────────────────────────────────────────────────

V4l2Worker::V4l2Worker(V4l2WorkerConfig cfg)
    : CaptureWorker(cfg.name), cfg_(std::move(cfg)) {}

// ─── Setup: open device, set format, request buffers, start streaming ───

std::optional<std::string> V4l2Worker::setup() {
    if (cfg_.device_path.empty()) return "device_path is empty";
    if (cfg_.caps.width == 0 || cfg_.caps.height == 0 || cfg_.caps.fps == 0)
        return "invalid width/height/fps";

    const uint32_t pixfmt = fourcc_from_format(cfg_.caps.format);
    if (pixfmt == 0) return "unsupported v4l2 format: " + cfg_.caps.format;

    fd_ = ::open(cfg_.device_path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
        return "open(" + cfg_.device_path + "): " + std::string(strerror(errno));

    // Verify capture capability
    v4l2_capability cap{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0)
        return "VIDIOC_QUERYCAP: " + std::string(strerror(errno));

    const bool is_capture = (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) ||
                            (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
    if (!is_capture) { cleanup(); return "not a V4L2 video capture device"; }
    if (!(cap.device_caps & V4L2_CAP_STREAMING)) { cleanup(); return "no streaming I/O"; }

    // Set format
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = cfg_.caps.width;
    fmt.fmt.pix.height      = cfg_.caps.height;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;
    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        cleanup();
        return "VIDIOC_S_FMT: " + std::string(strerror(errno));
    }
    actual_width_  = fmt.fmt.pix.width;
    actual_height_ = fmt.fmt.pix.height;

    // Set framerate
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = cfg_.caps.fps;
    if (xioctl(fd_, VIDIOC_S_PARM, &parm) < 0) {
        cleanup();
        return "VIDIOC_S_PARM: " + std::string(strerror(errno));
    }

    // Request mmap buffers
    constexpr uint32_t kBufferCount = 4;
    v4l2_requestbuffers req{};
    req.count  = kBufferCount;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        cleanup();
        return "VIDIOC_REQBUFS: " + std::string(strerror(errno));
    }
    if (req.count == 0) { cleanup(); return "VIDIOC_REQBUFS returned 0 buffers"; }

    buffers_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            cleanup();
            return "VIDIOC_QUERYBUF: " + std::string(strerror(errno));
        }
        void* start = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd_, buf.m.offset);
        if (start == MAP_FAILED) { cleanup(); return "mmap: " + std::string(strerror(errno)); }
        buffers_[i] = MmapBuffer{start, buf.length};
    }

    // Queue all buffers
    for (uint32_t i = 0; i < buffers_.size(); ++i) {
        std::string err;
        if (!queue_buffer(static_cast<int>(i), err)) { cleanup(); return err; }
    }

    // Start streaming
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        cleanup();
        return "VIDIOC_STREAMON: " + std::string(strerror(errno));
    }
    streaming_ = true;

    std::fprintf(stderr, "V4L2 worker '%s' started: %s %ux%u fps=%u buffers=%zu\n",
                 cfg_.name.c_str(), cfg_.device_path.c_str(),
                 actual_width_, actual_height_, cfg_.caps.fps, buffers_.size());
    return std::nullopt;
}

// ─── Capture loop ───────────────────────────────────────────────────────

void V4l2Worker::run() {
    while (!stop_requested()) {
        pollfd pfd{};
        pfd.fd     = fd_;
        pfd.events = POLLIN;
        const int r = ::poll(&pfd, 1, 200);
        if (r == 0) continue;  // timeout
        if (r < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "V4L2 worker '%s' poll error: %s\n",
                         cfg_.name.c_str(), strerror(errno));
            break;
        }
        if ((pfd.revents & POLLIN) == 0) continue;

        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            std::fprintf(stderr, "V4L2 worker '%s' DQBUF error: %s\n",
                         cfg_.name.c_str(), strerror(errno));
            break;
        }

        const int idx = static_cast<int>(buf.index);
        if (idx < 0 || static_cast<size_t>(idx) >= buffers_.size()) {
            std::fprintf(stderr, "V4L2 worker '%s' invalid buffer index\n",
                         cfg_.name.c_str());
            break;
        }

        const uint64_t monotonic_ns =
            static_cast<uint64_t>(buf.timestamp.tv_sec) * 1'000'000'000ULL +
            static_cast<uint64_t>(buf.timestamp.tv_usec) * 1'000ULL;

        const auto* data = static_cast<const uint8_t*>(buffers_[idx].start);
        deliver_frame("image", data, buf.bytesused,
                      static_cast<int64_t>(monotonic_ns));

        requeue_buffer_best_effort(idx);
    }
}

// ─── Cleanup ────────────────────────────────────────────────────────────

void V4l2Worker::cleanup() {
    if (fd_ >= 0 && streaming_) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
    for (auto& b : buffers_) {
        if (b.start && b.length) ::munmap(b.start, b.length);
        b = MmapBuffer{};
    }
    buffers_.clear();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    actual_width_ = 0;
    actual_height_ = 0;
}

// ─── Buffer management ─────────────────────────────────────────────────

bool V4l2Worker::queue_buffer(int index, std::string& err) noexcept {
    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = static_cast<uint32_t>(index);
    if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        err = "VIDIOC_QBUF: " + std::string(strerror(errno));
        return false;
    }
    return true;
}

void V4l2Worker::requeue_buffer_best_effort(int index) noexcept {
    std::string err;
    if (!queue_buffer(index, err)) {
        std::fprintf(stderr, "V4L2 requeue failed (index=%d): %s\n", index, err.c_str());
    }
}

}  // namespace insightos::backend
