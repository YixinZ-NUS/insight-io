// role: task-9 example app for exact V4L2 route callbacks and latency stats.
// revision: 2026-03-27 task9-sdk-route-surface
// major changes: demonstrates the route-based SDK on one video route while
// reporting steady-clock receive latency against frame pts/dts.

#include "insightos/app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    insightos::App::Options options;
    int max_frames = 120;
    std::vector<std::string> route_args;
    route_args.emplace_back(argc > 0 ? argv[0] : "v4l2_latency_monitor");

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help") {
            std::cout
                << "Usage: v4l2_latency_monitor [options] [camera=]insightos://...\n"
                << "       v4l2_latency_monitor [options] camera=session:<id>\n"
                << "\n"
                << "Options:\n"
                << "  --app-name=<name>\n"
                << "  --backend-host=<host>\n"
                << "  --backend-port=<port>\n"
                << "  --max-frames=<count>\n"
                << "\n"
                << "If no startup bind is provided, the app starts idle and can receive a\n"
                << "later bind through POST /api/apps/{id}/sources.\n";
            return 0;
        }
        if (arg.rfind("--app-name=", 0) == 0) {
            options.name = arg.substr(std::string("--app-name=").size());
            continue;
        }
        if (arg.rfind("--backend-host=", 0) == 0) {
            options.backend_host = arg.substr(std::string("--backend-host=").size());
            continue;
        }
        if (arg.rfind("--backend-port=", 0) == 0) {
            options.backend_port =
                static_cast<std::uint16_t>(std::stoul(
                    arg.substr(std::string("--backend-port=").size())));
            continue;
        }
        if (arg.rfind("--max-frames=", 0) == 0) {
            max_frames = std::stoi(arg.substr(std::string("--max-frames=").size()));
            continue;
        }
        route_args.push_back(arg);
    }

    insightos::App app(options);

    std::uint64_t frames_seen = 0;
    double sum_pts_ms = 0.0;
    double sum_dts_ms = 0.0;
    double min_pts_ms = std::numeric_limits<double>::max();
    double max_pts_ms = 0.0;

    app.route("camera")
        .expect(insightos::Video{})
        .on_caps([](const insightos::Caps& caps) {
            std::cout << "camera caps: format=" << caps.format
                      << " size=" << caps.width << "x" << caps.height
                      << " fps=" << caps.fps << "\n";
        })
        .on_frame([&](const insightos::Frame& frame) {
            const double pts_latency_ms =
                frame.pts_ns > 0
                    ? static_cast<double>(frame.received_steady_ns - frame.pts_ns) /
                          1'000'000.0
                    : 0.0;
            const double dts_latency_ms =
                frame.dts_ns > 0
                    ? static_cast<double>(frame.received_steady_ns - frame.dts_ns) /
                          1'000'000.0
                    : pts_latency_ms;

            ++frames_seen;
            sum_pts_ms += pts_latency_ms;
            sum_dts_ms += dts_latency_ms;
            min_pts_ms = std::min(min_pts_ms, pts_latency_ms);
            max_pts_ms = std::max(max_pts_ms, pts_latency_ms);

            if (frames_seen == 1 || frames_seen % 30 == 0) {
                const double avg_pts_ms = sum_pts_ms / static_cast<double>(frames_seen);
                const double avg_dts_ms = sum_dts_ms / static_cast<double>(frames_seen);
                const auto wall_ms = frame.received_wall_ns / 1'000'000;
                std::cout << "frame=" << frames_seen
                          << " pts_ms=" << pts_latency_ms
                          << " dts_ms=" << dts_latency_ms
                          << " avg_pts_ms=" << avg_pts_ms
                          << " avg_dts_ms=" << avg_dts_ms
                          << " min_pts_ms=" << min_pts_ms
                          << " max_pts_ms=" << max_pts_ms
                          << " recv_wall_ms=" << wall_ms << "\n";
            }

            if (max_frames > 0 && static_cast<int>(frames_seen) >= max_frames) {
                app.request_stop();
            }
        })
        .on_stop([]() { std::cout << "camera route stopped\n"; });

    std::vector<char*> route_argv;
    route_argv.reserve(route_args.size());
    for (auto& value : route_args) {
        route_argv.push_back(value.data());
    }
    const int exit_code = app.run(static_cast<int>(route_argv.size()), route_argv.data());
    if (exit_code != 0) {
        std::cerr << app.last_error() << "\n";
    }
    return exit_code;
}
