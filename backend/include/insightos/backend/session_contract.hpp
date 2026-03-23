#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace insightos::backend {

enum class RequestOrigin {
    kUnknown,
    kUri,
    kHttpApi,
    kFrontend,
    kSdk,
    kInternal,
};

struct SessionOverrides {
    std::optional<std::uint32_t> audio_rate;
    std::optional<std::string> audio_format;
    std::optional<std::uint32_t> channels;
    std::optional<std::string> depth_alignment;
    bool must_match{false};
};

struct DeviceSelector {
    std::string name;
    std::string device_uuid;
};

struct SessionRequest {
    DeviceSelector selector;
    std::string preset_name;
    std::optional<std::string> delivery_name;
    RequestOrigin origin{RequestOrigin::kUnknown};
    SessionOverrides overrides;
};

using StreamOverrides = SessionOverrides;
using StreamRequest = SessionRequest;

struct ServiceSettings {
    std::string log_level{"info"};
    std::uint32_t idle_timeout_ms{30000};
    std::string ipc_socket_path;
    std::uint32_t video_buffer_slots{4};
    std::uint32_t depth_buffer_slots{4};
    std::uint32_t audio_buffer_slots{8};
    std::string http_bind{"127.0.0.1"};
    std::uint16_t http_port{18180};
    bool rtsp_enabled{true};
    std::string rtsp_listen_host{"0.0.0.0"};
    std::uint16_t rtsp_port{8554};
    std::string rtsp_local_host{"127.0.0.1"};
    std::string rtsp_lan_host{"auto"};
    std::string rtsp_publisher_transport{"tcp"};
    std::vector<std::string> rtsp_allowed_client_transports{"tcp", "udp"};
    std::string rtsp_path_strategy{"device"};
    std::string rtsp_endpoint_path_template{"{device}/{preset}/{stream}"};
    std::string rtsp_session_path_template{"session/{session_id}/{stream}"};
};

struct SessionPlan {
    SessionRequest request;
    std::string chosen_delivery;
    bool local_only{false};
};

}  // namespace insightos::backend
