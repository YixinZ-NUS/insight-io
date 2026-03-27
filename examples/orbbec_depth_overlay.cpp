// role: task-9 example app for grouped Orbbec color-plus-depth callbacks.
// revision: 2026-03-27 task9-sdk-route-surface
// major changes: demonstrates grouped target-root binds by consuming ordinary
// `orbbec/color` and `orbbec/depth` callbacks and saving an OpenCV overlay.

#include "insightos/app.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct LatestColor {
    cv::Mat image;
    std::int64_t pts_ns{0};
};

struct LatestDepth {
    cv::Mat image;
    std::int64_t pts_ns{0};
};

}  // namespace

int main(int argc, char** argv) {
    insightos::App::Options options;
    int max_pairs = 60;
    int pair_threshold_ms = 80;
    std::string output_path = "/tmp/insight-io-orbbec-overlay.png";
    std::vector<std::string> route_args;
    route_args.emplace_back(argc > 0 ? argv[0] : "orbbec_depth_overlay");

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help") {
            std::cout
                << "Usage: orbbec_depth_overlay [options] [orbbec=]insightos://...\n"
                << "       orbbec_depth_overlay [options] orbbec=session:<id>\n"
                << "\n"
                << "Examples:\n"
                << "  orbbec_depth_overlay "
                   "insightos://localhost/sv1301s-u3/orbbec/preset/480p_30\n"
                << "  orbbec_depth_overlay "
                   "insightos://localhost/sv1301s-u3/orbbec/preset/720p_30\n"
                << "\n"
                << "Options:\n"
                << "  --app-name=<name>\n"
                << "  --backend-host=<host>\n"
                << "  --backend-port=<port>\n"
                << "  --max-pairs=<count>\n"
                << "  --pair-threshold-ms=<ms>\n"
                << "  --output=<path>\n"
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
        if (arg.rfind("--max-pairs=", 0) == 0) {
            max_pairs = std::stoi(arg.substr(std::string("--max-pairs=").size()));
            continue;
        }
        if (arg.rfind("--pair-threshold-ms=", 0) == 0) {
            pair_threshold_ms =
                std::stoi(arg.substr(std::string("--pair-threshold-ms=").size()));
            continue;
        }
        if (arg.rfind("--output=", 0) == 0) {
            output_path = arg.substr(std::string("--output=").size());
            continue;
        }
        route_args.push_back(arg);
    }

    insightos::App app(options);
    LatestColor latest_color;
    LatestDepth latest_depth;
    std::int64_t last_rendered_color_pts = 0;
    std::int64_t last_rendered_depth_pts = 0;
    int rendered_pairs = 0;

    auto maybe_render_overlay = [&]() {
        if (latest_color.image.empty() || latest_depth.image.empty()) {
            return;
        }
        if (latest_color.pts_ns == last_rendered_color_pts &&
            latest_depth.pts_ns == last_rendered_depth_pts) {
            return;
        }

        const auto pts_delta_ns =
            std::llabs(latest_color.pts_ns - latest_depth.pts_ns);
        if (pts_delta_ns > static_cast<std::int64_t>(pair_threshold_ms) *
                               1'000'000LL) {
            return;
        }

        double min_depth = 0.0;
        double max_depth = 0.0;
        cv::minMaxLoc(latest_depth.image, &min_depth, &max_depth);
        if (max_depth <= min_depth) {
            return;
        }

        cv::Mat depth_8u;
        latest_depth.image.convertTo(
            depth_8u,
            CV_8UC1,
            255.0 / (max_depth - min_depth),
            -min_depth * 255.0 / (max_depth - min_depth));

        cv::Mat depth_color;
        cv::applyColorMap(depth_8u, depth_color, cv::COLORMAP_TURBO);

        cv::Mat depth_resized;
        if (depth_color.size() != latest_color.image.size()) {
            cv::resize(depth_color, depth_resized, latest_color.image.size());
        } else {
            depth_resized = depth_color;
        }

        cv::Mat overlay;
        cv::addWeighted(latest_color.image, 0.65, depth_resized, 0.35, 0.0, overlay);

        const std::string label =
            "pts_delta_ms=" + std::to_string(pts_delta_ns / 1'000'000.0) +
            " color=" + std::to_string(latest_color.image.cols) + "x" +
            std::to_string(latest_color.image.rows) + " depth=" +
            std::to_string(latest_depth.image.cols) + "x" +
            std::to_string(latest_depth.image.rows);
        cv::putText(overlay,
                    label,
                    cv::Point(24, 36),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.8,
                    cv::Scalar(255, 255, 255),
                    2);

        if (!cv::imwrite(output_path, overlay)) {
            std::cerr << "failed to write overlay image to " << output_path << "\n";
            app.request_stop();
            return;
        }

        last_rendered_color_pts = latest_color.pts_ns;
        last_rendered_depth_pts = latest_depth.pts_ns;
        ++rendered_pairs;

        if (rendered_pairs == 1 || rendered_pairs % 10 == 0) {
            std::cout << "rendered_pairs=" << rendered_pairs
                      << " output=" << output_path << "\n";
        }

        if (max_pairs > 0 && rendered_pairs >= max_pairs) {
            app.request_stop();
        }
    };

    app.route("orbbec/color")
        .expect(insightos::Video{})
        .on_caps([](const insightos::Caps& caps) {
            std::cout << "color caps: format=" << caps.format
                      << " size=" << caps.width << "x" << caps.height
                      << " fps=" << caps.fps << "\n";
        })
        .on_frame([&](const insightos::Frame& frame) {
            std::vector<std::uint8_t> encoded(frame.data, frame.data + frame.size);
            latest_color.image = cv::imdecode(encoded, cv::IMREAD_COLOR);
            latest_color.pts_ns = frame.pts_ns;
            maybe_render_overlay();
        });

    app.route("orbbec/depth")
        .expect(insightos::Depth{})
        .on_caps([](const insightos::Caps& caps) {
            std::cout << "depth caps: format=" << caps.format
                      << " size=" << caps.width << "x" << caps.height
                      << " fps=" << caps.fps << "\n";
        })
        .on_frame([&](const insightos::Frame& frame) {
            if (frame.width == 0 || frame.height == 0 ||
                frame.size < static_cast<std::size_t>(frame.width) * frame.height * 2) {
                return;
            }
            cv::Mat depth(static_cast<int>(frame.height),
                          static_cast<int>(frame.width),
                          CV_16UC1,
                          const_cast<std::uint8_t*>(frame.data));
            latest_depth.image = depth.clone();
            latest_depth.pts_ns = frame.pts_ns;
            maybe_render_overlay();
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
