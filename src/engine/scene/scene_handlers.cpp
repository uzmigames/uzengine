// scene_handlers.cpp -- IPC handler implementations for scene.* commands.
//
// Each handler receives a JSON "args" object and returns a JSON result.
// All handlers capture the Scene* by value (pointer copy). The scene must
// remain valid for the lifetime of the registry.

#include "engine/scene/scene_handlers.h"
#include "engine/scene/scene_loader.h"
#include "engine/scene/scene_saver.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ============================================================================
// Internal helpers
// ============================================================================

/// Build a standard error response.
static nlohmann::json make_error(const char* msg) {
    return {{"ok", false}, {"error", msg}};
}

/// Build a standard error response from std::string.
static nlohmann::json make_error(const std::string& msg) {
    return {{"ok", false}, {"error", msg}};
}

/// Build a standard success response.
static nlohmann::json make_ok() {
    return {{"ok", true}};
}

// ============================================================================
// scene_register_handlers
// ============================================================================

void scene_register_handlers(IpcRegistry& registry, Scene* scene) {

    // ----------------------------------------------------------------
    // scene.getTree
    // ----------------------------------------------------------------
    registry.add("scene.getTree", [scene](const nlohmann::json& /*args*/) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        auto result = nlohmann::json::array();

        for (uint32_t i = 1; i < scene->next_id; ++i) {
            const SceneNode& node = scene->nodes[i];
            if (!node.alive) {
                continue;
            }

            result.push_back({
                {"id",        node.id},
                {"name",      node.name},
                {"type",      node_type_to_string(node.type)},
                {"parent_id", node.parent_id},
                {"active",    static_cast<bool>(node.flags & NODE_FLAG_ACTIVE)},
                {"visible",   static_cast<bool>(node.flags & NODE_FLAG_VISIBLE)},
                {"position",  {node.local_transform.position.x,
                               node.local_transform.position.y,
                               node.local_transform.position.z}},
                {"rotation",  {node.local_transform.rotation.x,
                               node.local_transform.rotation.y,
                               node.local_transform.rotation.z,
                               node.local_transform.rotation.w}},
                {"scale",     {node.local_transform.scale.x,
                               node.local_transform.scale.y,
                               node.local_transform.scale.z}}
            });
        }

        return result;
    });

    // ----------------------------------------------------------------
    // scene.addNode
    // ----------------------------------------------------------------
    registry.add("scene.addNode", [scene](const nlohmann::json& args) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        if (!args.contains("name") || !args["name"].is_string()) {
            return make_error("missing or invalid 'name'");
        }

        const std::string name = args["name"].get<std::string>();

        // Type defaults to Node3D if not specified.
        NodeType type = NodeType::Node3D;
        if (args.contains("type") && args["type"].is_string()) {
            type = node_type_from_string(args["type"].get<std::string>().c_str());
        }

        // Parent defaults to kNodeIdInvalid (root-level).
        NodeId parent_id = kNodeIdInvalid;
        if (args.contains("parent_id") && args["parent_id"].is_number_integer()) {
            parent_id = args["parent_id"].get<uint32_t>();
        }

        const NodeId new_id = scene_create_node(scene, name.c_str(), type, parent_id);
        if (new_id == kNodeIdInvalid) {
            return make_error("scene full");
        }

        return {{"ok", true}, {"id", new_id}};
    });

    // ----------------------------------------------------------------
    // scene.removeNode
    // ----------------------------------------------------------------
    registry.add("scene.removeNode", [scene](const nlohmann::json& args) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        if (!args.contains("id") || !args["id"].is_number_integer()) {
            return make_error("missing or invalid 'id'");
        }

        const NodeId id = args["id"].get<uint32_t>();

        // Verify the node exists before destroying.
        const SceneNode* node = scene_get_node(scene, id);
        if (!node) {
            return make_error("invalid id");
        }

        scene_destroy_node(scene, id);
        return make_ok();
    });

    // ----------------------------------------------------------------
    // scene.setProperty
    // ----------------------------------------------------------------
    registry.add("scene.setProperty", [scene](const nlohmann::json& args) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        if (!args.contains("id") || !args["id"].is_number_integer()) {
            return make_error("missing or invalid 'id'");
        }
        if (!args.contains("key") || !args["key"].is_string()) {
            return make_error("missing or invalid 'key'");
        }
        if (!args.contains("value")) {
            return make_error("missing 'value'");
        }

        const NodeId id = args["id"].get<uint32_t>();
        const std::string key = args["key"].get<std::string>();
        const auto& value = args["value"];

        SceneNode* node = scene_get_node(scene, id);
        if (!node) {
            return make_error("node not found");
        }

        if (key == "name") {
            if (!value.is_string()) {
                return make_error("'name' value must be a string");
            }
            node->set_name(value.get<std::string>().c_str());
            return make_ok();
        }

        if (key == "position") {
            if (!value.is_array() || value.size() != 3) {
                return make_error("'position' value must be a [x, y, z] array");
            }
            auto v = value.get<std::vector<float>>();
            node->local_transform.position = {v[0], v[1], v[2], 0.0f};
            return make_ok();
        }

        if (key == "rotation") {
            if (!value.is_array() || value.size() != 4) {
                return make_error("'rotation' value must be a [x, y, z, w] array");
            }
            auto v = value.get<std::vector<float>>();
            node->local_transform.rotation = {v[0], v[1], v[2], v[3]};
            return make_ok();
        }

        if (key == "scale") {
            if (!value.is_array() || value.size() != 3) {
                return make_error("'scale' value must be a [x, y, z] array");
            }
            auto v = value.get<std::vector<float>>();
            node->local_transform.scale = {v[0], v[1], v[2], 0.0f};
            return make_ok();
        }

        if (key == "active") {
            if (!value.is_boolean()) {
                return make_error("'active' value must be a boolean");
            }
            if (value.get<bool>()) {
                node->flags |= NODE_FLAG_ACTIVE;
            } else {
                node->flags &= static_cast<uint16_t>(~NODE_FLAG_ACTIVE);
            }
            return make_ok();
        }

        if (key == "visible") {
            if (!value.is_boolean()) {
                return make_error("'visible' value must be a boolean");
            }
            if (value.get<bool>()) {
                node->flags |= NODE_FLAG_VISIBLE;
            } else {
                node->flags &= static_cast<uint16_t>(~NODE_FLAG_VISIBLE);
            }
            return make_ok();
        }

        return make_error("unknown key: " + key);
    });

    // ----------------------------------------------------------------
    // scene.getProperty
    // ----------------------------------------------------------------
    registry.add("scene.getProperty", [scene](const nlohmann::json& args) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        if (!args.contains("id") || !args["id"].is_number_integer()) {
            return make_error("missing or invalid 'id'");
        }
        if (!args.contains("key") || !args["key"].is_string()) {
            return make_error("missing or invalid 'key'");
        }

        const NodeId id = args["id"].get<uint32_t>();
        const std::string key = args["key"].get<std::string>();

        const SceneNode* node = scene_get_node(scene, id);
        if (!node) {
            return make_error("node not found");
        }

        if (key == "name") {
            return {{"ok", true}, {"value", node->name}};
        }

        if (key == "position") {
            return {{"ok", true}, {"value", {node->local_transform.position.x,
                                              node->local_transform.position.y,
                                              node->local_transform.position.z}}};
        }

        if (key == "rotation") {
            return {{"ok", true}, {"value", {node->local_transform.rotation.x,
                                              node->local_transform.rotation.y,
                                              node->local_transform.rotation.z,
                                              node->local_transform.rotation.w}}};
        }

        if (key == "scale") {
            return {{"ok", true}, {"value", {node->local_transform.scale.x,
                                              node->local_transform.scale.y,
                                              node->local_transform.scale.z}}};
        }

        if (key == "active") {
            return {{"ok", true}, {"value", static_cast<bool>(node->flags & NODE_FLAG_ACTIVE)}};
        }

        if (key == "visible") {
            return {{"ok", true}, {"value", static_cast<bool>(node->flags & NODE_FLAG_VISIBLE)}};
        }

        return make_error("unknown key: " + key);
    });

    // ----------------------------------------------------------------
    // scene.load
    // ----------------------------------------------------------------
    registry.add("scene.load", [scene](const nlohmann::json& args) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }

        const std::string path = args["path"].get<std::string>();
        auto result = scene_load_file(scene, path.c_str());

        if (result.is_err()) {
            return make_error(result.error());
        }

        return make_ok();
    });

    // ----------------------------------------------------------------
    // scene.save
    // ----------------------------------------------------------------
    registry.add("scene.save", [scene](const nlohmann::json& args) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }

        const std::string path = args["path"].get<std::string>();
        auto result = scene_save_file(scene, path.c_str());

        if (result.is_err()) {
            return make_error(result.error());
        }

        return make_ok();
    });

    // ----------------------------------------------------------------
    // scene.selectNode
    // ----------------------------------------------------------------
    registry.add("scene.selectNode", [scene](const nlohmann::json& args) -> nlohmann::json {
        if (!scene) {
            return make_error("no scene");
        }

        if (!args.contains("id") || !args["id"].is_number_integer()) {
            return make_error("missing or invalid 'id'");
        }

        const NodeId id = args["id"].get<uint32_t>();

        // id=0 means deselect — always OK.
        if (id == kNodeIdInvalid) {
            return {{"ok", true}, {"id", 0}};
        }

        const SceneNode* node = scene_get_node(scene, id);
        if (!node) {
            return make_error("not found");
        }

        return {{"ok", true}, {"id", id}, {"name", node->name}};
    });
}

} // namespace uz
