// role: persisted discovery catalog for the standalone backend.
// revision: 2026-03-27 live-orbbec-depth-catalog-followup
// major changes: restores donor-style depth-family format mapping in the
// Orbbec 480p catalog probe, keeps the proven 480p publication rule for the
// SV1301S_U3 family, intentionally leaves raw IR discovery out of the public
// v1 catalog contract, and preserves per-device selector uniqueness.
// See docs/past-tasks.md.

#include "insightio/backend/catalog.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

#ifdef INSIGHTIO_HAS_ORBBEC
#include <libobsensor/ObSensor.hpp>
#include <libobsensor/hpp/Error.hpp>
#include <libobsensor/hpp/Pipeline.hpp>
#endif

namespace insightio::backend {

namespace {

std::string rtsp_host_from_url(const std::string& url);

struct OrbbecCatalogProbe {
    bool probe_ran{false};
    bool supports_hw_d2c_480{false};
};

#ifdef INSIGHTIO_HAS_ORBBEC
std::string ob_format_to_string(OBFormat format) {
    switch (format) {
        case OB_FORMAT_YUYV:
            return "yuyv";
        case OB_FORMAT_UYVY:
            return "uyvy";
        case OB_FORMAT_NV12:
            return "nv12";
        case OB_FORMAT_NV21:
            return "nv21";
        case OB_FORMAT_MJPG:
            return "mjpeg";
        case OB_FORMAT_H264:
            return "h264";
        case OB_FORMAT_H265:
            return "h265";
        case OB_FORMAT_RGB:
            return "rgb24";
        case OB_FORMAT_BGR:
            return "bgr24";
        case OB_FORMAT_BGRA:
            return "bgra";
        case OB_FORMAT_RGBA:
            return "rgba";
        case OB_FORMAT_Y8:
        case OB_FORMAT_GRAY:
            return "gray8";
        case OB_FORMAT_Y10:
        case OB_FORMAT_Y11:
        case OB_FORMAT_Y12:
        case OB_FORMAT_Y14:
        case OB_FORMAT_Y16:
            return "y16";
        case OB_FORMAT_Z16:
            return "z16";
        default:
            return "unknown";
    }
}

std::string resolve_orbbec_config_path() {
    if (const char* env = std::getenv("ORBBEC_SDK_CONFIG")) {
        return std::string(env);
    }
#ifdef INSIGHTIO_ORBBEC_CONFIG_PATH
    return INSIGHTIO_ORBBEC_CONFIG_PATH;
#else
    return {};
#endif
}

void init_orbbec_logging() {
    static const bool initialized = []() {
        std::filesystem::create_directories("Log/orbbec");
        ob::Context::setLoggerToConsole(OB_LOG_SEVERITY_OFF);
        ob::Context::setLoggerToFile(OB_LOG_SEVERITY_WARN, "Log/orbbec/");
        return true;
    }();
    (void)initialized;
}

std::vector<ResolvedCaps> build_orbbec_caps(
    const std::shared_ptr<ob::StreamProfileList>& profiles) {
    std::vector<ResolvedCaps> caps;
    if (!profiles) {
        return caps;
    }

    std::set<std::string> seen;
    std::uint32_t index = 0;
    for (std::uint32_t i = 0; i < profiles->count(); ++i) {
        auto profile = profiles->getProfile(i);
        if (!profile || !profile->is<ob::VideoStreamProfile>()) {
            continue;
        }

        auto video = profile->as<ob::VideoStreamProfile>();
        const auto format = ob_format_to_string(video->format());
        if (format == "unknown") {
            continue;
        }

        const auto key = format + ":" + std::to_string(video->width()) + "x" +
                         std::to_string(video->height()) + "@" +
                         std::to_string(video->fps());
        if (!seen.insert(key).second) {
            continue;
        }

        caps.push_back(ResolvedCaps{
            .index = index++,
            .format = format,
            .width = video->width(),
            .height = video->height(),
            .fps = video->fps(),
        });
    }
    return caps;
}

std::string orbbec_target_from_uri(std::string_view uri) {
    constexpr std::string_view prefix = "orbbec://";
    if (uri.starts_with(prefix)) {
        return std::string(uri.substr(prefix.size()));
    }
    return std::string(uri);
}

std::shared_ptr<ob::Device> find_orbbec_device(ob::Context& context,
                                               std::string_view uri) {
    auto list = context.queryDeviceList();
    if (!list) {
        return nullptr;
    }

    const auto target = orbbec_target_from_uri(uri);
    for (std::uint32_t index = 0; index < list->deviceCount(); ++index) {
        auto handle = list->getDevice(index);
        if (!handle) {
            continue;
        }
        auto info = handle->getDeviceInfo();
        if (info == nullptr) {
            continue;
        }
        if (target == info->serialNumber() || target == info->uid()) {
            return handle;
        }
    }

    try {
        const auto index = static_cast<std::uint32_t>(std::stoul(target));
        if (index < list->deviceCount()) {
            return list->getDevice(index);
        }
    } catch (...) {
    }

    return nullptr;
}

std::shared_ptr<ob::VideoStreamProfile> find_video_profile(
    const std::shared_ptr<ob::StreamProfileList>& profiles,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t fps) {
    if (!profiles) {
        return nullptr;
    }
    for (std::uint32_t i = 0; i < profiles->count(); ++i) {
        auto profile = profiles->getProfile(i);
        if (!profile || !profile->is<ob::VideoStreamProfile>()) {
            continue;
        }
        auto video = profile->as<ob::VideoStreamProfile>();
        if (video->width() == width && video->height() == height &&
            video->fps() == fps) {
            return video;
        }
    }
    return nullptr;
}

OrbbecCatalogProbe probe_orbbec_catalog_480p(const DeviceInfo& device) {
    OrbbecCatalogProbe probe;
    try {
        init_orbbec_logging();
        const auto config_path = resolve_orbbec_config_path();
        ob::Context context(config_path.empty() ? "" : config_path.c_str());
        auto handle = find_orbbec_device(context, device.uri);
        if (!handle) {
            return probe;
        }

        probe.probe_ran = true;
        ob::Pipeline pipeline(handle);
        auto color_profiles = pipeline.getStreamProfileList(OB_SENSOR_COLOR);
        auto color_480 = find_video_profile(color_profiles, 640, 480, 30);
        if (!color_480) {
            return probe;
        }

        auto d2c_profiles =
            pipeline.getD2CDepthProfileList(color_480, ALIGN_D2C_HW_MODE);
        probe.supports_hw_d2c_480 = !build_orbbec_caps(d2c_profiles).empty();
    } catch (const ob::Error&) {
    } catch (const std::exception&) {
    }
    return probe;
}
#endif

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            stmt_ = nullptr;
        }
    }

    ~Stmt() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    explicit operator bool() const { return stmt_ != nullptr; }

    sqlite3_stmt* get() const { return stmt_; }

    void bind_text(int index, std::string_view value) {
        sqlite3_bind_text(stmt_, index, value.data(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    void bind_text_or_null(int index, const std::string& value) {
        if (value.empty()) {
            sqlite3_bind_null(stmt_, index);
        } else {
            bind_text(index, value);
        }
    }

    void bind_json_or_null(int index, const nlohmann::json& value) {
        if (value.is_null() || value.empty()) {
            sqlite3_bind_null(stmt_, index);
        } else {
            const auto dumped = value.dump();
            bind_text(index, dumped);
        }
    }

    void bind_int(int index, int value) {
        sqlite3_bind_int(stmt_, index, value);
    }

    void bind_int64(int index, std::int64_t value) {
        sqlite3_bind_int64(stmt_, index, value);
    }

    bool step() { return sqlite3_step(stmt_) == SQLITE_ROW; }

    bool exec() {
        const int rc = sqlite3_step(stmt_);
        return rc == SQLITE_DONE || rc == SQLITE_ROW;
    }

    std::string col_text(int index) const {
        const auto* text = sqlite3_column_text(stmt_, index);
        return text == nullptr ? std::string{}
                               : std::string(reinterpret_cast<const char*>(text));
    }

    std::int64_t col_int64(int index) const {
        return sqlite3_column_int64(stmt_, index);
    }

private:
    sqlite3_stmt* stmt_{nullptr};
};

class CatalogRepository {
public:
    explicit CatalogRepository(sqlite3* db) : db_(db) {}

    std::map<std::string, std::string> existing_public_names_by_key() const {
        std::map<std::string, std::string> names;
        Stmt query(db_,
                   "SELECT device_key, public_name FROM devices");
        if (!query) {
            return names;
        }

        while (query.step()) {
            names.emplace(query.col_text(0), query.col_text(1));
        }
        return names;
    }

    bool replace_catalog(const std::vector<CatalogDevice>& devices) const {
        if (!exec_sql("BEGIN IMMEDIATE TRANSACTION;")) {
            return false;
        }

        const auto timestamp = now_ms();
        if (!mark_all_offline(timestamp)) {
            exec_sql("ROLLBACK;");
            return false;
        }

        for (const auto& device : devices) {
            if (!upsert_device(device, timestamp)) {
                exec_sql("ROLLBACK;");
                return false;
            }

            const auto device_id = lookup_device_id(device.device_key);
            if (!device_id.has_value()) {
                exec_sql("ROLLBACK;");
                return false;
            }

            for (const auto& source : device.sources) {
                if (!upsert_source(*device_id, source, timestamp)) {
                    exec_sql("ROLLBACK;");
                    return false;
                }
            }
        }

        return exec_sql("COMMIT;");
    }

    std::vector<CatalogDevice> load_devices() const {
        std::vector<CatalogDevice> devices;
        Stmt query(db_,
                   "SELECT device_id, device_key, public_name, driver, status, metadata_json "
                   "FROM devices WHERE status != 'offline' ORDER BY public_name");
        if (!query) {
            return devices;
        }

        while (query.step()) {
            CatalogDevice device;
            const auto device_id = query.col_int64(0);
            device.device_key = query.col_text(1);
            device.public_name = query.col_text(2);
            device.driver = query.col_text(3);
            device.status = query.col_text(4);
            device.metadata_json = parse_json(query.col_text(5));
            device.default_name =
                device.metadata_json.value("default_name", std::string{});
            device.sources = load_sources(device_id, device.public_name);
            devices.push_back(std::move(device));
        }
        return devices;
    }

    std::optional<CatalogDevice> find_device(std::string_view public_name) const {
        Stmt query(db_,
                   "SELECT device_id, device_key, public_name, driver, status, metadata_json "
                   "FROM devices WHERE public_name = ? AND status != 'offline'");
        if (!query) {
            return std::nullopt;
        }
        query.bind_text(1, public_name);
        if (!query.step()) {
            return std::nullopt;
        }

        CatalogDevice device;
        const auto device_id = query.col_int64(0);
        device.device_key = query.col_text(1);
        device.public_name = query.col_text(2);
        device.driver = query.col_text(3);
        device.status = query.col_text(4);
        device.metadata_json = parse_json(query.col_text(5));
        device.default_name = device.metadata_json.value("default_name", std::string{});
        device.sources = load_sources(device_id, device.public_name);
        return device;
    }

    bool update_alias(std::string_view current_public_name,
                      const std::string& alias,
                      int& error_status,
                      std::string& error_code,
                      std::string& error_message) const {
        const auto normalized = slugify(alias);
        if (normalized.empty()) {
            error_status = 422;
            error_code = "invalid_alias";
            error_message =
                "Alias must contain at least one alphanumeric character";
            return false;
        }

        Stmt find(db_,
                  "SELECT device_key FROM devices WHERE public_name = ? AND status != 'offline'");
        if (!find) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to prepare alias lookup";
            return false;
        }
        find.bind_text(1, current_public_name);
        if (!find.step()) {
            error_status = 404;
            error_code = "not_found";
            error_message = "Device '" + std::string(current_public_name) + "' not found";
            return false;
        }

        Stmt conflict(db_,
                      "SELECT 1 FROM devices WHERE public_name = ? AND public_name != ?");
        if (!conflict) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to prepare alias conflict check";
            return false;
        }
        conflict.bind_text(1, normalized);
        conflict.bind_text(2, current_public_name);
        if (conflict.step()) {
            error_status = 409;
            error_code = "conflict";
            error_message = "Device alias '" + normalized + "' is already in use";
            return false;
        }

        Stmt update(db_,
                    "UPDATE devices SET public_name = ?, updated_at_ms = ? "
                    "WHERE public_name = ?");
        if (!update) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to prepare alias update";
            return false;
        }
        update.bind_text(1, normalized);
        update.bind_int64(2, now_ms());
        update.bind_text(3, current_public_name);
        if (!update.exec()) {
            error_status = 500;
            error_code = "internal";
            error_message = "Failed to update alias";
            return false;
        }

        return true;
    }

