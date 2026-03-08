// entity_template.cpp -- EntityTemplate parsing, serialization, and instantiation.

#include "engine/scene/entity_template.h"
#include "engine/scene/component_registry.h"
#include "engine/scene/component_data.h"

#include <nlohmann/json.hpp>
#include "common/safe_string.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace uz {

// ============================================================================
// Internal helpers
// ============================================================================

/// Parse a single TemplateNode from a JSON object.
static Result<TemplateNode> parse_node(const nlohmann::json& j) {
    TemplateNode node;

    if (j.contains("name") && j["name"].is_string()) {
        node.name = j["name"].get<std::string>();
    }

    if (j.contains("type") && j["type"].is_string()) {
        node.type_name = j["type"].get<std::string>();
    }

    // Position: [x, y, z]
    if (j.contains("position") && j["position"].is_array()) {
        const auto& arr = j["position"];
        if (arr.size() >= 3) {
            node.position[0] = arr[0].get<float>();
            node.position[1] = arr[1].get<float>();
            node.position[2] = arr[2].get<float>();
        }
    }

    // Rotation: [x, y, z, w]
    if (j.contains("rotation") && j["rotation"].is_array()) {
        const auto& arr = j["rotation"];
        if (arr.size() >= 4) {
            node.rotation[0] = arr[0].get<float>();
            node.rotation[1] = arr[1].get<float>();
            node.rotation[2] = arr[2].get<float>();
            node.rotation[3] = arr[3].get<float>();
        }
    }

    // Scale: [x, y, z]
    if (j.contains("scale") && j["scale"].is_array()) {
        const auto& arr = j["scale"];
        if (arr.size() >= 3) {
            node.scale[0] = arr[0].get<float>();
            node.scale[1] = arr[1].get<float>();
            node.scale[2] = arr[2].get<float>();
        }
    }

    // Properties: { "key": "value", ... }
    if (j.contains("properties") && j["properties"].is_object()) {
        for (auto& [key, val] : j["properties"].items()) {
            if (val.is_string()) {
                node.properties[key] = val.get<std::string>();
            } else if (val.is_number()) {
                // Convert numeric values to string for uniform handling.
                node.properties[key] = std::to_string(val.get<double>());
            } else if (val.is_boolean()) {
                node.properties[key] = val.get<bool>() ? "true" : "false";
            }
        }
    }

    // Script path
    if (j.contains("script") && j["script"].is_string()) {
        node.script = j["script"].get<std::string>();
    }

    // Template reference (nested template)
    if (j.contains("template") && j["template"].is_string()) {
        node.template_ref = j["template"].get<std::string>();
    }

    // Template overrides
    if (j.contains("overrides") && j["overrides"].is_object()) {
        for (auto& [key, val] : j["overrides"].items()) {
            if (val.is_string()) {
                node.template_overrides[key] = val.get<std::string>();
            } else if (val.is_number()) {
                node.template_overrides[key] = std::to_string(val.get<double>());
            } else if (val.is_boolean()) {
                node.template_overrides[key] = val.get<bool>() ? "true" : "false";
            }
        }
    }

    // Children (recursive)
    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& child_j : j["children"]) {
            auto child_result = parse_node(child_j);
            if (!child_result.is_ok()) {
                return Result<TemplateNode>::Err(child_result.error());
            }
            node.children.push_back(std::move(child_result.value()));
        }
    }

    return Result<TemplateNode>::Ok(std::move(node));
}

