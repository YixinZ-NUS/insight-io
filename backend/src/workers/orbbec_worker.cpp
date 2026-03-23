/// InsightOS backend — Orbbec depth-camera capture worker implementation.
///
/// Ported from donor src/workers/orbbec_capture_worker.cpp (commit 4032eb4).
///
/// Key changes from donor:
///   - Uses CaptureWorker callback delivery instead of direct IPC writes
///   - Keeps Orbbec AGGREGATE output disabled so each waitForFrames() returns
///     a single independent stream type per FrameSet
///   - Uses std::fprintf(stderr, ...) for runtime logging

#ifdef INSIGHTOS_HAS_ORBBEC

#include "orbbec_worker.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <utility>

#include <unistd.h>

#include <libobsensor/ObSensor.hpp>
#include <libobsensor/hpp/Error.hpp>
#include <libobsensor/hpp/Pipeline.hpp>

namespace insightos::backend {
namespace {

bool stop_pipeline_with_retry(ob::Pipeline& pipeline,
                              const std::string& worker_name) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        try {
            pipeline.stop();
            return true;
        } catch (const ob::Error& e) {
            std::fprintf(stderr,
                         "Orbbec worker '%s' stop attempt %d failed: %s\n",
                         worker_name.c_str(), attempt + 1, e.getMessage());
        } catch (...) {
            std::fprintf(stderr,
                         "Orbbec worker '%s' stop attempt %d failed: resource busy\n",
                         worker_name.c_str(), attempt + 1);
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(200 * (attempt + 1)));
    }

    std::fprintf(stderr,
                 "Orbbec worker '%s': pipeline.stop() failed after 5 retries\n",
                 worker_name.c_str());
    return false;
}

void init_orbbec_logging() {
    std::filesystem::create_directories("Log/orbbec");
    ob::Context::setLoggerToConsole(OB_LOG_SEVERITY_OFF);
    ob::Context::setLoggerToFile(OB_LOG_SEVERITY_WARN, "Log/orbbec/");
}

std::string resolve_config_path() {
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

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return "";
}

OBFormat string_to_ob_format(const std::string& fmt) {
    if (fmt.empty()) return OB_FORMAT_ANY;
    if (fmt == "mjpeg") return OB_FORMAT_MJPG;
    if (fmt == "yuyv") return OB_FORMAT_YUYV;
    if (fmt == "uyvy") return OB_FORMAT_UYVY;
    if (fmt == "nv12") return OB_FORMAT_NV12;
    if (fmt == "nv21") return OB_FORMAT_NV21;
    if (fmt == "rgb24") return OB_FORMAT_RGB;
    if (fmt == "bgr24") return OB_FORMAT_BGR;
    if (fmt == "rgba") return OB_FORMAT_RGBA;
    if (fmt == "bgra") return OB_FORMAT_BGRA;
    if (fmt == "gray8") return OB_FORMAT_GRAY;
    if (fmt == "z16") return OB_FORMAT_Z16;
    if (fmt == "y16" || fmt == "gray16") return OB_FORMAT_Y16;
    return OB_FORMAT_ANY;
}

std::string ob_format_to_string(OBFormat fmt) {
    switch (fmt) {
        case OB_FORMAT_YUYV: return "yuyv";
        case OB_FORMAT_UYVY: return "uyvy";
        case OB_FORMAT_NV12: return "nv12";
        case OB_FORMAT_NV21: return "nv21";
        case OB_FORMAT_MJPG: return "mjpeg";
        case OB_FORMAT_H264: return "h264";
        case OB_FORMAT_H265: return "h265";
        case OB_FORMAT_RGB: return "rgb24";
        case OB_FORMAT_BGR: return "bgr24";
        case OB_FORMAT_BGRA: return "bgra";
        case OB_FORMAT_RGBA: return "rgba";
        case OB_FORMAT_Y8:
        case OB_FORMAT_GRAY: return "gray8";
        case OB_FORMAT_Y10:
        case OB_FORMAT_Y11:
        case OB_FORMAT_Y12:
        case OB_FORMAT_Y14:
        case OB_FORMAT_Y16: return "y16";
        case OB_FORMAT_Z16: return "z16";
        default: return "unknown";
    }
}