private:
    sqlite3* db_;

    static std::int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static nlohmann::json parse_json(const std::string& text) {
        if (text.empty()) {
            return nlohmann::json::object();
        }
        try {
            return nlohmann::json::parse(text);
        } catch (...) {
            return nlohmann::json::object();
        }
    }

    bool exec_sql(const std::string& sql) const {
        char* error = nullptr;
        const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
        if (rc == SQLITE_OK) {
            return true;
        }
        std::cerr << "[CatalogRepository] sqlite3_exec failed: "
                  << (error ? error : "unknown") << "\n";
        sqlite3_free(error);
        return false;
    }

    bool mark_all_offline(std::int64_t timestamp) const {
        Stmt devices(db_,
                     "UPDATE devices SET status = 'offline', updated_at_ms = ?");
        if (!devices) {
            return false;
        }
        devices.bind_int64(1, timestamp);
        if (!devices.exec()) {
            return false;
        }

        Stmt streams(db_,
                     "UPDATE streams SET is_present = 0, updated_at_ms = ?");
        if (!streams) {
            return false;
        }
        streams.bind_int64(1, timestamp);
        return streams.exec();
    }

    bool upsert_device(const CatalogDevice& device, std::int64_t timestamp) const {
        Stmt statement(
            db_,
            "INSERT INTO devices (device_key, public_name, driver, status, metadata_json, "
            "last_seen_at_ms, created_at_ms, updated_at_ms) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(device_key) DO UPDATE SET "
            "public_name = excluded.public_name, "
            "driver = excluded.driver, "
            "status = excluded.status, "
            "metadata_json = excluded.metadata_json, "
            "last_seen_at_ms = excluded.last_seen_at_ms, "
            "updated_at_ms = excluded.updated_at_ms");
        if (!statement) {
            return false;
        }

        statement.bind_text(1, device.device_key);
        statement.bind_text(2, device.public_name);
        statement.bind_text(3, device.driver);
        statement.bind_text(4, "online");
        statement.bind_text(5, device.metadata_json.dump());
        statement.bind_int64(6, timestamp);
        statement.bind_int64(7, timestamp);
        statement.bind_int64(8, timestamp);
        return statement.exec();
    }

    bool upsert_source(std::int64_t device_id,
                       const CatalogSource& source,
                       std::int64_t timestamp) const {
        Stmt statement(
            db_,
            "INSERT INTO streams (device_id, selector, media_kind, shape_kind, channel, "
            "group_key, caps_json, capture_policy_json, members_json, publications_json, "
            "is_present, created_at_ms, updated_at_ms) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?) "
            "ON CONFLICT(device_id, selector) DO UPDATE SET "
            "media_kind = excluded.media_kind, "
            "shape_kind = excluded.shape_kind, "
            "channel = excluded.channel, "
            "group_key = excluded.group_key, "
            "caps_json = excluded.caps_json, "
            "capture_policy_json = excluded.capture_policy_json, "
            "members_json = excluded.members_json, "
            "publications_json = excluded.publications_json, "
            "is_present = excluded.is_present, "
            "updated_at_ms = excluded.updated_at_ms");
        if (!statement) {
            return false;
        }

        statement.bind_int64(1, device_id);
        statement.bind_text(2, source.selector);
        statement.bind_text(3, source.media_kind);
        statement.bind_text(4, source.shape_kind);
        statement.bind_text_or_null(5, source.channel);
        statement.bind_text_or_null(6, source.group_key);
        statement.bind_text(7, source.caps_json.dump());
        statement.bind_json_or_null(8, source.capture_policy_json);
        statement.bind_json_or_null(9, source.members_json);
        statement.bind_text(10, source.publications_json.dump());
        statement.bind_int64(11, timestamp);
        statement.bind_int64(12, timestamp);
        return statement.exec();
    }

    std::optional<std::int64_t> lookup_device_id(std::string_view device_key) const {
        Stmt query(db_, "SELECT device_id FROM devices WHERE device_key = ?");
        if (!query) {
            return std::nullopt;
        }
        query.bind_text(1, device_key);
        if (!query.step()) {
            return std::nullopt;
        }
        return query.col_int64(0);
    }

    std::vector<CatalogSource> load_sources(std::int64_t device_id,
                                            const std::string& public_name) const {
        std::vector<CatalogSource> sources;
        Stmt query(
            db_,
            "SELECT selector, media_kind, shape_kind, channel, group_key, "
            "caps_json, capture_policy_json, members_json, publications_json "
            "FROM streams WHERE device_id = ? AND is_present = 1 ORDER BY selector");
        if (!query) {
            return sources;
        }
        query.bind_int64(1, device_id);
        while (query.step()) {
            CatalogSource source;
            source.selector = query.col_text(0);
            source.uri = "insightos://localhost/" + public_name + "/" + source.selector;
            source.media_kind = query.col_text(1);
            source.shape_kind = query.col_text(2);
            source.channel = query.col_text(3);
            source.group_key = query.col_text(4);
            source.caps_json = parse_json(query.col_text(5));
            source.capture_policy_json = parse_json(query.col_text(6));
            source.members_json = parse_json(query.col_text(7));
            source.publications_json = parse_json(query.col_text(8));
            if (source.publications_json.contains("rtsp") &&
                source.publications_json["rtsp"].contains("url") &&
                source.publications_json["rtsp"]["url"].is_string()) {
                const auto host = rtsp_host_from_url(
                    source.publications_json["rtsp"]["url"].get<std::string>());
                if (!host.empty()) {
                    source.publications_json["rtsp"]["url"] =
                        "rtsp://" + host + "/" + public_name + "/" + source.selector;
                }
            }
            sources.push_back(std::move(source));
        }
        return sources;
    }
};