/// Serialize a TemplateNode to JSON.
static nlohmann::json serialize_node(const TemplateNode& node) {
    nlohmann::json j;

    if (!node.name.empty()) {
        j["name"] = node.name;
    }

    if (node.type_name != "Node3D") {
        j["type"] = node.type_name;
    }

    // Only write non-default transforms.
    if (node.position[0] != 0.0f || node.position[1] != 0.0f || node.position[2] != 0.0f) {
        j["position"] = {node.position[0], node.position[1], node.position[2]};
    }

    if (node.rotation[0] != 0.0f || node.rotation[1] != 0.0f ||
        node.rotation[2] != 0.0f || node.rotation[3] != 1.0f) {
        j["rotation"] = {node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]};
    }

    if (node.scale[0] != 1.0f || node.scale[1] != 1.0f || node.scale[2] != 1.0f) {
        j["scale"] = {node.scale[0], node.scale[1], node.scale[2]};
    }

    if (!node.properties.empty()) {
        j["properties"] = node.properties;
    }

    if (!node.script.empty()) {
        j["script"] = node.script;
    }

    if (!node.template_ref.empty()) {
        j["template"] = node.template_ref;
    }

    if (!node.template_overrides.empty()) {
        j["overrides"] = node.template_overrides;
    }

    if (!node.children.empty()) {
        j["children"] = nlohmann::json::array();
        for (const auto& child : node.children) {
            j["children"].push_back(serialize_node(child));
        }
    }

    return j;
}

// ============================================================================
// EntityTemplateManager — Parse / Serialize
// ============================================================================

Result<EntityTemplate> EntityTemplateManager::parse(const std::string& json_text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (...) {
        return Result<EntityTemplate>::Err("Failed to parse JSON");
    }

    if (!j.is_object()) {
        return Result<EntityTemplate>::Err("Root must be a JSON object");
    }

    EntityTemplate tmpl;

    if (j.contains("name") && j["name"].is_string()) {
        tmpl.name = j["name"].get<std::string>();
    }

    // Parse nodes array.
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& node_j : j["nodes"]) {
            auto node_result = parse_node(node_j);
            if (!node_result.is_ok()) {
                return Result<EntityTemplate>::Err(node_result.error());
            }
            tmpl.nodes.push_back(std::move(node_result.value()));
        }
    }

    // Parse overridable field list.
    if (j.contains("overridable") && j["overridable"].is_array()) {
        for (const auto& item : j["overridable"]) {
            if (item.is_string()) {
                tmpl.overridable.insert(item.get<std::string>());
            }
        }
    }

    return Result<EntityTemplate>::Ok(std::move(tmpl));
}

std::string EntityTemplateManager::serialize(const EntityTemplate& tmpl) {
    nlohmann::json j;

    if (!tmpl.name.empty()) {
        j["name"] = tmpl.name;
    }

    j["nodes"] = nlohmann::json::array();
    for (const auto& node : tmpl.nodes) {
        j["nodes"].push_back(serialize_node(node));
    }

    if (!tmpl.overridable.empty()) {
        j["overridable"] = nlohmann::json::array();
        for (const auto& field : tmpl.overridable) {
            j["overridable"].push_back(field);
        }
    }

    return j.dump(2);
}

// ============================================================================
// EntityTemplateManager — Load / Cache
// ============================================================================

Result<const EntityTemplate*> EntityTemplateManager::load(const std::string& path) {
    // Check cache first.
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return Result<const EntityTemplate*>::Ok(&it->second);
    }

    // Read file.
    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<const EntityTemplate*>::Err(
            std::string("Cannot open template file: ") + path);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();

    auto parse_result = parse(ss.str());
    if (!parse_result.is_ok()) {
        return Result<const EntityTemplate*>::Err(parse_result.error());
    }

    auto [inserted_it, _] = cache_.emplace(path, std::move(parse_result.value()));
    return Result<const EntityTemplate*>::Ok(&inserted_it->second);
}

const EntityTemplate* EntityTemplateManager::get(const std::string& path) const {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return &it->second;
    }
    return nullptr;
}

Result<const EntityTemplate*> EntityTemplateManager::reload(const std::string& path) {
    // Remove from cache first.
    cache_.erase(path);
    return load(path);
}

void EntityTemplateManager::unload(const std::string& path) {
    cache_.erase(path);
}

void EntityTemplateManager::clear() {
    cache_.clear();
}

size_t EntityTemplateManager::count() const {
    return cache_.size();
}

// ============================================================================
// EntityTemplateManager — Instantiation
// ============================================================================

