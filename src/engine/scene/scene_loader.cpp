// scene_loader.cpp — .uzscene INI-like parser implementation.
//
// Parses the .uzscene text format into an initialized Scene structure.
// See scene_loader.h for format documentation.

#include "engine/scene/scene_loader.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"

#include <cstdio>
#include <cstdlib>
#include "common/safe_string.h"
#include <string>
#include <sstream>

namespace uz {

// ============================================================================
// Internal helpers
// ============================================================================

/// Trim leading and trailing whitespace from a string in-place (returns a view
/// via start/end pointers into the original buffer). We return a std::string
/// for convenience since we are not on a hot path.
static std::string trim(const std::string& s) {
    const char* begin = s.c_str();
    const char* end   = begin + s.size();

    while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r')) {
        ++begin;
    }
    while (end > begin && (*(end - 1) == ' ' || *(end - 1) == '\t' || *(end - 1) == '\r')) {
        --end;
    }

    return std::string(begin, static_cast<size_t>(end - begin));
}

/// Parse a single float from a string. Advances `ptr` past the consumed
/// characters and any trailing whitespace. Returns false if the parse fails.
static bool parse_float(const char*& ptr, float& out) {
    // Skip leading whitespace.
    while (*ptr == ' ' || *ptr == '\t') {
        ++ptr;
    }

    if (*ptr == '\0') {
        return false;
    }

    char* endptr = nullptr;
    out = std::strtof(ptr, &endptr);

    // strtof sets endptr == ptr when no conversion was performed.
    if (endptr == ptr) {
        return false;
    }

    ptr = endptr;
    return true;
}

/// Parse exactly `count` floats separated by whitespace from `value_str`.
/// Returns false if there are fewer than `count` valid floats or if extra
/// non-whitespace characters remain after parsing.
static bool parse_floats(const char* value_str, float* out, int count) {
    const char* ptr = value_str;

    for (int i = 0; i < count; ++i) {
        if (!parse_float(ptr, out[i])) {
            return false;
        }
    }

    // Verify no extra non-whitespace tokens remain.
    while (*ptr == ' ' || *ptr == '\t') {
        ++ptr;
    }
    if (*ptr != '\0') {
        return false;
    }

    return true;
}

/// Parse a boolean value: "true" or "1" -> true, anything else -> false.
static bool parse_bool(const std::string& value) {
    return value == "true" || value == "1";
}

// ============================================================================
// Pending node accumulator
// ============================================================================

/// Accumulated state for a node being parsed from a [node] section.
struct PendingNode {
    std::string name;
    std::string type_str;
    std::string parent_name;
    float position[3];
    float rotation[4];
    float scale_val[3];
    bool  has_position;
    bool  has_rotation;
    bool  has_scale;
    bool  active;
    bool  visible;
    bool  has_active;
    bool  has_visible;

    // Camera component data.
    float fov_y;
    float near_plane;
    float far_plane;
    bool  has_fov;
    bool  has_near;
    bool  has_far;

    // DirectionalLight component data.
    float light_color[3];
    float light_intensity;
    bool  has_light_color;
    bool  has_light_intensity;

    // StaticMesh component data.
    std::string mesh_path;
    std::string material_path;
    bool has_mesh;
    bool has_material;

    void reset() {
        name.clear();
        type_str.clear();
        parent_name.clear();
        position[0] = 0.0f; position[1] = 0.0f; position[2] = 0.0f;
        rotation[0] = 0.0f; rotation[1] = 0.0f; rotation[2] = 0.0f; rotation[3] = 1.0f;
        scale_val[0] = 1.0f; scale_val[1] = 1.0f; scale_val[2] = 1.0f;
        has_position = false;
        has_rotation = false;
        has_scale    = false;
        active       = true;
        visible      = true;
        has_active   = false;
        has_visible  = false;
        fov_y         = 45.0f;
        near_plane    = 0.1f;
        far_plane     = 500.0f;
        has_fov       = false;
        has_near      = false;
        has_far       = false;
        light_color[0] = 1.0f; light_color[1] = 1.0f; light_color[2] = 1.0f;
        light_intensity = 1.0f;
        has_light_color     = false;
        has_light_intensity = false;
        mesh_path.clear();
        material_path.clear();
        has_mesh = false;
        has_material = false;
    }
};