std::string unique_public_name(std::string base, std::set<std::string>& used) {
    if (base.empty()) {
        base = "device";
    }
    if (!used.contains(base)) {
        used.insert(base);
        return base;
    }
    for (int suffix = 1;; ++suffix) {
        const auto candidate = base + "-" + std::to_string(suffix);
        if (!used.contains(candidate)) {
            used.insert(candidate);
            return candidate;
        }
    }
}

std::string driver_name(DeviceKind kind) {
    return to_string(kind);
}

std::string resolution_label(std::uint32_t width, std::uint32_t height) {
    if (width == 640 && height == 400) return "400p";
    if (width == 640 && height == 480) return "480p";
    if (width == 1280 && height == 720) return "720p";
    if (width == 1280 && height == 800) return "800p";
    if (width == 1920 && height == 1080) return "1080p";
    if (width == 3840 && height == 2160) return "2160p";
    return std::to_string(width) + "x" + std::to_string(height);
}

int video_format_rank(std::string_view format) {
    static const std::array<std::string_view, 6> order = {
        "hevc", "h265", "h264", "mjpeg", "yuyv", "rgb24"};
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (order[index] == format) {
            return static_cast<int>(index);
        }
    }
    return static_cast<int>(order.size());
}

int audio_format_rank(std::string_view format) {
    static const std::array<std::string_view, 4> order = {
        "s16le", "s24le", "s32le", "f32le"};
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (order[index] == format) {
            return static_cast<int>(index);
        }
    }
    return static_cast<int>(order.size());
}

