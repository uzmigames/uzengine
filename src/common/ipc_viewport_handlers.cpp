// ipc_viewport_handlers.cpp -- IPC handler implementations for viewport.* commands.
//
// Each handler receives a JSON "args" object and returns a JSON result.
// All handlers capture the ViewportRegistry* by value (pointer copy). The
// registry must remain valid for the lifetime of the IPC registry.
//
// Handlers:
//   viewport.create    -- {type, width, height, target_fps, jpeg_quality, debug_name, vram_bytes}
//                         -> {ok, viewport_id}
//   viewport.destroy   -- {viewport_id} -> {ok}
//   viewport.setActive -- {viewport_id, active: bool} -> {ok, error_code}
//   viewport.resize    -- {viewport_id, width, height} -> {ok}
//   viewport.getStatus -- {viewport_id} -> {ok, status: {...}}
//   viewport.list      -- {} -> {ok, viewports: [...]}

#include "common/ipc_viewport_handlers.h"
#include "common/ipc_registry.h"
#include "engine/renderer/core/viewport_registry.h"

#include <nlohmann/json.hpp>

#include "common/safe_string.h"
#include <string>

namespace uz {

namespace {

/// Build a standard error response.
nlohmann::json make_error(const char* msg) {
    return {{"ok", false}, {"error", msg}};
}

/// Convert ViewportState enum to string.
const char* state_to_string(ViewportState s) {
    switch (s) {
        case ViewportState::Inactive:  return "inactive";
        case ViewportState::Active:    return "active";
        case ViewportState::Suspended: return "suspended";
        case ViewportState::Destroyed: return "destroyed";
    }
    return "unknown";
}

/// Convert ViewportType enum to string.
const char* type_to_string(ViewportType t) {
    switch (t) {
        case ViewportType::Scene:      return "scene";
        case ViewportType::VfxPreview: return "vfx_preview";
        case ViewportType::Material:   return "material";
        case ViewportType::Mesh:       return "mesh";
        case ViewportType::Texture:    return "texture";
        case ViewportType::Prefab:     return "prefab";
        case ViewportType::Animation:  return "animation";
    }
    return "unknown";
}

/// Parse ViewportType from string. Returns Scene on unknown input.
ViewportType type_from_string(const std::string& s) {
    if (s == "scene")       return ViewportType::Scene;
    if (s == "vfx_preview") return ViewportType::VfxPreview;
    if (s == "material")    return ViewportType::Material;
    if (s == "mesh")        return ViewportType::Mesh;
    if (s == "texture")     return ViewportType::Texture;
    if (s == "prefab")      return ViewportType::Prefab;
    if (s == "animation")   return ViewportType::Animation;
    return ViewportType::Scene;
}

/// Convert ViewportError enum to string.
const char* error_to_string(ViewportError e) {
    switch (e) {
        case ViewportError::None:           return "none";
        case ViewportError::InvalidId:      return "invalid_id";
        case ViewportError::CapReached:     return "cap_reached";
        case ViewportError::VramBudget:     return "vram_budget";
        case ViewportError::AlreadyInState: return "already_in_state";
    }
    return "unknown";
}

} // anonymous namespace

void viewport_register_handlers(IpcRegistry& registry, ViewportRegistry* reg) {

    // ----------------------------------------------------------------
    // viewport.create -- {type?, width?, height?, target_fps?, jpeg_quality?,
    //                     debug_name?, vram_bytes?} -> {ok, viewport_id}
    // ----------------------------------------------------------------
    registry.add("viewport.create", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no viewport registry");
        }

        ViewportDesc desc;

        if (args.contains("type") && args["type"].is_string()) {
            desc.type = type_from_string(args["type"].get<std::string>());
        }
        if (args.contains("width") && args["width"].is_number_integer()) {
            desc.width = args["width"].get<uint32_t>();
        }
        if (args.contains("height") && args["height"].is_number_integer()) {
            desc.height = args["height"].get<uint32_t>();
        }
        if (args.contains("target_fps") && args["target_fps"].is_number_integer()) {
            desc.target_fps = args["target_fps"].get<uint32_t>();
        }
        if (args.contains("jpeg_quality") && args["jpeg_quality"].is_number_integer()) {
            desc.jpeg_quality = args["jpeg_quality"].get<uint32_t>();
        }
        if (args.contains("vram_bytes") && args["vram_bytes"].is_number_integer()) {
            desc.vram_bytes = args["vram_bytes"].get<uint64_t>();
        }
        if (args.contains("debug_name") && args["debug_name"].is_string()) {
            std::string name = args["debug_name"].get<std::string>();
            // Safe copy into fixed-size buffer.
            uz::str_copy(desc.debug_name, sizeof(desc.debug_name), name.c_str());
        }

        const ViewportId id = reg->create(desc);
        if (id == kViewportIdInvalid) {
            return make_error("registry full");
        }

        return {{"ok", true}, {"viewport_id", id}};
    });

    // ----------------------------------------------------------------
    // viewport.destroy -- {viewport_id} -> {ok}
    // ----------------------------------------------------------------
    registry.add("viewport.destroy", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no viewport registry");
        }

        if (!args.contains("viewport_id") || !args["viewport_id"].is_number_integer()) {
            return make_error("missing or invalid 'viewport_id'");
        }

        const ViewportId id = args["viewport_id"].get<uint32_t>();
        const bool ok = reg->destroy(id);
        if (!ok) {
            return make_error("invalid viewport_id");
        }

        return {{"ok", true}};
    });

    // ----------------------------------------------------------------
    // viewport.setActive -- {viewport_id, active: bool} -> {ok, error_code?}
    // ----------------------------------------------------------------
    registry.add("viewport.setActive", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no viewport registry");
        }

        if (!args.contains("viewport_id") || !args["viewport_id"].is_number_integer()) {
            return make_error("missing or invalid 'viewport_id'");
        }
        if (!args.contains("active") || !args["active"].is_boolean()) {
            return make_error("missing or invalid 'active'");
        }

        const ViewportId id = args["viewport_id"].get<uint32_t>();
        const bool active = args["active"].get<bool>();

        const ViewportError err = reg->set_active(id, active);
        if (err != ViewportError::None) {
            return {
                {"ok", false},
                {"error_code", error_to_string(err)}
            };
        }

        return {{"ok", true}};
    });

    // ----------------------------------------------------------------
    // viewport.resize -- {viewport_id, width, height} -> {ok}
    // ----------------------------------------------------------------
    registry.add("viewport.resize", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no viewport registry");
        }

        if (!args.contains("viewport_id") || !args["viewport_id"].is_number_integer()) {
            return make_error("missing or invalid 'viewport_id'");
        }
        if (!args.contains("width") || !args["width"].is_number_integer()) {
            return make_error("missing or invalid 'width'");
        }
        if (!args.contains("height") || !args["height"].is_number_integer()) {
            return make_error("missing or invalid 'height'");
        }

        const ViewportId id = args["viewport_id"].get<uint32_t>();
        const uint32_t w = args["width"].get<uint32_t>();
        const uint32_t h = args["height"].get<uint32_t>();

        const bool ok = reg->resize(id, w, h);
        if (!ok) {
            return make_error("invalid viewport_id");
        }

        return {{"ok", true}};
    });

    // ----------------------------------------------------------------
    // viewport.getStatus -- {viewport_id} -> {ok, status}
    // ----------------------------------------------------------------
    registry.add("viewport.getStatus", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no viewport registry");
        }

        if (!args.contains("viewport_id") || !args["viewport_id"].is_number_integer()) {
            return make_error("missing or invalid 'viewport_id'");
        }

        const ViewportId id = args["viewport_id"].get<uint32_t>();

        ViewportStatus status;
        const bool found = reg->get_status(id, status);
        if (!found) {
            return make_error("invalid viewport_id");
        }

        return {
            {"ok", true},
            {"status", {
                {"id",          status.id},
                {"state",       state_to_string(status.state)},
                {"type",        type_to_string(status.type)},
                {"width",       status.width},
                {"height",      status.height},
                {"frame_count", status.frame_count},
                {"vram_bytes",  status.vram_bytes},
                {"fps",         status.fps}
            }}
        };
    });

    // ----------------------------------------------------------------
    // viewport.list -- {} -> {ok, viewports: [...], active_count, total_count}
    // ----------------------------------------------------------------
    registry.add("viewport.list", [reg](const nlohmann::json& /*args*/) -> nlohmann::json {
        if (!reg) {
            return make_error("no viewport registry");
        }

        // Iterate over possible IDs. Since we use monotonic IDs, we query
        // status for each potential id. We use a simple approach: try IDs
        // 1..next_id (which we can estimate from total_count).
        // More robust: just iterate by trying IDs we've seen.
        // For simplicity, collect up to kMaxViewports worth of statuses.
        auto viewports_arr = nlohmann::json::array();

        // We scan IDs starting from 1 up to a reasonable bound.
        // The registry uses monotonic IDs, so the max possible ID is bounded
        // by total_count + destroyed count. We try up to a generous bound.
        for (uint32_t probe_id = 1; probe_id <= kMaxViewports * 64; ++probe_id) {
            ViewportStatus status;
            if (reg->get_status(probe_id, status)) {
                viewports_arr.push_back({
                    {"id",          status.id},
                    {"state",       state_to_string(status.state)},
                    {"type",        type_to_string(status.type)},
                    {"width",       status.width},
                    {"height",      status.height},
                    {"frame_count", status.frame_count},
                    {"vram_bytes",  status.vram_bytes},
                    {"fps",         status.fps}
                });
            }

            // Early exit: if we've found all live viewports, stop scanning.
            if (viewports_arr.size() == reg->total_count()) {
                break;
            }
        }

        return {
            {"ok", true},
            {"viewports",    viewports_arr},
            {"active_count", reg->active_count()},
            {"total_count",  reg->total_count()}
        };
    });
}

} // namespace uz
