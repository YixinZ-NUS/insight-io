#pragma once

/// InsightOS backend — Session runtime (CaptureSession + DeliverySession).
///
/// CaptureSession: keyed by physical_device_uri + preset, owns hardware
/// worker lifecycle. Multiple delivery sessions share one capture session.
///
/// DeliverySession: keyed by capture session + canonical stream id + delivery,
/// owns promise-specific transforms and publication (IPC/RTSP).
///
/// Design source: docs/design_doc/TECH_REPORT.md (session model)

#include "insightos/backend/catalog.hpp"
#include "insightos/backend/device_store.hpp"
#include "insightos/backend/result.hpp"
#include "insightos/backend/session_contract.hpp"
#include "insightos/backend/types.hpp"
#include "insightos/backend/worker.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <variant>
#include <vector>

namespace insightos::backend {

// Forward declaration for delivery publisher (implemented later)
class DeliveryPublisher;
class ChannelRegistry;

// ─── Capture reuse key ──────────────────────────────────────────────────

enum class TransportKind {
    kIpc,
    kRtsp,
};

struct CaptureReuseKey {
    std::string preset_id;
    std::string capture_policy_key;

    bool operator<(const CaptureReuseKey& o) const;
    bool operator==(const CaptureReuseKey& o) const;
    std::string to_string() const;
};

using CaptureKey = CaptureReuseKey;

// ─── Publication key ────────────────────────────────────────────────────

struct PublicationKey {
    std::string capture_session_id;
    std::string stream_key;
    std::string delivery_name;
    TransportKind transport_kind{TransportKind::kIpc};
    std::string public_stream_name;

    bool operator<(const PublicationKey& o) const;
    bool operator==(const PublicationKey& o) const;
    std::string to_string() const;
};

using DeliveryKey = PublicationKey;

// ─── Per-stream runtime state ───────────────────────────────────────────

struct StreamState {
    std::string stream_id;
    std::string stream_name;
    std::string promised_format;
    std::string actual_format;
    std::uint32_t actual_width{0};
    std::uint32_t actual_height{0};
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::uint32_t fps{0};
    std::uint64_t frame_count{0};
    std::uint32_t consumer_count{0};
    std::string transport;  // "ipc" or "rtsp"
    std::string rtsp_url;   // populated when transport is "rtsp"
};

struct IpcDescriptor {
    std::string socket_path;
    std::string session_id;
    std::string stream_name;
    std::string channel_id;
};

// ─── Session lifecycle state ────────────────────────────────────────────

enum class SessionState {
    kPending,
    kStarting,
    kActive,
    kStopping,
    kStopped,
    kError,
};

// ─── CaptureSession — owns the hardware ─────────────────────────────────

class CaptureSession {
public:
    CaptureSession(std::string capture_session_id, CaptureReuseKey key,
                   const DeviceInfo& device,
                   const ResolvedSession& resolution);

    const std::string& capture_session_id() const { return capture_session_id_; }
    const CaptureReuseKey& key() const { return key_; }
    SessionState state() const { return state_; }
    int ref_count() const { return ref_count_; }

    void add_ref() { ++ref_count_; }
    bool release();  // returns true when ref_count reaches 0

    bool start();
    void stop();

    using StreamSink = std::function<void(const uint8_t* data, size_t size,
                                             int64_t pts_ns, uint32_t flags)>;

    const DeviceInfo& device() const { return device_; }
    const ResolvedSession& resolution() const { return resolution_; }
    CaptureWorker* worker() { return worker_.get(); }

    void record_frame(std::string_view stream_name);
    std::uint64_t frame_count(std::string_view stream_name) const;
    std::uint64_t add_sink(std::string_view stream_name, StreamSink sink);
    void remove_sink(std::string_view stream_name, std::uint64_t sink_id);

private:
    std::string capture_session_id_;
    CaptureReuseKey key_;
    DeviceInfo device_;
    ResolvedSession resolution_;
    SessionState state_{SessionState::kPending};
    std::atomic<int> ref_count_{0};
    std::unique_ptr<CaptureWorker> worker_;
    mutable std::mutex frame_counts_mutex_;
    std::map<std::string, std::uint64_t> frame_counts_;
    mutable std::mutex sinks_mutex_;
    std::uint64_t next_sink_id_{1};
    std::map<std::string, std::vector<std::pair<std::uint64_t, StreamSink>>> sinks_;
};

// ─── DeliverySession — owns promise fulfillment ─────────────────────────

class DeliverySession {
public:
    DeliverySession(std::string delivery_session_id, PublicationKey key,
                    const ResolvedSession::StreamResolution& stream_res,
                    std::shared_ptr<CaptureSession> capture);