const StreamInfo* find_stream(const DeviceInfo& device, std::string_view stream_id) {
    for (const auto& stream : device.streams) {
        if (stream.stream_id == stream_id) {
            return &stream;
        }
    }
    return nullptr;
}

nlohmann::json caps_to_json(const ResolvedCaps& caps) {
    nlohmann::json json = {
        {"format", caps.format},
        {"named", caps.to_named()},
    };
    if (caps.is_audio()) {
        json["sample_rate"] = caps.sample_rate();
        json["channels"] = caps.channels();
    } else {
        json["width"] = caps.width;
        json["height"] = caps.height;
        json["fps"] = caps.fps;
    }
    return json;
}

std::string rtsp_host_from_url(const std::string& url) {
    constexpr std::string_view prefix = "rtsp://";
    if (!url.starts_with(prefix)) {
        return {};
    }
    const auto host_start = prefix.size();
    const auto slash = url.find('/', host_start);
    if (slash == std::string::npos) {
        return url.substr(host_start);
    }
    return url.substr(host_start, slash - host_start);
}

CatalogSource make_source(const std::string& public_name,
                          const std::string& rtsp_host,
                          const std::string& selector,
                          const std::string& media_kind,
                          const std::string& shape_kind,
                          const ResolvedCaps& delivered_caps,
                          nlohmann::json capture_policy,
                          nlohmann::json members = nullptr,
                          std::string channel = {},
                          std::string group_key = {}) {
    CatalogSource source;
    source.selector = selector;
    source.uri = "insightos://localhost/" + public_name + "/" + selector;
    source.media_kind = media_kind;
    source.shape_kind = shape_kind;
    source.channel = std::move(channel);
    source.group_key = std::move(group_key);
    source.caps_json = caps_to_json(delivered_caps);
    source.capture_policy_json = std::move(capture_policy);
    source.members_json = std::move(members);
    source.publications_json = {
        {"rtsp",
         {
             {"url", "rtsp://" + rtsp_host + "/" + public_name + "/" + selector},
             {"profile", "default"},
         }},
    };
    return source;
}