Result<bool> EntityTemplateManager::spawn_node(
    Scene* scene,
    const TemplateNode& node,
    NodeId parent_id,
    SpawnResult& result,
    const SpawnOverrides& overrides,
    const std::string& node_prefix,
    uint32_t nesting_depth)
{
    // Build the qualified name for override matching.
    std::string qualified_name = node_prefix.empty()
        ? node.name
        : node_prefix + "." + node.name;

    // Handle nested template reference.
    if (!node.template_ref.empty()) {
        if (nesting_depth >= kMaxNestingDepth) {
            result.warnings.push_back(
                "Max nesting depth exceeded for template: " + node.template_ref);
            return Result<bool>::Ok(true);
        }

        auto load_result = load(node.template_ref);
        if (!load_result.is_ok()) {
            result.warnings.push_back(
                "Failed to load nested template: " + node.template_ref +
                " (" + load_result.error() + ")");
            return Result<bool>::Ok(true);
        }

        const EntityTemplate* nested_tmpl = load_result.value();

        // Build overrides for the nested template from node.template_overrides.
        SpawnOverrides nested_overrides;
        nested_overrides.fields = node.template_overrides;

        // Apply position from the referencing node.
        if (node.position[0] != 0.0f || node.position[1] != 0.0f || node.position[2] != 0.0f) {
            nested_overrides.has_position = true;
            nested_overrides.position[0] = node.position[0];
            nested_overrides.position[1] = node.position[1];
            nested_overrides.position[2] = node.position[2];
        }

        // Instantiate nested template nodes.
        for (const auto& nested_node : nested_tmpl->nodes) {
            auto r = spawn_node(scene, nested_node, parent_id, result,
                                nested_overrides, qualified_name,
                                nesting_depth + 1);
            if (!r.is_ok()) return r;
        }

        return Result<bool>::Ok(true);
    }

    // Create the scene node.
    const char* type_name = node.type_name.c_str();
    const char* name_str  = node.name.empty() ? "Unnamed" : node.name.c_str();

    NodeId new_id = scene_create_node(scene, name_str, type_name, parent_id);
    if (new_id == kNodeIdInvalid) {
        return Result<bool>::Err("Scene is full, cannot create node: " + node.name);
    }

    result.node_ids.push_back(new_id);

    // Set transform.
    SceneNode* scene_node = scene_get_node(scene, new_id);
    if (scene_node) {
        float pos[3] = {node.position[0], node.position[1], node.position[2]};

        // Apply position override if this is a root node and overrides have position.
        if (parent_id == kNodeIdInvalid || overrides.has_position) {
            // Check for qualified override first.
            auto pos_it = overrides.fields.find(qualified_name + ".position");
            if (pos_it != overrides.fields.end()) {
                // Parse "x y z" format.
                float vals[3];
                if (std::sscanf(pos_it->second.c_str(), "%f %f %f",
                                &vals[0], &vals[1], &vals[2]) == 3) {
                    pos[0] = vals[0]; pos[1] = vals[1]; pos[2] = vals[2];
                }
            } else if (overrides.has_position && result.node_ids.size() == 1) {
                // Root node: apply spawn position.
                pos[0] = overrides.position[0];
                pos[1] = overrides.position[1];
                pos[2] = overrides.position[2];
            }
        }

        scene_node->local_transform.position = {pos[0], pos[1], pos[2], 0.0f};
        scene_node->local_transform.rotation = {
            node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]
        };
        scene_node->local_transform.scale = {node.scale[0], node.scale[1], node.scale[2], 0.0f};
    }

    // Attach components via ComponentRegistry.
    const NodeTypeDef* def = ComponentRegistry::find(type_name);
    if (def) {
        for (uint32_t ci = 0; ci < def->comp_count; ++ci) {
            const NodeComponentDef& cd = def->comps[ci];
            void* data = scene->components.add(new_id, cd.type_id);
            if (!data) continue;
            cd.construct(data);

            // Apply template properties.
            for (const auto& [key, val] : node.properties) {
                // Check if there's a caller override for this property.
                std::string override_key = qualified_name + "." + key;
                const std::string* effective_val = &val;

                auto ov_it = overrides.fields.find(override_key);
                if (ov_it != overrides.fields.end()) {
                    effective_val = &ov_it->second;
                }

                // Apply via property parse functions.
                for (uint32_t pi = 0; pi < cd.prop_count; ++pi) {
                    if (key == cd.props[pi].key) {
                        cd.props[pi].parse(data, effective_val->c_str());
                        break;
                    }
                }
            }

            // Apply overrides for properties not in the template.
            for (const auto& [ov_key, ov_val] : overrides.fields) {
                // Check if this override targets a property on this node.
                if (ov_key.starts_with(qualified_name + ".")) {
                    std::string prop_key = ov_key.substr(qualified_name.size() + 1);
                    // Skip position (already handled above).
                    if (prop_key == "position") continue;
                    // Skip if already in template properties (handled above).
                    if (node.properties.count(prop_key)) continue;

                    for (uint32_t pi = 0; pi < cd.prop_count; ++pi) {
                        if (prop_key == cd.props[pi].key) {
                            cd.props[pi].parse(data, ov_val.c_str());
                            break;
                        }
                    }
                }
            }
        }
    }

    // Attach script if specified.
    if (!node.script.empty()) {
        auto* sd = component_add<ScriptData>(&scene->components, new_id);
        if (sd) {
            str_copy(sd->script_path, sizeof(sd->script_path), node.script.c_str());
        }
    }

    // Recursively create children.
    for (const auto& child : node.children) {
        auto r = spawn_node(scene, child, new_id, result, overrides,
                            qualified_name, nesting_depth);
        if (!r.is_ok()) return r;
    }

    return Result<bool>::Ok(true);
}

