// scene_loader.cpp — .uzscene INI-like parser implementation.
//
// Data-driven via ComponentRegistry: adding a new node type requires zero
// changes here. The parser accumulates raw key=value pairs per [node] section
// and dispatches them to the registered component property setters.

#include "engine/scene/scene_loader.h"
#include "engine/scene/component_registry.h"
#include "engine/scene/component_store.h"

#include <cstdio>
#include <cstdlib>
#include "common/safe_string.h"
#include <string>
#include <sstream>
#include <vector>
#include <utility>

namespace uz {

// ============================================================================
// Internal helpers
// ============================================================================

static std::string trim(const std::string& s) {
    const char* begin = s.c_str();
    const char* end   = begin + s.size();
    while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r')) ++begin;
    while (end > begin && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\r')) --end;
    return std::string(begin, static_cast<size_t>(end - begin));
}

static bool parse_float(const char*& ptr, float& out) {
    while (*ptr == ' ' || *ptr == '\t') ++ptr;
    if (*ptr == '\0') return false;
    char* end = nullptr;
    out = std::strtof(ptr, &end);
    if (end == ptr) return false;
    ptr = end;
    return true;
}

static bool parse_floats(const char* s, float* out, int count) {
    const char* p = s;
    for (int i = 0; i < count; ++i)
        if (!parse_float(p, out[i])) return false;
    while (*p == ' ' || *p == '\t') ++p;
    return *p == '\0';
}

static bool parse_bool(const std::string& v) { return v == "true" || v == "1"; }

// ============================================================================
// PendingNode — accumulates one [node] section before committing to the scene.
//
// Common structural fields (transform, flags) are parsed directly.
// All remaining key=value pairs go into raw_props and are dispatched via
// ComponentRegistry property setters in flush_node().
// ============================================================================

struct PendingNode {
    std::string name;
    std::string type_str;
    std::string parent_name;

    float position[3] = {0,0,0};
    float rotation[4] = {0,0,0,1};
    float scale_val[3] = {1,1,1};
    bool  has_position = false;
    bool  has_rotation = false;
    bool  has_scale    = false;

    bool  active  = true;
    bool  visible = true;
    bool  has_active  = false;
    bool  has_visible = false;

    std::string script_path;  // Cross-cutting: any node can have a Lua script

    // All component-specific properties accumulated as raw strings.
    // Dispatched to ComponentRegistry::find()->comps[i].props[j].parse() in flush_node().
    std::vector<std::pair<std::string, std::string>> raw_props;

    void reset() {
        name.clear(); type_str.clear(); parent_name.clear();
        script_path.clear();
        position[0] = 0; position[1] = 0; position[2] = 0;
        rotation[0] = 0; rotation[1] = 0; rotation[2] = 0; rotation[3] = 1;
        scale_val[0] = 1; scale_val[1] = 1; scale_val[2] = 1;
        has_position = has_rotation = has_scale = false;
        active = true; visible = true;
        has_active = has_visible = false;
        raw_props.clear();
    }
};

// ============================================================================
// flush_node — commit a PendingNode to the Scene
// ============================================================================

static std::string flush_node(Scene* scene, const PendingNode& p) {
    const char* node_name = p.name.empty() ? "Unnamed" : p.name.c_str();
    const char* type_name = p.type_str.empty() ? "Node3D" : p.type_str.c_str();

    // Resolve parent by name.
    NodeId parent_id = kNodeIdInvalid;
    if (!p.parent_name.empty()) {
        parent_id = scene_find_by_name(scene, p.parent_name.c_str());
        if (parent_id == kNodeIdInvalid)
            return std::string("Parent node not found: ") + p.parent_name;
    }

    NodeId id = scene_create_node(scene, node_name, type_name, parent_id);
    if (id == kNodeIdInvalid)
        return std::string("Failed to create node: ") + node_name;

    SceneNode* node = scene_get_node(scene, id);
    if (!node) return std::string("Internal error: created node is inaccessible");

    // Apply transform.
    if (p.has_position)
        node->local_transform.position = {p.position[0], p.position[1], p.position[2], 0.0f};
    if (p.has_rotation)
        node->local_transform.rotation = {p.rotation[0], p.rotation[1], p.rotation[2], p.rotation[3]};
    if (p.has_scale)
        node->local_transform.scale = {p.scale_val[0], p.scale_val[1], p.scale_val[2], 0.0f};

    // Apply flags.
    uint16_t flags = 0;
    if (p.has_active  ? p.active  : true)  flags |= NODE_FLAG_ACTIVE;
    if (p.has_visible ? p.visible : true)  flags |= NODE_FLAG_VISIBLE;
    node->flags = flags;

    // Registry dispatch: attach components and apply raw properties.
    const NodeTypeDef* def = ComponentRegistry::find(type_name);
    if (def) {
        for (uint32_t ci = 0; ci < def->comp_count; ++ci) {
            const NodeComponentDef& cd = def->comps[ci];
            void* data = scene->components.add(id, cd.type_id);
            if (!data) continue;
            cd.construct(data);

            // Apply each raw property that matches this component's known keys.
            for (auto& [k, v] : p.raw_props) {
                for (uint32_t pi = 0; pi < cd.prop_count; ++pi) {
                    if (k == cd.props[pi].key) {
                        cd.props[pi].parse(data, v.c_str());
                        break;
                    }
                }
            }
        }
    }
    // Unknown type: node created with no components — just a named transform node.

    // Attach ScriptData if the node has a script property.
    if (!p.script_path.empty()) {
        auto* sd = component_add<ScriptData>(&scene->components, id);
        if (sd) {
            str_copy(sd->script_path, sizeof(sd->script_path), p.script_path.c_str());
        }
    }

    return {};
}

// ============================================================================
// Core parser
// ============================================================================

SceneLoadResult scene_load_from_string(Scene* scene, const char* text) {
    if (!scene) return SceneLoadResult::Err("scene_load_from_string: scene is null");
    if (!text)  return SceneLoadResult::Err("scene_load_from_string: text is null");

    scene_clear(scene);

    std::istringstream stream(text);
    std::string line;

    enum class Section { None, UzScene, Node, Editor };
    Section current_section = Section::None;

    PendingNode pending;
    bool has_pending_node = false;
    int  line_number = 0;

    while (std::getline(stream, line)) {
        ++line_number;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;

        // Section header
        if (trimmed[0] == '[') {
            if (has_pending_node) {
                std::string err = flush_node(scene, pending);
                if (!err.empty())
                    return SceneLoadResult::Err("Line " + std::to_string(line_number) + ": " + err);
                has_pending_node = false;
            }

            // Find closing bracket — ignore inline comments after ]
            auto close = trimmed.find(']');
            std::string section_name = (close != std::string::npos)
                ? trim(trimmed.substr(1, close - 1)) : trim(trimmed.substr(1));

            if (section_name == "uzscene") {
                current_section = Section::UzScene;
            } else if (section_name == "node") {
                current_section = Section::Node;
                pending.reset();
                has_pending_node = true;
            } else if (section_name == "editor") {
                current_section = Section::Editor;
            } else {
                current_section = Section::None;  // Unknown section — forward-compatible skip
            }
            continue;
        }

        // key = value
        auto eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key   = trim(trimmed.substr(0, eq_pos));
        std::string value = trim(trimmed.substr(eq_pos + 1));

        if (current_section == Section::UzScene) {
            if (key == "name") {
                uz::str_copy(scene->name, sizeof(scene->name), value.c_str());
            } else if (key == "version") {
                char* ep = nullptr;
                long v = std::strtol(value.c_str(), &ep, 10);
                if (ep != value.c_str()) scene->version = static_cast<uint32_t>(v);
            }

        } else if (current_section == Section::Node) {
            // Common structural keys handled directly.
            if (key == "name") {
                pending.name = value;
            } else if (key == "type") {
                pending.type_str = value;
            } else if (key == "parent") {
                pending.parent_name = value;
            } else if (key == "position") {
                float vals[3];
                if (!parse_floats(value.c_str(), vals, 3))
                    return SceneLoadResult::Err("Line " + std::to_string(line_number) +
                                                ": position requires exactly 3 floats");
                pending.position[0] = vals[0]; pending.position[1] = vals[1];
                pending.position[2] = vals[2]; pending.has_position = true;
            } else if (key == "rotation") {
                float vals[4];
                if (!parse_floats(value.c_str(), vals, 4))
                    return SceneLoadResult::Err("Line " + std::to_string(line_number) +
                                                ": rotation requires exactly 4 floats");
                pending.rotation[0] = vals[0]; pending.rotation[1] = vals[1];
                pending.rotation[2] = vals[2]; pending.rotation[3] = vals[3];
                pending.has_rotation = true;
            } else if (key == "scale") {
                float vals[3];
                if (!parse_floats(value.c_str(), vals, 3))
                    return SceneLoadResult::Err("Line " + std::to_string(line_number) +
                                                ": scale requires exactly 3 floats");
                pending.scale_val[0] = vals[0]; pending.scale_val[1] = vals[1];
                pending.scale_val[2] = vals[2]; pending.has_scale = true;
            } else if (key == "active") {
                pending.active = parse_bool(value); pending.has_active = true;
            } else if (key == "visible") {
                pending.visible = parse_bool(value); pending.has_visible = true;
            } else if (key == "script") {
                pending.script_path = value;
            } else {
                // All other keys go to raw_props for registry dispatch.
                pending.raw_props.emplace_back(key, value);
            }

        } else if (current_section == Section::Editor) {
            EditorState& es = scene->editor_state;
            if (key == "camera_position") {
                float vals[3];
                if (parse_floats(value.c_str(), vals, 3)) {
                    es.cam_pos_x = vals[0]; es.cam_pos_y = vals[1];
                    es.cam_pos_z = vals[2]; es.valid = true;
                }
            } else if (key == "camera_yaw") {
                char* ep = nullptr; float v = std::strtof(value.c_str(), &ep);
                if (ep != value.c_str()) { es.cam_yaw = v; es.valid = true; }
            } else if (key == "camera_pitch") {
                char* ep = nullptr; float v = std::strtof(value.c_str(), &ep);
                if (ep != value.c_str()) { es.cam_pitch = v; es.valid = true; }
            } else if (key == "camera_speed") {
                char* ep = nullptr; float v = std::strtof(value.c_str(), &ep);
                if (ep != value.c_str()) { es.cam_speed = v; es.valid = true; }
            }
        }
    }

    // Flush the last pending node.
    if (has_pending_node) {
        std::string err = flush_node(scene, pending);
        if (!err.empty()) return SceneLoadResult::Err(err);
    }

    return SceneLoadResult::Ok(true);
}

// ============================================================================
// File I/O
// ============================================================================

SceneLoadResult scene_load_file(Scene* scene, const char* path) {
    if (!scene) return SceneLoadResult::Err("scene_load_file: scene is null");
    if (!path)  return SceneLoadResult::Err("scene_load_file: path is null");

    FILE* f = uz::file_open(path, "rb");
    if (!f)
        return SceneLoadResult::Err(std::string("scene_load_file: cannot open file: ") + path);

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size < 0) {
        std::fclose(f);
        return SceneLoadResult::Err(std::string("scene_load_file: cannot stat file: ") + path);
    }

    std::string contents(static_cast<size_t>(size), '\0');
    size_t read = std::fread(&contents[0], 1, static_cast<size_t>(size), f);
    std::fclose(f);

    if (read != static_cast<size_t>(size))
        return SceneLoadResult::Err(std::string("scene_load_file: read error: ") + path);

    auto result = scene_load_from_string(scene, contents.c_str());
    if (result.is_ok())
        uz::str_copy(scene->source_path, sizeof(scene->source_path), path);
    return result;
}

} // namespace uz
