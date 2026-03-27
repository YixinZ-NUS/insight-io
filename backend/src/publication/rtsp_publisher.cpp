// role: runtime-only RTSP publication helper for exact serving runtimes.
// revision: 2026-03-27 task8-rtsp-runtime-validation
// major changes: adds RTSP publication planning plus a small ffmpeg-backed
// publisher that can expose one capture stream through an external RTSP server
// without adding durable publication tables, and turns pipe backpressure into
// explicit runtime errors instead of silent frame truncation.
// See docs/past-tasks.md for verification history.

#include "rtsp_publisher.hpp"

#include "insightio/backend/ipc.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace insightio::backend {

namespace {

std::optional<std::string> raw_video_pixel_format(std::string_view format) {
    if (format == "yuyv") {
        return std::string{"yuyv422"};
    }
    if (format == "uyvy") {
        return std::string{"uyvy422"};
    }
    if (format == "nv12" || format == "nv21" || format == "rgb24" ||
        format == "bgr24") {
        return std::string(format);
    }
    if (format == "gray8") {
        return std::string{"gray"};
    }
    return std::nullopt;
}

std::string errno_to_string(int code) {
    return std::strerror(code);
}

bool is_passthrough_video(std::string_view format) {
    return format == "h264" || format == "h265" || format == "hevc";
}

bool is_pcm_audio(std::string_view format) {
    return format == "u8" || format == "s16le" || format == "s24le" ||
           format == "s32le" || format == "f32le";
}

std::vector<std::string> input_args_for_caps(const ResolvedCaps& caps) {
    const auto fps = std::to_string(caps.fps == 0 ? 30 : caps.fps);
    if (caps.is_audio()) {
        if (!is_pcm_audio(caps.format)) {
            return {};
        }
        return {
            "-f",
            caps.format,
            "-ar",
            std::to_string(caps.sample_rate()),
            "-ac",
            std::to_string(std::max<std::uint32_t>(caps.channels(), 1)),
            "-i",
            "pipe:0",
        };
    }

    if (caps.format == "mjpeg") {
        return {
            "-f",
            "mjpeg",
            "-framerate",
            fps,
            "-i",
            "pipe:0",
        };
    }
    if (caps.format == "h264") {
        return {"-f", "h264", "-i", "pipe:0"};
    }
    if (caps.format == "h265" || caps.format == "hevc") {
        return {"-f", "hevc", "-i", "pipe:0"};
    }

    const auto pixel_format = raw_video_pixel_format(caps.format);
    if (!pixel_format.has_value()) {
        return {};
    }
    return {
        "-f",
        "rawvideo",
        "-pixel_format",
        *pixel_format,
        "-video_size",
        std::to_string(caps.width) + "x" + std::to_string(caps.height),
        "-framerate",
        fps,
        "-i",
        "pipe:0",
    };
}

std::vector<std::string> output_args_for_caps(const ResolvedCaps& caps,
                                              std::string& promised_format) {
    if (caps.is_audio()) {
        promised_format = "aac";
        return {
            "-vn",
            "-c:a",
            "aac",
            "-b:a",
            "128k",
        };
    }

    if (is_passthrough_video(caps.format)) {
        promised_format = caps.format == "hevc" ? "h265" : caps.format;
        return {
            "-an",
            "-c:v",
            "copy",
        };
    }

    promised_format = "h264";
    return {
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-tune",
        "zerolatency",
        "-pix_fmt",
        "yuv420p",
        "-g",
        std::to_string(caps.fps == 0 ? 30 : caps.fps),
    };
}

std::string trim_copy(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    while (!value.empty() &&
           (value.front() == '\n' || value.front() == '\r' || value.front() == ' ')) {
        value.erase(value.begin());
    }
    return value;
}

}  // namespace

std::optional<RtspPublicationPlan> build_rtsp_publication_plan(
    const ResolvedCaps& caps) {
    auto input_args = input_args_for_caps(caps);
    if (input_args.empty()) {
        return std::nullopt;
    }

    std::string promised_format;
    auto output_args = output_args_for_caps(caps, promised_format);
    if (output_args.empty() || promised_format.empty()) {
        return std::nullopt;
    }

    RtspPublicationPlan plan;
    plan.promised_format = std::move(promised_format);
    plan.actual_format = caps.format;
    plan.input_args = std::move(input_args);
    plan.output_args = std::move(output_args);
    return plan;
}

RtspPublisher::RtspPublisher(std::string name,
                             std::string url,
                             RtspPublicationPlan plan)
    : name_(std::move(name)), url_(std::move(url)), plan_(std::move(plan)) {}

RtspPublisher::~RtspPublisher() {
    stop();
}

bool RtspPublisher::set_nonblocking(int fd, std::string& err) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        err = "fcntl(F_GETFL): " + errno_to_string(errno);
        return false;
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        err = "fcntl(F_SETFL): " + errno_to_string(errno);
        return false;
    }
    return true;
}