Result<SpawnResult> EntityTemplateManager::spawn(
    Scene* scene,
    const EntityTemplate& tmpl,
    NodeId parent_id,
    const SpawnOverrides& overrides)
{
    if (!scene) {
        return Result<SpawnResult>::Err("Scene is null");
    }

    SpawnResult result;

    // Check for non-overridable field overrides and generate warnings.
    if (!tmpl.overridable.empty()) {
        for (const auto& [key, _] : overrides.fields) {
            if (!tmpl.overridable.count(key)) {
                result.warnings.push_back(
                    "Override for non-overridable field ignored: " + key);
            }
        }
    }

    for (const auto& node : tmpl.nodes) {
        auto r = spawn_node(scene, node, parent_id, result, overrides, "", 0);
        if (!r.is_ok()) {
            // Rollback: destroy any nodes we created.
            for (auto it = result.node_ids.rbegin(); it != result.node_ids.rend(); ++it) {
                // Only destroy root-level nodes (children are destroyed recursively).
                SceneNode* sn = scene_get_node(scene, *it);
                if (sn && sn->parent_id == parent_id) {
                    scene_destroy_node(scene, *it);
                }
            }
            return Result<SpawnResult>::Err(r.error());
        }
    }

    return Result<SpawnResult>::Ok(std::move(result));
}

Result<SpawnResult> EntityTemplateManager::spawn_by_path(
    Scene* scene,
    const std::string& path,
    NodeId parent_id,
    const SpawnOverrides& overrides)
{
    auto load_result = load(path);
    if (!load_result.is_ok()) {
        return Result<SpawnResult>::Err(load_result.error());
    }

    return spawn(scene, *load_result.value(), parent_id, overrides);
}

void EntityTemplateManager::despawn(Scene* scene, const SpawnResult& result) {
    if (!scene) return;

    // Destroy in reverse order. Only destroy nodes that are still alive.
    // Children will be destroyed recursively by scene_destroy_node.
    for (auto it = result.node_ids.rbegin(); it != result.node_ids.rend(); ++it) {
        SceneNode* node = scene_get_node(scene, *it);
        if (node) {
            scene_destroy_node(scene, *it);
        }
    }
}

} // namespace uz