/// Flush a completed PendingNode into the Scene. Returns an error string on
/// failure, or an empty string on success.
static std::string flush_node(Scene* scene, const PendingNode& pending) {
    // Determine the node type.
    NodeType type = NodeType::Node3D;
    if (!pending.type_str.empty()) {
        type = node_type_from_string(pending.type_str.c_str());
    }

    // Resolve the parent by name.
    NodeId parent_id = kNodeIdInvalid;
    if (!pending.parent_name.empty()) {
        parent_id = scene_find_by_name(scene, pending.parent_name.c_str());
        if (parent_id == kNodeIdInvalid) {
            return std::string("Parent node not found: ") + pending.parent_name;
        }
    }

    // Create the node.
    const char* node_name = pending.name.empty() ? "Unnamed" : pending.name.c_str();
    NodeId id = scene_create_node(scene, node_name, type, parent_id);
    if (id == kNodeIdInvalid) {
        return std::string("Failed to create node: ") + node_name
               + " (scene may be full)";
    }

    // Set the transform.
    SceneNode* node = scene_get_node(scene, id);
    if (!node) {
        return std::string("Internal error: created node is inaccessible");
    }

    if (pending.has_position) {
        node->local_transform.position = {
            pending.position[0], pending.position[1], pending.position[2], 0.0f
        };
    }

    if (pending.has_rotation) {
        node->local_transform.rotation = {
            pending.rotation[0], pending.rotation[1],
            pending.rotation[2], pending.rotation[3]
        };
    }

    if (pending.has_scale) {
        node->local_transform.scale = {
            pending.scale_val[0], pending.scale_val[1], pending.scale_val[2], 0.0f
        };
    }

    // Set flags.
    uint16_t flags = 0;
    if (pending.has_active) {
        if (pending.active)  flags |= NODE_FLAG_ACTIVE;
    } else {
        flags |= NODE_FLAG_ACTIVE; // Default: active.
    }

    if (pending.has_visible) {
        if (pending.visible) flags |= NODE_FLAG_VISIBLE;
    } else {
        flags |= NODE_FLAG_VISIBLE; // Default: visible.
    }

    node->flags = flags;

    // Apply component-specific data via ComponentStore.
    if (type == NodeType::Camera) {
        CameraData* cd = component_add<CameraData>(&scene->components, id);
        if (cd) {
            if (pending.has_fov)  cd->fov_y      = pending.fov_y;
            if (pending.has_near) cd->near_plane  = pending.near_plane;
            if (pending.has_far)  cd->far_plane   = pending.far_plane;
        }
    }

    if (type == NodeType::DirectionalLight) {
        DirectionalLightData* ld = component_add<DirectionalLightData>(&scene->components, id);
        if (ld) {
            if (pending.has_light_color) {
                ld->color = {
                    pending.light_color[0], pending.light_color[1],
                    pending.light_color[2], 0.0f
                };
            }
            if (pending.has_light_intensity) {
                ld->intensity = pending.light_intensity;
            }
        }
    }

    if (type == NodeType::StaticMesh) {
        StaticMeshData* smd = component_add<StaticMeshData>(&scene->components, id);
        if (smd) {
            if (pending.has_mesh) {
                uz::str_copy(smd->mesh_path, sizeof(smd->mesh_path), pending.mesh_path.c_str());
            }
            if (pending.has_material) {
                uz::str_copy(smd->material_path, sizeof(smd->material_path), pending.material_path.c_str());
            }
        }
    }

    return {};
}

// ============================================================================
// Core parser
// ============================================================================