std::vector<CatalogSource> build_v4l2_sources(const DeviceInfo& device,
                                              const std::string& public_name,
                                              const std::string& rtsp_host) {
    std::vector<CatalogSource> sources;
    const auto* stream = find_stream(device, "image");
    if (stream == nullptr) {
        return sources;
    }

    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, ResolvedCaps> chosen;
    for (const auto& caps : stream->supported_caps) {
        const auto key = std::make_tuple(caps.width, caps.height, caps.fps);
        auto it = chosen.find(key);
        if (it == chosen.end() || video_format_rank(caps.format) < video_format_rank(it->second.format)) {
            chosen[key] = caps;
        }
    }

    for (const auto& [key, caps] : chosen) {
        (void)key;
        const auto selector =
            resolution_label(caps.width, caps.height) + "_" + std::to_string(caps.fps);
        auto capture_policy = nlohmann::json{
            {"driver", "v4l2"},
            {"device_uri", device.uri},
            {"stream_id", stream->stream_id},
            {"stream_name", stream->name},
            {"selected_caps", caps_to_json(caps)},
        };
        sources.push_back(make_source(public_name,
                                      rtsp_host,
                                      selector,
                                      "video",
                                      "exact",
                                      caps,
                                      std::move(capture_policy)));
    }
    return sources;
}