const char* d2c_mode_label(D2CMode mode) {
    switch (mode) {
        case D2CMode::kOff: return "off";
        case D2CMode::kHardware: return "hardware";
        case D2CMode::kSoftware: return "software";
    }
    return "unknown";
}

const char* stream_type_label(OBStreamType type) {
    switch (type) {
        case OB_STREAM_COLOR: return "color";
        case OB_STREAM_DEPTH: return "depth";
        case OB_STREAM_IR: return "ir";
        default: return "unknown";
    }
}

std::shared_ptr<ob::Device> select_device(ob::Context& ctx,
                                          const std::string& uri,
                                          std::string& err) {
    auto list = ctx.queryDeviceList();
    if (!list || list->deviceCount() == 0) {
        err = "no Orbbec devices found";
        return nullptr;
    }

    std::string target = uri;
    if (target.rfind("orbbec://", 0) == 0) {
        target = target.substr(9);
    }

    for (uint32_t i = 0; i < list->deviceCount(); ++i) {
        auto dev = list->getDevice(i);
        if (!dev) continue;
        auto info = dev->getDeviceInfo();
        if (info && (target == info->serialNumber() || target == info->uid())) {
            return dev;
        }
    }

    try {
        const uint32_t index = static_cast<uint32_t>(std::stoul(target));
        if (index < list->deviceCount()) {
            return list->getDevice(index);
        }
    } catch (...) {
    }

    err = "Orbbec device not found for uri: " + uri;
    return nullptr;
}

std::shared_ptr<ob::VideoStreamProfile> select_video_profile(
    const std::shared_ptr<ob::StreamProfileList>& list,
    const ResolvedCaps& caps,
    std::string& err) {
    if (!list || list->count() == 0) {
        err = "no stream profiles available";
        return nullptr;
    }

    const OBFormat fmt = string_to_ob_format(caps.format);
    const bool depth_like = is_depth_format(caps.format);

    for (uint32_t i = 0; i < list->count(); ++i) {
        auto profile = list->getProfile(i);
        if (!profile || !profile->is<ob::VideoStreamProfile>()) continue;
        auto video = profile->as<ob::VideoStreamProfile>();

        if (caps.width > 0 && video->width() != caps.width) continue;
        if (caps.height > 0 && video->height() != caps.height) continue;
        if (caps.fps > 0 && video->fps() != caps.fps) continue;
        if (fmt != OB_FORMAT_ANY) {
            const OBFormat found = video->format();
            if (found != fmt) {
                if (depth_like) {
                    if (!(found == OB_FORMAT_Z16 || found == OB_FORMAT_Y16 ||
                          found == OB_FORMAT_Y14 || found == OB_FORMAT_Y12 ||
                          found == OB_FORMAT_Y11 || found == OB_FORMAT_Y10)) {
                        continue;
                    }
                } else {
                    continue;
                }
            }
        }
        return video;
    }

    err = "no matching profile for " + caps.to_named();
    return nullptr;
}

int64_t frame_pts_ns(const std::shared_ptr<ob::Frame>& frame) {
    if (!frame) return 0;
    const uint64_t system_us = frame->systemTimeStampUs();
    const uint64_t device_us = frame->timeStampUs();
    const uint64_t use_us = system_us ? system_us : device_us;
    if (use_us == 0) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    return static_cast<int64_t>(use_us) * 1000;
}

}  // namespace

OrbbecWorker::OrbbecWorker(OrbbecWorkerConfig cfg)
    : CaptureWorker(cfg.name), cfg_(std::move(cfg)) {}