    const std::string& delivery_session_id() const { return delivery_session_id_; }
    const PublicationKey& key() const { return key_; }
    SessionState state() const { return state_; }
    const StreamState& stream_state() const { return stream_state_; }
    int ref_count() const { return ref_count_; }

    void add_ref() { ++ref_count_; }
    bool release();

    bool start(ChannelRegistry& registry);
    void stop();

    std::shared_ptr<CaptureSession> capture_session() const { return capture_; }

    void update_frame_count(std::uint64_t count) {
        stream_state_.frame_count = count;
    }

private:
    std::string delivery_session_id_;
    PublicationKey key_;
    StreamState stream_state_;
    SessionState state_{SessionState::kPending};
    std::shared_ptr<CaptureSession> capture_;
    std::atomic<int> ref_count_{0};
    std::shared_ptr<ipc::Writer> ipc_writer_;
    std::uint64_t ipc_sink_id_{0};
    int rtsp_stdin_fd_{-1};
    std::uint64_t rtsp_sink_id_{0};
    pid_t rtsp_pid_{-1};
};

// ─── StreamSession — returned from session creation ─────────────────────

struct StreamSession {
    std::string session_id;
    std::string state;
    SessionRequest request;
    std::string name;
    std::string device_uuid;
    std::string preset;
    std::string delivery;
    std::string host;
    std::string locality;  // "local" or "remote"
    std::string capture_session_id;
    std::string last_error;
    std::vector<StreamState> streams;
    std::map<std::string, IpcDescriptor> ipc_descriptors;
};

struct AppSourceView {
    std::string source_id;
    std::string target_name;
    std::string target_kind;
    std::string input;
    std::string canonical_uri;
    std::string state;
    std::string last_error;
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
    SessionRequest request;
    struct Binding {
        std::string role;
        std::string stream_id;
        std::string stream_name;
    };
    std::vector<Binding> bindings;
    std::optional<StreamSession> session;
};

struct AppTargetView {
    std::string target_id;
    std::string target_name;
    std::string target_kind;
    std::string contract_json{"{}"};
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

struct RuntimeAppView {
    std::string app_id;
    std::string name;
    std::string description;
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
    std::vector<AppTargetView> targets;
    std::vector<AppSourceView> sources;
};

// ─── SessionManager — coordinates capture + delivery lifecycle ──────────

class SessionManager {
public:
    explicit SessionManager(std::string db_path = {});
    ~SessionManager();

    /// Initialize with discovery + catalog build.
    bool initialize();

    /// Re-discover devices and rebuild catalog (preserves active sessions).
    bool refresh();

    const EndpointCatalog& catalog() const { return catalog_; }

    /// Create a session from a StreamRequest.
    std::variant<StreamSession, ResolutionError>
    create_session(const SessionRequest& request);

    /// Per-device status rollup.
    struct DeliveryStatus {
        std::string delivery;
        std::string stream_name;
        std::string state;
        std::uint32_t session_ref_count{0};
        StreamState stream_state;
    };
    struct StatusEntry {
        std::string name;
        std::string device_uuid;
        std::string preset;
        std::string capture_session_id;
        std::uint32_t capture_ref_count{0};
        std::vector<std::string> session_ids;
        std::vector<DeliveryStatus> delivery_sessions;
    };

    std::vector<StatusEntry> get_status() const;

    std::optional<StreamSession> get_session(const std::string& session_id) const;
    std::vector<StreamSession> list_sessions() const;
    Result<StreamSession> start_session(const std::string& session_id);
    Result<StreamSession> stop_session(const std::string& session_id);