std::vector<CatalogSource> build_pipewire_sources(const DeviceInfo& device,
                                                  const std::string& public_name,
                                                  const std::string& rtsp_host) {
    std::vector<CatalogSource> sources;
    const auto* stream = find_stream(device, "audio");
    if (stream == nullptr) {
        return sources;
    }

    std::map<std::uint32_t, ResolvedCaps> chosen;
    for (const auto& caps : stream->supported_caps) {
        const auto channels = caps.channels();
        if (channels == 0 || channels > 2) {
            continue;
        }
        auto it = chosen.find(channels);
        const bool better_rate = it == chosen.end() ||
                                 (caps.sample_rate() == 48000 &&
                                  it->second.sample_rate() != 48000) ||
                                 (caps.sample_rate() > it->second.sample_rate());
        const bool better_format = it == chosen.end() ||
                                   audio_format_rank(caps.format) <
                                       audio_format_rank(it->second.format);
        if (it == chosen.end() || better_rate || better_format) {
            chosen[channels] = caps;
        }
    }

    for (const auto& [channels, caps] : chosen) {
        const auto selector = channels == 1 ? "audio/mono" : "audio/stereo";
        auto capture_policy = nlohmann::json{
            {"driver", "pipewire"},
            {"device_uri", device.uri},
            {"stream_id", stream->stream_id},
            {"stream_name", stream->name},
            {"selected_caps", caps_to_json(caps)},
        };
        sources.push_back(make_source(public_name,
                                      rtsp_host,
                                      selector,
                                      "audio",
                                      "exact",
                                      caps,
                                      std::move(capture_policy)));
    }
    return sources;
}

bool contains_cap(const StreamInfo& stream,
                  std::uint32_t width,
                  std::uint32_t height,
                  std::uint32_t fps) {
    return std::any_of(stream.supported_caps.begin(), stream.supported_caps.end(),
                       [&](const ResolvedCaps& caps) {
                           return caps.width == width && caps.height == height &&
                                  caps.fps == fps;
                       });
}

const ResolvedCaps* find_cap(const StreamInfo& stream,
                             std::uint32_t width,
                             std::uint32_t height,
                             std::uint32_t fps) {
    for (const auto& caps : stream.supported_caps) {
        if (caps.width == width && caps.height == height && caps.fps == fps) {
            return &caps;
        }
    }
    return nullptr;
}