void OrbbecWorker::request_add_streams(
    std::vector<OrbbecStreamConfig> new_streams) {
    std::lock_guard lock(pending_mutex_);
    for (auto& stream : new_streams) {
        bool duplicate = false;
        for (const auto& existing : cfg_.streams) {
            if (existing.name == stream.name) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            for (const auto& pending : pending_streams_) {
                if (pending.name == stream.name) {
                    duplicate = true;
                    break;
                }
            }
        }
        if (!duplicate) {
            std::fprintf(stderr,
                         "Orbbec worker '%s': queued hot-add stream '%s'\n",
                         cfg_.name.c_str(), stream.name.c_str());
            pending_streams_.push_back(std::move(stream));
        }
    }
}

void OrbbecWorker::set_d2c(D2CMode mode) {
    std::lock_guard lock(pending_mutex_);
    cfg_.d2c = mode;
}

std::vector<std::string> OrbbecWorker::active_stream_names() const {
    std::lock_guard lock(pending_mutex_);
    std::vector<std::string> names;
    names.reserve(cfg_.streams.size());
    for (const auto& stream : cfg_.streams) {
        names.push_back(stream.name);
    }
    return names;
}

std::optional<std::string> OrbbecWorker::setup() {
    if (cfg_.streams.empty()) {
        return "no streams configured";
    }
    return std::nullopt;
}

