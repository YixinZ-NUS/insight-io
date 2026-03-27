// role: task-9 example app for simultaneous V4L2 and grouped Orbbec routes.
// revision: 2026-03-27 task9-sdk-route-surface
// major changes: demonstrates one app consuming one exact V4L2 route plus one
// grouped Orbbec target-root bind at the same time.

#include "insightos/app.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    insightos::App::Options options;
    int max_frames = 180;
    std::vector<std::string> route_args;
    route_args.emplace_back(argc > 0 ? argv[0] : "mixed_device_consumer");

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help") {
            std::cout
                << "Usage: mixed_device_consumer [options] camera=insightos://... "
                   "orbbec=insightos://...\n"
                << "       mixed_device_consumer [options] camera=session:<id> "
                   "orbbec=session:<id>\n"
                << "\n"
                << "Options:\n"
                << "  --app-name=<name>\n"
                << "  --backend-host=<host>\n"
                << "  --backend-port=<port>\n"
                << "  --max-frames=<count>\n"
                << "\n"
                << "If no startup binds are provided, the app starts idle and can "
                   "receive\n"
                << "later binds through POST /api/apps/{id}/sources.\n";
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

    int camera_frames = 0;
    int color_frames = 0;
    int depth_frames = 0;

    auto maybe_log = [&]() {
        const int total = camera_frames + color_frames + depth_frames;
        if (total == 1 || total % 30 == 0) {
            std::cout << "camera=" << camera_frames
                      << " color=" << color_frames
                      << " depth=" << depth_frames << "\n";
        }
        if (max_frames > 0 && total >= max_frames && camera_frames > 0 &&
            color_frames > 0 && depth_frames > 0) {
            app.request_stop();
        }
    };

    app.route("camera")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame& frame) {
            ++camera_frames;
            if (camera_frames == 1) {
                std::cout << "camera first frame format=" << frame.format
                          << " size=" << frame.width << "x" << frame.height << "\n";
            }
            maybe_log();
        });

    app.route("orbbec/color")
        .expect(insightos::Video{})
        .on_frame([&](const insightos::Frame& frame) {
            ++color_frames;
            if (color_frames == 1) {
                std::cout << "orbbec color first frame format=" << frame.format
                          << " size=" << frame.width << "x" << frame.height << "\n";
            }
            maybe_log();
        });

    app.route("orbbec/depth")
        .expect(insightos::Depth{})
        .on_frame([&](const insightos::Frame& frame) {
            ++depth_frames;
            if (depth_frames == 1) {
                std::cout << "orbbec depth first frame format=" << frame.format
                          << " size=" << frame.width << "x" << frame.height << "\n";
            }
            maybe_log();
        });

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
