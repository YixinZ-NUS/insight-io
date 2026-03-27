// role: task-9 example app for exact PipeWire audio route callbacks.
// revision: 2026-03-27 task9-sdk-route-audio-example
// major changes: demonstrates the route-based SDK on one audio route while
// reporting selector, sample-rate, channel-count, and simple level metrics.

#include "insightos/app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    insightos::App::Options options;
    int max_frames = 150;
    int report_every = 25;
    std::vector<std::string> route_args;
    route_args.emplace_back(argc > 0 ? argv[0] : "pipewire_audio_monitor");

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help") {
            std::cout
                << "Usage: pipewire_audio_monitor [options] [audio=]insightos://...\n"
                << "       pipewire_audio_monitor [options] audio=session:<id>\n"
                << "\n"
                << "Examples:\n"
                << "  pipewire_audio_monitor insightos://localhost/web-camera-mono/audio/mono\n"
                << "  pipewire_audio_monitor audio=insightos://localhost/web-camera-mono/audio/stereo\n"
                << "\n"
                << "Options:\n"
                << "  --app-name=<name>\n"
                << "  --backend-host=<host>\n"
                << "  --backend-port=<port>\n"
                << "  --max-frames=<count>\n"
                << "  --report-every=<count>\n"
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
        if (arg.rfind("--report-every=", 0) == 0) {
            report_every =
                std::stoi(arg.substr(std::string("--report-every=").size()));
            continue;
        }
        route_args.push_back(arg);
    }

    insightos::App app(options);

    int frames_seen = 0;
    std::uint64_t samples_seen = 0;
    double max_peak = 0.0;

    app.route("audio")
        .expect(insightos::Audio{})
        .on_caps([](const insightos::Caps& caps) {
            std::cout << "audio caps: selector=" << caps.selector
                      << " format=" << caps.format
                      << " sample_rate=" << caps.sample_rate
                      << " channels=" << caps.channels << "\n";
        })
        .on_frame([&](const insightos::Frame& frame) {
            ++frames_seen;

            std::uint64_t frame_samples = 0;
            double frame_rms = 0.0;
            double frame_peak = 0.0;

            if (frame.format == "s16le" && frame.size >= 2) {
                double sum_squares = 0.0;
                for (std::size_t offset = 0; offset + 1 < frame.size; offset += 2) {
                    const auto lo = static_cast<std::uint16_t>(frame.data[offset]);
                    const auto hi =
                        static_cast<std::uint16_t>(frame.data[offset + 1]) << 8;
                    const auto sample =
                        static_cast<std::int16_t>(static_cast<std::uint16_t>(lo | hi));
                    const double normalized = std::clamp(
                        static_cast<double>(sample) / 32768.0,
                        -1.0,
                        1.0);
                    sum_squares += normalized * normalized;
                    frame_peak = std::max(frame_peak, std::abs(normalized));
                    ++frame_samples;
                }
                if (frame_samples > 0) {
                    frame_rms = std::sqrt(sum_squares /
                                          static_cast<double>(frame_samples));
                }
                samples_seen += frame_samples;
                max_peak = std::max(max_peak, frame_peak);
            }

            if (frames_seen == 1 || (report_every > 0 && frames_seen % report_every == 0)) {
                std::cout << "frame=" << frames_seen
                          << " selector=" << frame.selector
                          << " format=" << frame.format
                          << " sample_rate=" << frame.sample_rate
                          << " channels=" << frame.channels
                          << " samples=" << frame_samples
                          << " rms=" << frame_rms
                          << " peak=" << frame_peak
                          << " max_peak=" << max_peak
                          << " recv_wall_ms=" << (frame.received_wall_ns / 1'000'000)
                          << " total_samples=" << samples_seen << "\n";
            }

            if (max_frames > 0 && frames_seen >= max_frames) {
                app.request_stop();
            }
        })
        .on_stop([]() { std::cout << "audio route stopped\n"; });

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
