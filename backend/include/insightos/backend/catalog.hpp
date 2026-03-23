#pragma once

/// InsightOS backend — Public device catalog and preset resolution.
///
/// Maps discovered devices to public device ids and resolves presets into
/// concrete device configurations. This is the key abstraction between
/// donor device identity and the standalone public device surface.
///
/// Design source: docs/plan/standalone-project-plan.md (Milestone 1)
/// Donor evidence: iocontroller worker configs (commit 4032eb4)

#include "insightos/backend/session_contract.hpp"
#include "insightos/backend/types.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace insightos::backend {

// ─── Preset definition for a device type ────────────────────────────────

struct PresetSpec {
    std::string name;  // "1080p_30", "stereo", "480p_30"

    // V4L2 / Orbbec video constraints
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t fps{0};

    // PipeWire audio constraints
    std::uint32_t channels{0};
    std::uint32_t preferred_rate{0};
    std::uint32_t fallback_rate{0};

    // Orbbec grouped preset (color + depth together)
    struct OrbbecGroupSpec {
        std::uint32_t color_width{0}, color_height{0};
        std::uint32_t depth_width{0}, depth_height{0};
        std::uint32_t ir_width{0}, ir_height{0};
        std::uint32_t fps{0};
        D2CMode d2c_default{D2CMode::kOff};
    };
    std::optional<OrbbecGroupSpec> orbbec_group;

    // Default delivery for this preset
    std::string default_delivery;
};

// ─── Public catalog device ──────────────────────────────────────────────

struct CatalogEndpoint {
    std::string name;          // "web-camera", "desk", "device-1"
    std::string default_name;  // discovered-name-derived default id
    std::string device_key;    // opaque backend-owned identifier
    std::string device_uuid;   // stable deterministic UUID
    DeviceKind device_kind;
    std::string device_uri;    // actual device URI ("v4l2:/dev/video0")
    std::string hardware_name; // discovery-owned human-readable label
    std::vector<PresetSpec> presets;
    std::vector<std::string> supported_deliveries;
};

// ─── Resolution result ──────────────────────────────────────────────────

struct ResolvedSession {
    std::string name;
    std::string device_key;
    std::string device_uuid;
    std::string preset;
    std::string delivery;
    std::string device_uri;
    DeviceKind device_kind;

    struct StreamResolution {
        std::string stream_id;       // canonical worker/store id
        std::string stream_name;     // current public name
        ResolvedCaps chosen_caps;
        std::string promised_format; // what delivery promises
    };
    std::vector<StreamResolution> streams;

    D2CMode d2c{D2CMode::kOff};
};

// ─── Validation error ───────────────────────────────────────────────────

struct ResolutionError {
    std::string code;     // "device_not_found", "unknown_preset", …
    std::string message;
    std::vector<std::string> alternatives;
};

// ─── The catalog itself ─────────────────────────────────────────────────

class EndpointCatalog {
public:
    /// Build catalog from discovered devices.
    void build_from_discovery(const std::vector<DeviceInfo>& devices);

    /// All public catalog devices.
    const std::vector<CatalogEndpoint>& endpoints() const;

    /// Find public device by id, or nullptr.
    const CatalogEndpoint* find_endpoint(std::string_view name) const;
    const CatalogEndpoint* find_by_key(std::string_view key) const;
    const CatalogEndpoint* find_by_uuid(std::string_view uuid) const;

    /// Resolve a StreamRequest against discovered capabilities.
    std::variant<ResolvedSession, ResolutionError>
    resolve(const StreamRequest& request,
            const std::vector<DeviceInfo>& devices) const;

private:
    std::vector<CatalogEndpoint> endpoints_;

    // Device-type specific resolvers
    std::optional<ResolvedSession> resolve_v4l2(
        const CatalogEndpoint& ep, const PresetSpec& preset,
        const std::string& delivery, const StreamOverrides& overrides,
        const DeviceInfo& device) const;

    std::optional<ResolvedSession> resolve_pipewire(
        const CatalogEndpoint& ep, const PresetSpec& preset,
        const std::string& delivery, const StreamOverrides& overrides,
        const DeviceInfo& device) const;

    std::optional<ResolvedSession> resolve_orbbec(
        const CatalogEndpoint& ep, const PresetSpec& preset,
        const std::string& delivery, const StreamOverrides& overrides,
        const DeviceInfo& device) const;

    // Public device naming from device identity
    static std::string generate_endpoint_id(const DeviceInfo& device, int index);
};

}  // namespace insightos::backend