    Result<RuntimeAppView> create_app(std::string name = {},
                                      std::string description = {});
    std::optional<RuntimeAppView> get_app(const std::string& app_id) const;
    std::vector<RuntimeAppView> list_apps() const;
    std::optional<std::vector<AppTargetView>> list_app_targets(
        const std::string& app_id) const;
    Result<AppTargetView> create_app_target(const std::string& app_id,
                                            std::string target_name,
                                            std::string target_kind);
    bool delete_app_target(const std::string& app_id,
                           const std::string& target_name);
    std::optional<std::vector<AppSourceView>> list_app_sources(
        const std::string& app_id) const;
    Result<AppSourceView> add_app_source(const std::string& app_id,
                                         std::string source_input,
                                         std::string target_name);
    Result<AppSourceView> start_app_source(const std::string& app_id,
                                           const std::string& source_id);
    Result<AppSourceView> stop_app_source(const std::string& app_id,
                                          const std::string& source_id,
                                          std::string last_error = {});
    bool destroy_app(const std::string& app_id);

    /// Destroy a session by ID; returns false if not found.
    bool destroy_session(const std::string& session_id);

    const std::vector<DeviceInfo>& devices() const;

    /// Set a new public device id; returns the updated id.
    Result<std::string> set_device_alias(const std::string& device_id,
                                         const std::string& alias);

    /// Restore the discovered default public device id.
    Result<std::string> clear_device_alias(const std::string& device_id);

    /// Set the public stream name for one stream on a public device.
    Result<std::string> set_stream_alias(const std::string& device_id,
                                         const std::string& stream_name,
                                         const std::string& alias);

    /// Restore the default public stream name for one stream on a public
    /// device.
    Result<std::string> clear_stream_alias(const std::string& device_id,
                                           const std::string& stream_name);

    void set_ipc_socket_path(std::string path);
    const std::string& database_path() const { return store_.path(); }

    struct IpcConsumerLease {
        PublicationKey key;
        StreamState stream_state;
        std::string channel_id;
        int memfd{-1};
        int eventfd{-1};
        int writer_eventfd{-1};
    };

    Result<IpcConsumerLease> attach_ipc_consumer(const std::string& session_id,
                                                 const std::string& stream_name);
    void detach_ipc_consumer(const PublicationKey& key, int writer_eventfd);

private:
    mutable std::mutex mutex_;
    DeviceStore store_;
    std::vector<DeviceInfo> devices_cache_;  // refreshed from store_ on demand
    EndpointCatalog catalog_;
    std::string ipc_socket_path_;
    std::unique_ptr<ChannelRegistry> channels_;

    std::string daemon_run_id_;
    std::map<std::string, std::shared_ptr<CaptureSession>> capture_sessions_;
    std::map<std::string, std::shared_ptr<DeliverySession>> delivery_sessions_;

    /// Sync devices_cache_ from store_ and rebuild catalog.
    void reload_devices_locked();

    std::string generate_session_id() const;
    std::string generate_runtime_id(std::string_view prefix) const;
    const DeviceInfo* find_device(const std::string& uri) const;
    std::string current_device_name_locked(std::string_view device_uuid,
                                           std::string_view fallback) const;
    const CatalogEndpoint* current_endpoint_locked(
        std::string_view device_uuid) const;
    Result<StreamSession> activate_session_locked(
        SessionRow& row, const SessionRequest& request,
        std::vector<std::string>& delivery_session_ids);
    void release_delivery_sessions_locked(
        const std::vector<std::string>& delivery_session_ids);
    StreamSession make_session_view_locked(
        const SessionRow& row,
        const std::vector<std::string>& delivery_session_ids) const;
    AppSourceView make_app_source_view_locked(const AppSourceRow& source) const;
    AppTargetView make_app_target_view_locked(const AppTargetRow& target) const;
    RuntimeAppView make_app_view_locked(const AppRow& app) const;
    Result<StreamSession> create_session_locked(const SessionRequest& request);
    Result<StreamSession> start_session_locked(const std::string& session_id);
    Result<StreamSession> stop_session_locked(const std::string& session_id);
    Result<AppSourceView> change_app_source_state_locked(
        const std::string& app_id, const std::string& source_id,
        bool start, std::string last_error = {});
    std::vector<std::string> load_delivery_session_ids_locked(
        const SessionRow& row) const;
    bool save_session_locked(const SessionRow& row,
                             const std::vector<std::string>& delivery_session_ids);
    bool destroy_session_locked(const std::string& session_id);
};

}  // namespace insightos::backend
