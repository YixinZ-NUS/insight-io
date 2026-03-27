#pragma once

// role: high-level route-oriented SDK for task-9 app callbacks.
// revision: 2026-03-27 task9-sdk-route-surface
// major changes: introduces the thin `insightos::App` route declaration,
// startup bind, running-app REST control, and IPC callback surface that sits
// on top of the standalone insight-io backend.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace insightos {

struct Expectation {
    std::string media;
    std::string channel_name;
};

struct Video : public Expectation {
    Video();
    Video& channel(std::string value);
};

struct Depth : public Expectation {
    Depth();
    Depth& channel(std::string value);
};

struct Audio : public Expectation {
    Audio();
    Audio& channel(std::string value);
};

struct Caps {
    std::string route_name;
    std::string media_kind;
    std::string channel_id;
    std::string stream_name;
    std::string selector;
    std::string format;
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t fps{0};
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::int64_t source_id{0};
    std::int64_t session_id{0};
};

struct Frame {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
    std::int64_t pts_ns{0};
    std::int64_t dts_ns{0};
    std::uint32_t flags{0};
    std::uint64_t sequence{0};

    std::string route_name;
    std::string media_kind;
    std::string channel_id;
    std::string stream_name;
    std::string selector;
    std::string format;
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t fps{0};
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::int64_t source_id{0};
    std::int64_t session_id{0};
    std::int64_t received_steady_ns{0};
    std::int64_t received_wall_ns{0};

    static constexpr std::uint32_t kFlagCapsChange = 0x01;
    static constexpr std::uint32_t kFlagStateChange = 0x02;
    static constexpr std::uint32_t kFlagEndOfStream = 0x04;

    [[nodiscard]] bool has_end_of_stream() const;
};

class App {
public:
    struct Options {
        std::string name;
        std::string description;
        std::string backend_host{"127.0.0.1"};
        std::uint16_t backend_port{18180};
        bool delete_on_shutdown{true};
        bool fail_fast_startup{false};
        int refresh_interval_ms{250};
    };

    class AppRoute {
    public:
        AppRoute& expect(const Expectation& expectation);
        AppRoute& on_caps(std::function<void(const Caps&)> callback);
        AppRoute& on_frame(std::function<void(const Frame&)> callback);
        AppRoute& on_stop(std::function<void()> callback);

    private:
        friend class App;
        AppRoute(App* app, std::size_t route_index);

        App* app_{nullptr};
        std::size_t route_index_{0};
    };

    App();
    explicit App(Options options);
    ~App();
    App(App&&) noexcept;
    App& operator=(App&&) noexcept;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    AppRoute route(std::string route_name);

    App& bind_source(std::string target,
                     std::string input,
                     bool rtsp_enabled = false);
    App& bind_source(std::string target,
                     std::int64_t session_id,
                     bool rtsp_enabled = false);
    bool rebind(std::string target, std::string input, bool rtsp_enabled = false);
    bool rebind(std::string target, std::int64_t session_id, bool rtsp_enabled = false);

    bool bind_from_cli(int argc, char** argv, int start_index = 1);

    int connect();
    int run();
    int run(int argc, char** argv);
    void request_stop();

    [[nodiscard]] std::optional<std::int64_t> app_id() const;
    [[nodiscard]] std::string app_name() const;
    [[nodiscard]] std::string last_error() const;

private:
    struct Impl;
    explicit App(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace insightos