void OrbbecWorker::run() {
    init_orbbec_logging();

    const std::string config_path = resolve_config_path();

    try {
        ob::Context ctx(config_path.empty() ? "" : config_path.c_str());

        std::string select_err;
        auto device = select_device(ctx, cfg_.uri, select_err);
        if (!device) {
            std::fprintf(stderr, "Orbbec worker '%s': %s\n",
                         cfg_.name.c_str(), select_err.c_str());
            return;
        }

        struct StreamState {
            std::string stream_name;
            ResolvedCaps caps;
            OBStreamType stream_type;
            bool logged_first_frame{false};
            uint64_t frame_count{0};
        };
        std::vector<StreamState> states;

        ob::Pipeline pipeline(device);

        auto build_config = [&]() -> std::shared_ptr<ob::Config> {
            auto sdk_cfg = std::make_shared<ob::Config>();
            states.clear();
            std::string profile_err;
            std::vector<OrbbecStreamConfig> streams;
            D2CMode d2c_mode = D2CMode::kOff;
            {
                std::lock_guard lock(pending_mutex_);
                streams = cfg_.streams;
                d2c_mode = cfg_.d2c;
            }

            for (const auto& stream : streams) {
                OBSensorType sensor_type;
                OBStreamType stream_type;
                if (stream.name == "color") {
                    sensor_type = OB_SENSOR_COLOR;
                    stream_type = OB_STREAM_COLOR;
                } else if (stream.name == "depth") {
                    sensor_type = OB_SENSOR_DEPTH;
                    stream_type = OB_STREAM_DEPTH;
                } else if (stream.name == "ir") {
                    sensor_type = OB_SENSOR_IR;
                    stream_type = OB_STREAM_IR;
                } else {
                    std::fprintf(stderr,
                                 "Orbbec worker '%s': unsupported stream '%s'\n",
                                 cfg_.name.c_str(), stream.name.c_str());
                    continue;
                }

                auto profile_list = pipeline.getStreamProfileList(sensor_type);
                auto profile = select_video_profile(profile_list, stream.caps,
                                                   profile_err);
                if (!profile) {
                    std::fprintf(stderr,
                                 "Orbbec worker '%s' %s profile error: %s\n",
                                 cfg_.name.c_str(), stream.name.c_str(),
                                 profile_err.c_str());
                    continue;
                }

                sdk_cfg->enableStream(profile);
                std::fprintf(stderr,
                             "Orbbec worker '%s' enabling %s: %ux%u@%ufps format=%s\n",
                             cfg_.name.c_str(), stream.name.c_str(),
                             profile->width(), profile->height(), profile->fps(),
                             ob_format_to_string(profile->format()).c_str());
                states.push_back({stream.name, stream.caps, stream_type, false, 0});
            }

            // Keep streams independent. Each waitForFrames() returns a FrameSet
            // containing exactly one stream type.
            sdk_cfg->setFrameAggregateOutputMode(
                OB_FRAME_AGGREGATE_OUTPUT_DISABLE);

            if (d2c_mode != D2CMode::kOff) {
                bool has_color = false;
                bool has_depth = false;
                std::shared_ptr<ob::StreamProfile> color_profile;
                for (const auto& state : states) {
                    if (state.stream_type == OB_STREAM_COLOR) {
                        has_color = true;
                        auto color_profiles =
                            pipeline.getStreamProfileList(OB_SENSOR_COLOR);
                        color_profile = select_video_profile(color_profiles,
                                                            state.caps,
                                                            profile_err);
                    }
                    if (state.stream_type == OB_STREAM_DEPTH) {
                        has_depth = true;
                    }
                }

                if (has_color && has_depth && color_profile) {
                    const OBAlignMode ob_mode =
                        d2c_mode == D2CMode::kHardware ? ALIGN_D2C_HW_MODE
                                                       : ALIGN_D2C_SW_MODE;
                    try {
                        auto d2c_profiles =
                            pipeline.getD2CDepthProfileList(color_profile, ob_mode);
                        if (d2c_profiles && d2c_profiles->count() > 0) {
                            sdk_cfg->setAlignMode(ob_mode);
                            std::fprintf(stderr,
                                         "Orbbec worker '%s': D2C %s enabled (%u compatible depth profiles)\n",
                                         cfg_.name.c_str(),
                                         d2c_mode_label(d2c_mode),
                                         d2c_profiles->count());
                        } else {
                            std::fprintf(stderr,
                                         "Orbbec worker '%s': D2C %s requested but no compatible depth profiles were found\n",
                                         cfg_.name.c_str(),
                                         d2c_mode_label(d2c_mode));
                        }
                    } catch (const ob::Error& e) {
                        std::fprintf(stderr,
                                     "Orbbec worker '%s': D2C validation failed: %s\n",
                                     cfg_.name.c_str(), e.getMessage());
                    }
                } else if (!has_color || !has_depth) {
                    std::fprintf(stderr,
                                 "Orbbec worker '%s': D2C %s ignored because both color and depth are required\n",
                                 cfg_.name.c_str(), d2c_mode_label(d2c_mode));
                }
            }

            return sdk_cfg;
        };

        auto sdk_config = build_config();
        if (states.empty()) {
            std::fprintf(stderr,
                         "Orbbec worker '%s': no valid streams configured\n",
                         cfg_.name.c_str());
            return;
        }

        pipeline.disableFrameSync();
        pipeline.start(sdk_config);
        auto pipeline_start_time = std::chrono::steady_clock::now();
        auto last_fps_log = pipeline_start_time;
        std::fprintf(stderr,
                     "Orbbec worker '%s' active (%zu streams): %s\n",
                     cfg_.name.c_str(), states.size(), cfg_.uri.c_str());

        uint64_t total_frames_since_log = 0;
        while (!stop_requested()) {
            std::vector<OrbbecStreamConfig> to_add;
            {
                std::lock_guard lock(pending_mutex_);
                if (!pending_streams_.empty()) {
                    to_add.swap(pending_streams_);
                }
            }

            if (!to_add.empty()) {
                std::fprintf(stderr,
                             "Orbbec worker '%s': reconfiguring to add %zu stream(s)\n",
                             cfg_.name.c_str(), to_add.size());

                constexpr auto kMinRunTime = std::chrono::milliseconds(2000);
                auto elapsed = std::chrono::steady_clock::now() - pipeline_start_time;
                if (elapsed < kMinRunTime) {
                    std::this_thread::sleep_for(kMinRunTime - elapsed);
                }

                if (!stop_pipeline_with_retry(pipeline, cfg_.name)) {
                    continue;
                }

                {
                    std::lock_guard lock(pending_mutex_);
                    for (auto& stream : to_add) {
                        bool duplicate = false;
                        for (const auto& existing : cfg_.streams) {
                            if (existing.name == stream.name) {
                                duplicate = true;
                                break;
                            }
                        }
                        if (!duplicate) {
                            cfg_.streams.push_back(std::move(stream));
                        }
                    }
                }

                sdk_config = build_config();
                if (states.empty()) {
                    std::fprintf(stderr,
                                 "Orbbec worker '%s': reconfigure produced no valid streams\n",
                                 cfg_.name.c_str());
                    return;
                }

                pipeline.disableFrameSync();
                pipeline.start(sdk_config);
                pipeline_start_time = std::chrono::steady_clock::now();
                last_fps_log = pipeline_start_time;
                total_frames_since_log = 0;
                std::fprintf(stderr,
                             "Orbbec worker '%s': reconfigured, %zu stream(s) active\n",
                             cfg_.name.c_str(), states.size());
            }

            auto frameset = pipeline.waitForFrames(100);
            if (!frameset) {
                continue;
            }

            for (auto& state : states) {
                std::shared_ptr<ob::Frame> frame;
                if (state.stream_type == OB_STREAM_COLOR) {
                    frame = frameset->colorFrame();
                } else if (state.stream_type == OB_STREAM_DEPTH) {
                    frame = frameset->depthFrame();
                } else if (state.stream_type == OB_STREAM_IR) {
                    frame = frameset->irFrame();
                }
                if (!frame) {
                    continue;
                }

                if (!state.logged_first_frame) {
                    uint32_t width = 0;
                    uint32_t height = 0;
                    if (frame->is<ob::VideoFrame>()) {
                        auto video = frame->as<ob::VideoFrame>();
                        width = video->width();
                        height = video->height();
                    }
                    std::fprintf(stderr,
                                 "Orbbec worker '%s' %s first frame: %ux%u format=%s bytes=%u\n",
                                 cfg_.name.c_str(),
                                 stream_type_label(state.stream_type),
                                 width, height,
                                 ob_format_to_string(frame->format()).c_str(),
                                 frame->dataSize());
                    state.logged_first_frame = true;
                }

                const int64_t pts_ns = frame_pts_ns(frame);
                deliver_frame(state.stream_name,
                              static_cast<const uint8_t*>(frame->data()),
                              frame->dataSize(), pts_ns);
                ++state.frame_count;
                ++total_frames_since_log;
            }

            auto now = std::chrono::steady_clock::now();
            if (now - last_fps_log >= std::chrono::seconds(5)) {
                const double elapsed_s =
                    std::chrono::duration<double>(now - last_fps_log).count();
                std::fprintf(stderr,
                             "Orbbec worker '%s': %.1f fps across %zu stream(s)\n",
                             cfg_.name.c_str(),
                             elapsed_s > 0.0
                                 ? static_cast<double>(total_frames_since_log) /
                                       elapsed_s
                                 : 0.0,
                             states.size());
                for (auto& state : states) {
                    if (state.logged_first_frame) {
                        std::fprintf(stderr,
                                     "  stream %s: %llu frames in last %.1fs\n",
                                     state.stream_name.c_str(),
                                     static_cast<unsigned long long>(state.frame_count),
                                     elapsed_s);
                    }
                    state.frame_count = 0;
                }
                total_frames_since_log = 0;
                last_fps_log = now;
            }
        }

        constexpr auto kMinRunTime = std::chrono::milliseconds(2000);
        auto elapsed = std::chrono::steady_clock::now() - pipeline_start_time;
        if (elapsed < kMinRunTime) {
            std::this_thread::sleep_for(kMinRunTime - elapsed);
        }
        stop_pipeline_with_retry(pipeline, cfg_.name);
    } catch (const ob::Error& e) {
        std::fprintf(stderr, "Orbbec worker '%s' SDK error: %s\n",
                     cfg_.name.c_str(), e.getMessage());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Orbbec worker '%s' error: %s\n",
                     cfg_.name.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "Orbbec worker '%s' unknown SDK exception\n",
                     cfg_.name.c_str());
    }

    std::fprintf(stderr, "Orbbec worker '%s' stopping\n", cfg_.name.c_str());
}

void OrbbecWorker::cleanup() {
    std::fprintf(stderr, "Orbbec worker '%s' cleanup complete\n",
                 cfg_.name.c_str());
}

}  // namespace insightos::backend

#endif  // INSIGHTOS_HAS_ORBBEC