bool RtspPublisher::start(std::string& err) {
    std::scoped_lock lock(mutex_);
    if (running_) {
        return true;
    }

    int stdin_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (::pipe(stdin_pipe) != 0) {
        err = "pipe(stdin): " + errno_to_string(errno);
        return false;
    }
    if (::pipe(stderr_pipe) != 0) {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        err = "pipe(stderr): " + errno_to_string(errno);
        return false;
    }

    const pid_t child = ::fork();
    if (child == 0) {
        ::dup2(stdin_pipe[0], STDIN_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
        (void)std::freopen("/dev/null", "w", stdout);
        ::setsid();

        std::vector<std::string> args = {
            "ffmpeg",
            "-nostdin",
            "-loglevel",
            "warning",
        };
        args.insert(args.end(), plan_.input_args.begin(), plan_.input_args.end());
        args.insert(args.end(), plan_.output_args.begin(), plan_.output_args.end());
        args.insert(args.end(),
                    {"-f", "rtsp", "-rtsp_transport", "tcp", url_});

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        ::execvp("ffmpeg", argv.data());
        _exit(127);
    }

    ::close(stdin_pipe[0]);
    ::close(stderr_pipe[1]);

    if (child < 0) {
        ::close(stdin_pipe[1]);
        ::close(stderr_pipe[0]);
        err = "fork(): " + errno_to_string(errno);
        return false;
    }

    std::string nonblocking_error;
    if (!set_nonblocking(stdin_pipe[1], nonblocking_error) ||
        !set_nonblocking(stderr_pipe[0], nonblocking_error)) {
        ::close(stdin_pipe[1]);
        ::close(stderr_pipe[0]);
        ::kill(child, SIGTERM);
        ::waitpid(child, nullptr, 0);
        err = nonblocking_error;
        return false;
    }

    stdin_fd_ = stdin_pipe[1];
    stderr_fd_ = stderr_pipe[0];
    pid_ = child;
    running_ = true;
    last_error_.clear();

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (!poll_process_locked()) {
        err = last_error_.empty()
                  ? "RTSP publisher exited before becoming ready"
                  : last_error_;
        return false;
    }
    return true;
}

bool RtspPublisher::publish(const uint8_t* data,
                            size_t size,
                            int64_t /*pts_ns*/,
                            uint32_t flags) {
    if (data == nullptr || size == 0 || (flags & ipc::kFlagEndOfStream) != 0) {
        return true;
    }

    std::scoped_lock lock(mutex_);
    if (!poll_process_locked() || stdin_fd_ < 0) {
        return false;
    }

    size_t offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(stdin_fd_, data + offset, size - offset);
        if (written > 0) {
            offset += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::string wait_error;
            if (!wait_for_write_ready_locked(500, wait_error)) {
                last_error_ = wait_error;
                stop_locked();
                return false;
            }
            continue;
        }
        last_error_ = "RTSP publisher write failed: " + errno_to_string(errno);
        stop_locked();
        return false;
    }
    return true;
}

bool RtspPublisher::wait_for_write_ready_locked(int timeout_ms,
                                                std::string& err) const {
    while (true) {
        if (!poll_process_locked() || stdin_fd_ < 0) {
            err = last_error_.empty() ? "RTSP publisher is not running"
                                      : last_error_;
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = stdin_fd_;
        descriptor.events = POLLOUT;
        const int polled = ::poll(&descriptor, 1, timeout_ms);
        if (polled > 0) {
            if ((descriptor.revents & POLLOUT) != 0) {
                return true;
            }
            poll_process_locked();
            err = last_error_.empty() ? "RTSP publisher pipe is no longer writable"
                                      : last_error_;
            return false;
        }
        if (polled == 0) {
            err = "RTSP publisher backpressure timed out";
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        err = "poll(POLLOUT): " + errno_to_string(errno);
        return false;
    }
}

bool RtspPublisher::is_running() const {
    std::scoped_lock lock(mutex_);
    return poll_process_locked();
}

std::string RtspPublisher::last_error() const {
    std::scoped_lock lock(mutex_);
    poll_process_locked();
    return last_error_;
}

std::string RtspPublisher::drain_stderr_locked() const {
    if (stderr_fd_ < 0) {
        return {};
    }

    std::string output;
    char buffer[1024];
    while (true) {
        const ssize_t read_bytes = ::read(stderr_fd_, buffer, sizeof(buffer));
        if (read_bytes > 0) {
            output.append(buffer, static_cast<size_t>(read_bytes));
            continue;
        }
        if (read_bytes < 0 && errno == EINTR) {
            continue;
        }
        if (read_bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        break;
    }
    return trim_copy(output);
}

bool RtspPublisher::poll_process_locked() const {
    if (pid_ <= 0) {
        return false;
    }

    int status = 0;
    const pid_t waited = ::waitpid(pid_, &status, WNOHANG);
    if (waited == 0) {
        return running_;
    }
    if (waited == pid_) {
        running_ = false;
        const auto stderr_output = drain_stderr_locked();
        if (!stderr_output.empty()) {
            last_error_ = stderr_output;
        } else if (WIFEXITED(status)) {
            last_error_ = "RTSP publisher exited with code " +
                          std::to_string(WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            last_error_ = "RTSP publisher terminated by signal " +
                          std::to_string(WTERMSIG(status));
        }
        if (stdin_fd_ >= 0) {
            ::close(stdin_fd_);
            stdin_fd_ = -1;
        }
        if (stderr_fd_ >= 0) {
            ::close(stderr_fd_);
            stderr_fd_ = -1;
        }
        pid_ = -1;
        return false;
    }
    return running_;
}

void RtspPublisher::stop_locked() const {
    if (stdin_fd_ >= 0) {
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }

    if (pid_ > 0) {
        int status = 0;
        for (int attempt = 0; attempt < 10; ++attempt) {
            const pid_t waited = ::waitpid(pid_, &status, WNOHANG);
            if (waited == pid_) {
                pid_ = -1;
                break;
            }
            if (waited < 0 && errno == ECHILD) {
                pid_ = -1;
                break;
            }
            if (attempt == 3) {
                ::kill(pid_, SIGTERM);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
    }

    const auto stderr_output = drain_stderr_locked();
    if (!stderr_output.empty()) {
        last_error_ = stderr_output;
    }
    if (stderr_fd_ >= 0) {
        ::close(stderr_fd_);
        stderr_fd_ = -1;
    }
    running_ = false;
}

void RtspPublisher::stop() {
    std::scoped_lock lock(mutex_);
    stop_locked();
}

}  // namespace insightio::backend