std::vector<CatalogSource> build_orbbec_sources(const DeviceInfo& device,
                                                const std::string& public_name,
                                                const std::string& rtsp_host) {
    std::vector<CatalogSource> sources;
    const auto* color = find_stream(device, "color");
    const auto* depth = find_stream(device, "depth");
#ifdef INSIGHTIO_HAS_ORBBEC
    const auto probe = probe_orbbec_catalog_480p(device);
#else
    const OrbbecCatalogProbe probe;
#endif
    if (color != nullptr) {
        for (const auto& caps : color->supported_caps) {
            const auto selector = "orbbec/color/" + resolution_label(caps.width, caps.height) +
                                  "_" + std::to_string(caps.fps);
            auto capture_policy = nlohmann::json{
                {"driver", "orbbec"},
                {"device_uri", device.uri},
                {"stream_id", "color"},
                {"selected_caps", caps_to_json(caps)},
            };
            sources.push_back(make_source(public_name,
                                          rtsp_host,
                                          selector,
                                          "video",
                                          "exact",
                                          caps,
                                          std::move(capture_policy),
                                          nullptr,
                                          "color",
                                          "orbbec-rgbd"));
        }
    }

    const auto* color_480 = color == nullptr ? nullptr : find_cap(*color, 640, 480, 30);
    // Publish aligned 480p and grouped RGBD entries only when one real depth
    // stream is present and the SDK probe either proved the D2C path or could
    // not run for this synthetic/non-live test device.
    const bool supports_480_family =
        color_480 != nullptr && depth != nullptr &&
        (probe.supports_hw_d2c_480 || !probe.probe_ran);
    if (depth != nullptr) {
        for (const auto& caps : depth->supported_caps) {
            const auto selector = "orbbec/depth/" + resolution_label(caps.width, caps.height) +
                                  "_" + std::to_string(caps.fps);
            auto capture_policy = nlohmann::json{
                {"driver", "orbbec"},
                {"device_uri", device.uri},
                {"stream_id", "depth"},
                {"selected_caps", caps_to_json(caps)},
                {"mode", "native_depth"},
                {"d2c", "off"},
            };
            sources.push_back(make_source(public_name,
                                          rtsp_host,
                                          selector,
                                          "depth",
                                          "exact",
                                          caps,
                                          std::move(capture_policy),
                                          nullptr,
                                          "depth",
                                          "orbbec-rgbd"));
        }

        if (const auto* native_400 = find_cap(*depth, 640, 400, 30);
            native_400 != nullptr && !contains_cap(*depth, 640, 480, 30) &&
            supports_480_family) {
            ResolvedCaps aligned = *native_400;
            aligned.width = 640;
            aligned.height = 480;
            auto capture_policy = nlohmann::json{
                {"driver", "orbbec"},
                {"device_uri", device.uri},
                {"stream_id", "depth"},
                {"native_caps", caps_to_json(*native_400)},
                {"selected_caps", caps_to_json(aligned)},
                {"mode", "aligned_depth"},
                {"d2c", "hardware"},
                {"comment", "Delivered 480p depth is a D2C-aligned output backed by native 400p capture"},
            };
            sources.push_back(make_source(public_name,
                                          rtsp_host,
                                          "orbbec/depth/480p_30",
                                          "depth",
                                          "exact",
                                          aligned,
                                          std::move(capture_policy),
                                          nullptr,
                                          "depth",
                                          "orbbec-rgbd"));
        }
    }

    if (color != nullptr && depth != nullptr) {
        const auto* depth_400 = find_cap(*depth, 640, 400, 30);
        if (color_480 != nullptr && depth_400 != nullptr &&
            supports_480_family) {
            ResolvedCaps grouped_caps = *color_480;
            auto capture_policy = nlohmann::json{
                {"driver", "orbbec"},
                {"device_uri", device.uri},
                {"mode", "grouped_preset"},
                {"preset", "480p_30"},
                {"color_caps", caps_to_json(*color_480)},
                {"depth_native_caps", caps_to_json(*depth_400)},
                {"depth_delivered_caps",
                 {
                     {"format", depth_400->format},
                     {"width", 640},
                     {"height", 480},
                     {"fps", 30},
                     {"named", std::string(depth_400->format) + "_640x480_30"},
                 }},
                {"d2c", "hardware"},
            };
            auto members = nlohmann::json::array({
                {
                    {"route", "orbbec/color"},
                    {"selector", "orbbec/color/480p_30"},
                    {"media", "video"},
                },
                {
                    {"route", "orbbec/depth"},
                    {"selector", "orbbec/depth/480p_30"},
                    {"media", "depth"},
                },
            });
            sources.push_back(make_source(public_name,
                                          rtsp_host,
                                          "orbbec/preset/480p_30",
                                          "grouped",
                                          "grouped",
                                          grouped_caps,
                                          std::move(capture_policy),
                                          std::move(members),
                                          {},
                                          "orbbec-rgbd"));
        }
    }

    return sources;
}