SceneLoadResult scene_load_from_string(Scene* scene, const char* text) {
    if (!scene) {
        return SceneLoadResult::Err("scene_load_from_string: scene is null");
    }
    if (!text) {
        return SceneLoadResult::Err("scene_load_from_string: text is null");
    }

    scene_clear(scene);

    std::istringstream stream(text);
    std::string line;

    enum class Section { None, UzScene, Node, Editor };
    Section current_section = Section::None;

    PendingNode pending;
    pending.reset();
    bool has_pending_node = false;

    int line_number = 0;

    while (std::getline(stream, line)) {
        ++line_number;

        // Trim the line.
        std::string trimmed = trim(line);

        // Skip blank lines.
        if (trimmed.empty()) {
            continue;
        }

        // Skip comments.
        if (trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        // Check for section headers.
        if (trimmed[0] == '[') {
            // Flush any pending node before switching sections.
            if (has_pending_node) {
                std::string err = flush_node(scene, pending);
                if (!err.empty()) {
                    return SceneLoadResult::Err(
                        std::string("Line ") + std::to_string(line_number)
                        + ": " + err);
                }
                pending.reset();
                has_pending_node = false;
            }

            if (trimmed == "[uzscene]") {
                current_section = Section::UzScene;
            } else if (trimmed == "[node]") {
                current_section = Section::Node;
                pending.reset();
                has_pending_node = true;
            } else if (trimmed == "[editor]") {
                current_section = Section::Editor;
            } else {
                // Unknown section — silently skip (forward-compatible).
                current_section = Section::None;
            }

            continue;
        }

        // Parse "key = value" property lines.
        auto eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) {
            // Not a key=value line. Skip silently.
            continue;
        }

        std::string key   = trim(trimmed.substr(0, eq_pos));
        std::string value = trim(trimmed.substr(eq_pos + 1));

        if (current_section == Section::UzScene) {
            if (key == "name") {
                uz::str_copy(scene->name, sizeof(scene->name), value.c_str());
            } else if (key == "version") {
                char* endptr = nullptr;
                long v = std::strtol(value.c_str(), &endptr, 10);
                if (endptr != value.c_str()) {
                    scene->version = static_cast<uint32_t>(v);
                }
            }
            // Unknown keys in [uzscene] are silently ignored.
        } else if (current_section == Section::Node) {
            if (key == "name") {
                pending.name = value;
            } else if (key == "type") {
                pending.type_str = value;
            } else if (key == "parent") {
                pending.parent_name = value;
            } else if (key == "position") {
                float vals[3];
                if (!parse_floats(value.c_str(), vals, 3)) {
                    return SceneLoadResult::Err(
                        std::string("Line ") + std::to_string(line_number)
                        + ": position requires exactly 3 floats");
                }
                pending.position[0] = vals[0];
                pending.position[1] = vals[1];
                pending.position[2] = vals[2];
                pending.has_position = true;
            } else if (key == "rotation") {
                float vals[4];
                if (!parse_floats(value.c_str(), vals, 4)) {
                    return SceneLoadResult::Err(
                        std::string("Line ") + std::to_string(line_number)
                        + ": rotation requires exactly 4 floats");
                }
                pending.rotation[0] = vals[0];
                pending.rotation[1] = vals[1];
                pending.rotation[2] = vals[2];
                pending.rotation[3] = vals[3];
                pending.has_rotation = true;
            } else if (key == "scale") {
                float vals[3];
                if (!parse_floats(value.c_str(), vals, 3)) {
                    return SceneLoadResult::Err(
                        std::string("Line ") + std::to_string(line_number)
                        + ": scale requires exactly 3 floats");
                }
                pending.scale_val[0] = vals[0];
                pending.scale_val[1] = vals[1];
                pending.scale_val[2] = vals[2];
                pending.has_scale = true;
            } else if (key == "active") {
                pending.active     = parse_bool(value);
                pending.has_active = true;
            } else if (key == "visible") {
                pending.visible     = parse_bool(value);
                pending.has_visible = true;
            } else if (key == "fov") {
                char* endptr = nullptr;
                float v = std::strtof(value.c_str(), &endptr);
                if (endptr != value.c_str()) {
                    pending.fov_y   = v;
                    pending.has_fov = true;
                }
            } else if (key == "near") {
                char* endptr = nullptr;
                float v = std::strtof(value.c_str(), &endptr);
                if (endptr != value.c_str()) {
                    pending.near_plane = v;
                    pending.has_near   = true;
                }
            } else if (key == "far") {
                char* endptr = nullptr;
                float v = std::strtof(value.c_str(), &endptr);
                if (endptr != value.c_str()) {
                    pending.far_plane = v;
                    pending.has_far   = true;
                }
            } else if (key == "light_color") {
                float vals[3];
                if (parse_floats(value.c_str(), vals, 3)) {
                    pending.light_color[0]  = vals[0];
                    pending.light_color[1]  = vals[1];
                    pending.light_color[2]  = vals[2];
                    pending.has_light_color = true;
                }
            } else if (key == "light_intensity") {
                char* endptr = nullptr;
                float v = std::strtof(value.c_str(), &endptr);
                if (endptr != value.c_str()) {
                    pending.light_intensity   = v;
                    pending.has_light_intensity = true;
                }
            } else if (key == "mesh") {
                pending.mesh_path = value;
                pending.has_mesh = true;
            } else if (key == "material") {
                pending.material_path = value;
                pending.has_material = true;
            }
            // Unknown keys are silently ignored (forward-compatible).
        } else if (current_section == Section::Editor) {
            // Parse editor-only keys into scene->editor_state.
            EditorState& es = scene->editor_state;
            if (key == "camera_position") {
                float vals[3];
                if (parse_floats(value.c_str(), vals, 3)) {
                    es.cam_pos_x = vals[0];
                    es.cam_pos_y = vals[1];
                    es.cam_pos_z = vals[2];
                    es.valid = true;
                }
            } else if (key == "camera_yaw") {
                char* endptr = nullptr;
                float v = std::strtof(value.c_str(), &endptr);
                if (endptr != value.c_str()) {
                    es.cam_yaw = v;
                    es.valid = true;
                }
            } else if (key == "camera_pitch") {
                char* endptr = nullptr;
                float v = std::strtof(value.c_str(), &endptr);
                if (endptr != value.c_str()) {
                    es.cam_pitch = v;
                    es.valid = true;
                }
            } else if (key == "camera_speed") {
                char* endptr = nullptr;
                float v = std::strtof(value.c_str(), &endptr);
                if (endptr != value.c_str()) {
                    es.cam_speed = v;
                    es.valid = true;
                }
            }
            // Unknown editor keys are silently ignored (forward-compatible).
        }
        // Properties outside any known section are silently ignored.
    }

    // Flush the last pending node (if any).
    if (has_pending_node) {
        std::string err = flush_node(scene, pending);
        if (!err.empty()) {
            return SceneLoadResult::Err(err);
        }
    }

    return SceneLoadResult::Ok(true);
}