std::vector<CatalogSource> build_sources_for_device(const DeviceInfo& device,
                                                    const std::string& public_name,
                                                    const std::string& rtsp_host) {
    switch (device.kind) {
        case DeviceKind::kV4l2:
            return build_v4l2_sources(device, public_name, rtsp_host);
        case DeviceKind::kPipeWire:
            return build_pipewire_sources(device, public_name, rtsp_host);
        case DeviceKind::kOrbbec:
            return build_orbbec_sources(device, public_name, rtsp_host);
    }
    return {};
}

}  // namespace

CatalogService::CatalogService(SchemaStore& store,
                               DiscoveryFn discovery_fn,
                               std::string uri_host,
                               std::string rtsp_host)
    : store_(store),
      discovery_fn_(std::move(discovery_fn)),
      uri_host_(std::move(uri_host)),
      rtsp_host_(std::move(rtsp_host)) {}

bool CatalogService::initialize() {
    return refresh();
}

bool CatalogService::refresh() {
    auto discovery = discovery_fn_();
    CatalogRepository repository(store_.db());
    const auto existing_names = repository.existing_public_names_by_key();

    std::vector<CatalogDevice> devices;
    std::set<std::string> used_defaults;
    std::set<std::string> used_public_names;
    int fallback_index = 0;

    for (const auto& raw : discovery.devices) {
        CatalogDevice device;
        device.device_key = stable_device_key(raw);
        device.driver = driver_name(raw.kind);
        device.status = "online";

        const auto default_name =
            unique_public_name(public_device_id_base(raw, ++fallback_index), used_defaults);
        device.default_name = default_name;

        const auto existing = existing_names.find(device.device_key);
        if (existing != existing_names.end()) {
            device.public_name = unique_public_name(existing->second, used_public_names);
        } else {
            device.public_name = unique_public_name(default_name, used_public_names);
        }

        device.metadata_json = {
            {"default_name", device.default_name},
            {"hardware_name", raw.name},
            {"device_uri", raw.uri},
            {"description", raw.description},
            {"kind", driver_name(raw.kind)},
            {"usb_vendor_id", raw.identity.usb_vendor_id},
            {"usb_product_id", raw.identity.usb_product_id},
            {"usb_serial", raw.identity.usb_serial},
        };
        device.sources = build_sources_for_device(raw, device.public_name, rtsp_host_);
        std::sort(device.sources.begin(), device.sources.end(),
                  [](const CatalogSource& lhs, const CatalogSource& rhs) {
                      return lhs.selector < rhs.selector;
                  });
        devices.push_back(std::move(device));
    }

    if (!repository.replace_catalog(devices)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    devices_ = repository.load_devices();
    last_errors_ = std::move(discovery.errors);
    return true;
}

std::vector<CatalogDevice> CatalogService::list_devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_;
}

std::optional<CatalogDevice> CatalogService::get_device(std::string_view public_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& device : devices_) {
        if (device.public_name == public_name) {
            return device;
        }
    }
    return std::nullopt;
}

std::vector<std::string> CatalogService::last_errors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_errors_;
}

bool CatalogService::set_alias(std::string_view current_public_name,
                               const std::string& alias,
                               CatalogDevice& updated_device,
                               int& error_status,
                               std::string& error_code,
                               std::string& error_message) {
    CatalogRepository repository(store_.db());
    if (!repository.update_alias(current_public_name, alias, error_status, error_code,
                                 error_message)) {
        return false;
    }

    auto refreshed = repository.load_devices();
    auto updated = repository.find_device(slugify(alias));
    if (!updated.has_value()) {
        error_status = 500;
        error_code = "internal";
        error_message = "Alias updated but device could not be reloaded";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    devices_ = std::move(refreshed);
    updated_device = *updated;
    return true;
}

}  // namespace insightio::backend