// ============================================================================
// File I/O
// ============================================================================

SceneLoadResult scene_load_file(Scene* scene, const char* path) {
    if (!scene) {
        return SceneLoadResult::Err("scene_load_file: scene is null");
    }
    if (!path) {
        return SceneLoadResult::Err("scene_load_file: path is null");
    }

    FILE* f = uz::file_open(path, "rb");
    if (!f) {
        return SceneLoadResult::Err(
            std::string("scene_load_file: cannot open file: ") + path);
    }

    // Get file size.
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size < 0) {
        std::fclose(f);
        return SceneLoadResult::Err(
            std::string("scene_load_file: failed to determine file size: ") + path);
    }

    // Read entire file into a string.
    std::string contents(static_cast<size_t>(size), '\0');
    size_t read = std::fread(&contents[0], 1, static_cast<size_t>(size), f);
    std::fclose(f);

    if (read != static_cast<size_t>(size)) {
        return SceneLoadResult::Err(
            std::string("scene_load_file: failed to read file: ") + path);
    }

    auto result = scene_load_from_string(scene, contents.c_str());
    if (result.is_ok()) {
        // Record the source path so auto-save can write back to this file.
        uz::str_copy(scene->source_path, sizeof(scene->source_path), path);
    }
    return result;
}

} // namespace uz
