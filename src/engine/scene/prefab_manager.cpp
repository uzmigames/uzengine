// prefab_manager.cpp -- Prefab system implementation.
//
// Implements PrefabManager and PrefabInstanceData operations.
// See prefab_manager.h for interface documentation.

#include "engine/scene/prefab_manager.h"
#include "engine/scene/component_store.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_registry.h"

#include "common/safe_string.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace uz {

// ============================================================================
// PrefabInstanceData
// ============================================================================

bool PrefabInstanceData::set_override(const char* key, const char* value) {
    if (!key || !value) return false;

    // Check if override already exists — update it.
    uint32_t idx = find_override(key);
    if (idx < kMaxOverrides) {
        str_copy(override_values[idx], sizeof(override_values[idx]), value);
        return true;
    }

    // Add new override.
    if (override_count >= kMaxOverrides) return false;

    str_copy(override_keys[override_count], sizeof(override_keys[override_count]), key);
    str_copy(override_values[override_count], sizeof(override_values[override_count]), value);
    ++override_count;
    return true;
}

bool PrefabInstanceData::remove_override(const char* key) {
    if (!key) return false;

    uint32_t idx = find_override(key);
    if (idx >= kMaxOverrides) return false;

    // Shift remaining entries down.
    for (uint32_t i = idx; i + 1 < override_count; ++i) {
        std::memcpy(override_keys[i], override_keys[i + 1], sizeof(override_keys[i]));
        std::memcpy(override_values[i], override_values[i + 1], sizeof(override_values[i]));
    }
    --override_count;

    // Zero out the now-unused last slot.
    override_keys[override_count][0] = '\0';
    override_values[override_count][0] = '\0';

    return true;
}

void PrefabInstanceData::clear_overrides() {
    for (uint32_t i = 0; i < override_count; ++i) {
        override_keys[i][0] = '\0';
        override_values[i][0] = '\0';
    }
    override_count = 0;
}

uint32_t PrefabInstanceData::find_override(const char* key) const {
    if (!key) return kMaxOverrides;

    for (uint32_t i = 0; i < override_count; ++i) {
        if (std::strcmp(override_keys[i], key) == 0) {
            return i;
        }
    }
    return kMaxOverrides;
}

// ============================================================================
// PrefabManager — lifecycle
// ============================================================================

void PrefabManager::set_scene(Scene* scene) {
    scene_ = scene;
    // Ensure PrefabInstanceData component type is registered.
    if (scene_) {
        component_store_register<PrefabInstanceData>(&scene_->components);
    }
}

// ============================================================================
// PrefabManager — prefab edit mode
// ============================================================================

Result<NodeId> PrefabManager::open_prefab(const char* path) {
    if (!scene_) return Result<NodeId>::Err("no scene");
    if (!path || path[0] == '\0') return Result<NodeId>::Err("empty prefab path");
    if (mode_ == EditorMode::PREFAB_EDIT)
        return Result<NodeId>::Err("already in prefab edit mode");

    // 1. Cache the current main scene state as serialized text.
    cached_main_scene_ = scene_save_to_string(scene_);

    // 2. Load the prefab file into the scene (clears existing state).
    auto load_result = scene_load_file(scene_, path);
    if (load_result.is_err()) {
        // Restore main scene on failure.
        scene_load_from_string(scene_, cached_main_scene_.c_str());
        cached_main_scene_.clear();
        return Result<NodeId>::Err("failed to load prefab: " + load_result.error());
    }

    // 3. Set mode.
    mode_ = EditorMode::PREFAB_EDIT;
    active_prefab_path_ = path;

    // 4. Find the root node ID (first alive node).
    for (uint32_t i = 1; i < scene_->next_id; ++i) {
        if (scene_->nodes[i].alive) {
            return Result<NodeId>::Ok(i);
        }
    }

    // Prefab was empty — still valid, but return kNodeIdInvalid.
    return Result<NodeId>::Ok(kNodeIdInvalid);
}

Result<bool> PrefabManager::save_prefab(const char* path) {
    if (!scene_) return Result<bool>::Err("no scene");
    if (!path || path[0] == '\0') return Result<bool>::Err("empty path");
    if (mode_ != EditorMode::PREFAB_EDIT)
        return Result<bool>::Err("not in prefab edit mode");

    auto save_result = scene_save_file(scene_, path);
    if (save_result.is_err()) {
        return Result<bool>::Err("failed to save prefab: " + save_result.error());
    }

    return Result<bool>::Ok(true);
}

Result<bool> PrefabManager::close_prefab() {
    if (!scene_) return Result<bool>::Err("no scene");
    if (mode_ != EditorMode::PREFAB_EDIT)
        return Result<bool>::Err("not in prefab edit mode");

    // Restore the cached main scene.
    if (!cached_main_scene_.empty()) {
        auto load_result = scene_load_from_string(scene_, cached_main_scene_.c_str());
        if (load_result.is_err()) {
            return Result<bool>::Err("failed to restore main scene: " + load_result.error());
        }
    } else {
        scene_clear(scene_);
    }

    mode_ = EditorMode::SCENE_EDIT;
    active_prefab_path_.clear();
    cached_main_scene_.clear();

    return Result<bool>::Ok(true);
}

// ============================================================================
// PrefabManager — instantiation
// ============================================================================

Result<NodeId> PrefabManager::instantiate_prefab(
    const char* prefab_path,
    const char* instance_name,
    NodeId parent_id)
{
    if (!scene_) return Result<NodeId>::Err("no scene");
    if (!prefab_path || prefab_path[0] == '\0')
        return Result<NodeId>::Err("empty prefab path");

    // Load the prefab into a temporary scene to read its structure.
    auto temp_scene = std::make_unique<Scene>();
    scene_init(temp_scene.get(), "TempPrefab");
    // Ensure PrefabInstanceData is registered on temp scene too.
    component_store_register<PrefabInstanceData>(&temp_scene->components);

    auto load_result = scene_load_file(temp_scene.get(), prefab_path);
    if (load_result.is_err()) {
        return Result<NodeId>::Err("failed to load prefab file: " + load_result.error());
    }

    // Find the first root-level node in the prefab.
    NodeId prefab_root = kNodeIdInvalid;
    for (uint32_t i = 1; i < temp_scene->next_id; ++i) {
        if (temp_scene->nodes[i].alive && temp_scene->nodes[i].parent_id == kNodeIdInvalid) {
            prefab_root = i;
            break;
        }
    }

    if (prefab_root == kNodeIdInvalid) {
        return Result<NodeId>::Err("prefab has no root node");
    }

    const SceneNode* prefab_root_node = scene_get_node(temp_scene.get(), prefab_root);

    // Create the instance root node in the target scene.
    const char* name = (instance_name && instance_name[0] != '\0')
        ? instance_name
        : prefab_root_node->name;

    NodeId instance_id = scene_create_node(
        scene_, name, prefab_root_node->type_name, parent_id);

    if (instance_id == kNodeIdInvalid) {
        return Result<NodeId>::Err("scene full — cannot create instance node");
    }

    // Copy transform from prefab root.
    SceneNode* instance_node = scene_get_node(scene_, instance_id);
    if (instance_node) {
        instance_node->local_transform = prefab_root_node->local_transform;
        instance_node->flags = prefab_root_node->flags;
    }

    // Attach PrefabInstanceData component.
    auto* pid = component_add<PrefabInstanceData>(&scene_->components, instance_id);
    if (pid) {
        str_copy(pid->prefab_source, sizeof(pid->prefab_source), prefab_path);
    }

    // Copy component data from prefab root to instance (type-specific properties).
    {
        const NodeTypeDef* def = ComponentRegistry::find(prefab_root_node->type_name);
        if (def) {
            for (uint32_t ci = 0; ci < def->comp_count; ++ci) {
                const NodeComponentDef& cd = def->comps[ci];
                const void* src_data = temp_scene->components.get(prefab_root, cd.type_id);
                if (!src_data) continue;

                void* dst_data = scene_->components.add(instance_id, cd.type_id);
                if (dst_data) {
                    std::memcpy(dst_data, src_data, cd.stride);
                }
            }
        }
    }

    // Recursively instantiate children.
    // We use a simple iterative approach with a stack.
    struct CopyEntry {
        NodeId src_id;
        NodeId dst_parent_id;
    };

    std::vector<CopyEntry> stack;
    // Push all children of the prefab root.
    for (uint32_t i = 0; i < prefab_root_node->child_count; ++i) {
        stack.push_back({prefab_root_node->children[i], instance_id});
    }

    while (!stack.empty()) {
        CopyEntry entry = stack.back();
        stack.pop_back();

        const SceneNode* src = scene_get_node(temp_scene.get(), entry.src_id);
        if (!src) continue;

        NodeId child_id = scene_create_node(
            scene_, src->name, src->type_name, entry.dst_parent_id);
        if (child_id == kNodeIdInvalid) continue;

        SceneNode* child_node = scene_get_node(scene_, child_id);
        if (child_node) {
            child_node->local_transform = src->local_transform;
            child_node->flags = src->flags;
        }

        // Copy component data for child.
        const NodeTypeDef* def = ComponentRegistry::find(src->type_name);
        if (def) {
            for (uint32_t ci = 0; ci < def->comp_count; ++ci) {
                const NodeComponentDef& cd = def->comps[ci];
                const void* src_data = temp_scene->components.get(entry.src_id, cd.type_id);
                if (!src_data) continue;

                void* dst_data = scene_->components.add(child_id, cd.type_id);
                if (dst_data) {
                    std::memcpy(dst_data, src_data, cd.stride);
                }
            }
        }

        // Push this node's children for processing.
        for (uint32_t i = 0; i < src->child_count; ++i) {
            stack.push_back({src->children[i], child_id});
        }
    }

    return Result<NodeId>::Ok(instance_id);
}

Result<uint32_t> PrefabManager::reload_prefab_instances(const char* prefab_path) {
    if (!scene_) return Result<uint32_t>::Err("no scene");
    if (!prefab_path || prefab_path[0] == '\0')
        return Result<uint32_t>::Err("empty prefab path");

    // Load the prefab into a temp scene to get fresh defaults.
    auto temp_scene = std::make_unique<Scene>();
    scene_init(temp_scene.get(), "TempReload");
    component_store_register<PrefabInstanceData>(&temp_scene->components);

    auto load_result = scene_load_file(temp_scene.get(), prefab_path);
    if (load_result.is_err()) {
        return Result<uint32_t>::Err("failed to load prefab: " + load_result.error());
    }

    // Find the prefab root.
    NodeId prefab_root = kNodeIdInvalid;
    for (uint32_t i = 1; i < temp_scene->next_id; ++i) {
        if (temp_scene->nodes[i].alive && temp_scene->nodes[i].parent_id == kNodeIdInvalid) {
            prefab_root = i;
            break;
        }
    }

    if (prefab_root == kNodeIdInvalid) {
        return Result<uint32_t>::Err("prefab has no root node");
    }

    const SceneNode* prefab_root_node = scene_get_node(temp_scene.get(), prefab_root);

    // Scan for all instances of this prefab in the current scene.
    uint32_t reloaded = 0;
    for (uint32_t i = 1; i < scene_->next_id; ++i) {
        if (!scene_->nodes[i].alive) continue;

        auto* pid = component_get<PrefabInstanceData>(&scene_->components, i);
        if (!pid) continue;
        if (std::strcmp(pid->prefab_source, prefab_path) != 0) continue;

        SceneNode* instance_node = scene_get_node(scene_, i);
        if (!instance_node) continue;

        // Apply prefab defaults, then re-apply overrides.
        apply_prefab_defaults(instance_node, prefab_root_node);

        // Re-apply stored overrides.
        for (uint32_t oi = 0; oi < pid->override_count; ++oi) {
            const char* key = pid->override_keys[oi];
            const char* val = pid->override_values[oi];

            if (std::strcmp(key, "position") == 0) {
                float vals[3];
                const char* p = val;
                auto parse_f = [](const char*& ptr, float& out) -> bool {
                    while (*ptr == ' ' || *ptr == '\t') ++ptr;
                    if (*ptr == '\0') return false;
                    char* end = nullptr;
                    out = std::strtof(ptr, &end);
                    if (end == ptr) return false;
                    ptr = end;
                    return true;
                };
                if (parse_f(p, vals[0]) && parse_f(p, vals[1]) && parse_f(p, vals[2])) {
                    instance_node->local_transform.position = {vals[0], vals[1], vals[2], 0.0f};
                }
            } else if (std::strcmp(key, "rotation") == 0) {
                float vals[4];
                const char* p = val;
                auto parse_f = [](const char*& ptr, float& out) -> bool {
                    while (*ptr == ' ' || *ptr == '\t') ++ptr;
                    if (*ptr == '\0') return false;
                    char* end = nullptr;
                    out = std::strtof(ptr, &end);
                    if (end == ptr) return false;
                    ptr = end;
                    return true;
                };
                if (parse_f(p, vals[0]) && parse_f(p, vals[1]) &&
                    parse_f(p, vals[2]) && parse_f(p, vals[3])) {
                    instance_node->local_transform.rotation = {vals[0], vals[1], vals[2], vals[3]};
                }
            } else if (std::strcmp(key, "scale") == 0) {
                float vals[3];
                const char* p = val;
                auto parse_f = [](const char*& ptr, float& out) -> bool {
                    while (*ptr == ' ' || *ptr == '\t') ++ptr;
                    if (*ptr == '\0') return false;
                    char* end = nullptr;
                    out = std::strtof(ptr, &end);
                    if (end == ptr) return false;
                    ptr = end;
                    return true;
                };
                if (parse_f(p, vals[0]) && parse_f(p, vals[1]) && parse_f(p, vals[2])) {
                    instance_node->local_transform.scale = {vals[0], vals[1], vals[2], 0.0f};
                }
            }
        }

        ++reloaded;
    }

    return Result<uint32_t>::Ok(reloaded);
}

// ============================================================================
// PrefabManager — override system
// ============================================================================

Result<std::vector<PrefabOverride>> PrefabManager::get_prefab_overrides(NodeId instance_id) {
    if (!scene_) return Result<std::vector<PrefabOverride>>::Err("no scene");

    const SceneNode* instance_node = scene_get_node(scene_, instance_id);
    if (!instance_node)
        return Result<std::vector<PrefabOverride>>::Err("invalid instance node");

    const auto* pid = component_get<PrefabInstanceData>(&scene_->components, instance_id);
    if (!pid)
        return Result<std::vector<PrefabOverride>>::Err("node is not a prefab instance");

    // Load the prefab to get default values.
    auto temp_scene = std::make_unique<Scene>();
    scene_init(temp_scene.get(), "TempOverrides");
    component_store_register<PrefabInstanceData>(&temp_scene->components);
    auto load_result = scene_load_file(temp_scene.get(), pid->prefab_source);
    if (load_result.is_err()) {
        return Result<std::vector<PrefabOverride>>::Err(
            "failed to load prefab: " + load_result.error());
    }

    // Find the prefab root.
    const SceneNode* prefab_root = nullptr;
    for (uint32_t i = 1; i < temp_scene->next_id; ++i) {
        if (temp_scene->nodes[i].alive && temp_scene->nodes[i].parent_id == kNodeIdInvalid) {
            prefab_root = &temp_scene->nodes[i];
            break;
        }
    }

    if (!prefab_root) {
        return Result<std::vector<PrefabOverride>>::Err("prefab has no root node");
    }

    std::vector<PrefabOverride> overrides;
    compute_overrides(instance_node, prefab_root, overrides);

    return Result<std::vector<PrefabOverride>>::Ok(std::move(overrides));
}

Result<bool> PrefabManager::revert_prefab_override(NodeId instance_id, const char* key) {
    if (!scene_) return Result<bool>::Err("no scene");
    if (!key || key[0] == '\0') return Result<bool>::Err("empty key");

    SceneNode* instance_node = scene_get_node(scene_, instance_id);
    if (!instance_node) return Result<bool>::Err("invalid instance node");

    auto* pid = component_get<PrefabInstanceData>(&scene_->components, instance_id);
    if (!pid) return Result<bool>::Err("node is not a prefab instance");

    // Load prefab defaults.
    auto temp_scene = std::make_unique<Scene>();
    scene_init(temp_scene.get(), "TempRevert");
    component_store_register<PrefabInstanceData>(&temp_scene->components);

    auto load_result = scene_load_file(temp_scene.get(), pid->prefab_source);
    if (load_result.is_err()) {
        return Result<bool>::Err("failed to load prefab: " + load_result.error());
    }

    const SceneNode* prefab_root = nullptr;
    for (uint32_t i = 1; i < temp_scene->next_id; ++i) {
        if (temp_scene->nodes[i].alive && temp_scene->nodes[i].parent_id == kNodeIdInvalid) {
            prefab_root = &temp_scene->nodes[i];
            break;
        }
    }

    if (!prefab_root) {
        return Result<bool>::Err("prefab has no root node");
    }

    // Apply the prefab default for this specific key.
    if (std::strcmp(key, "position") == 0) {
        instance_node->local_transform.position = prefab_root->local_transform.position;
    } else if (std::strcmp(key, "rotation") == 0) {
        instance_node->local_transform.rotation = prefab_root->local_transform.rotation;
    } else if (std::strcmp(key, "scale") == 0) {
        instance_node->local_transform.scale = prefab_root->local_transform.scale;
    } else if (std::strcmp(key, "active") == 0) {
        if (prefab_root->flags & NODE_FLAG_ACTIVE)
            instance_node->flags |= NODE_FLAG_ACTIVE;
        else
            instance_node->flags &= static_cast<uint16_t>(~NODE_FLAG_ACTIVE);
    } else if (std::strcmp(key, "visible") == 0) {
        if (prefab_root->flags & NODE_FLAG_VISIBLE)
            instance_node->flags |= NODE_FLAG_VISIBLE;
        else
            instance_node->flags &= static_cast<uint16_t>(~NODE_FLAG_VISIBLE);
    } else {
        return Result<bool>::Err("unknown property key: " + std::string(key));
    }

    // Remove the override entry.
    pid->remove_override(key);

    return Result<bool>::Ok(true);
}

Result<bool> PrefabManager::revert_all_prefab_overrides(NodeId instance_id) {
    if (!scene_) return Result<bool>::Err("no scene");

    SceneNode* instance_node = scene_get_node(scene_, instance_id);
    if (!instance_node) return Result<bool>::Err("invalid instance node");

    auto* pid = component_get<PrefabInstanceData>(&scene_->components, instance_id);
    if (!pid) return Result<bool>::Err("node is not a prefab instance");

    // Load prefab defaults.
    auto temp_scene = std::make_unique<Scene>();
    scene_init(temp_scene.get(), "TempRevertAll");
    component_store_register<PrefabInstanceData>(&temp_scene->components);

    auto load_result = scene_load_file(temp_scene.get(), pid->prefab_source);
    if (load_result.is_err()) {
        return Result<bool>::Err("failed to load prefab: " + load_result.error());
    }

    const SceneNode* prefab_root = nullptr;
    for (uint32_t i = 1; i < temp_scene->next_id; ++i) {
        if (temp_scene->nodes[i].alive && temp_scene->nodes[i].parent_id == kNodeIdInvalid) {
            prefab_root = &temp_scene->nodes[i];
            break;
        }
    }

    if (!prefab_root) {
        return Result<bool>::Err("prefab has no root node");
    }

    // Apply all prefab defaults.
    apply_prefab_defaults(instance_node, prefab_root);

    // Clear all overrides.
    pid->clear_overrides();

    return Result<bool>::Ok(true);
}

// ============================================================================
// PrefabManager — queries
// ============================================================================

bool PrefabManager::is_prefab_instance(NodeId id) const {
    if (!scene_) return false;
    return component_has<PrefabInstanceData>(&scene_->components, id);
}

std::string PrefabManager::get_prefab_source(NodeId id) const {
    if (!scene_) return {};

    const auto* pid = component_get<PrefabInstanceData>(&scene_->components, id);
    if (!pid) return {};
    return pid->prefab_source;
}

uint32_t PrefabManager::count_instances(const char* prefab_path) const {
    if (!scene_ || !prefab_path) return 0;

    uint32_t count = 0;
    for (uint32_t i = 1; i < scene_->next_id; ++i) {
        if (!scene_->nodes[i].alive) continue;

        const auto* pid = component_get<PrefabInstanceData>(&scene_->components, i);
        if (pid && std::strcmp(pid->prefab_source, prefab_path) == 0) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// PrefabManager — helpers
// ============================================================================

std::string PrefabManager::serialize_float3(float x, float y, float z) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f",
                  static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    return buf;
}

std::string PrefabManager::serialize_float4(float x, float y, float z, float w) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f %.6f",
                  static_cast<double>(x), static_cast<double>(y),
                  static_cast<double>(z), static_cast<double>(w));
    return buf;
}

void PrefabManager::compute_overrides(
    const SceneNode* instance_node,
    const SceneNode* prefab_node,
    std::vector<PrefabOverride>& out) const
{
    constexpr float tol = 1e-5f;
    auto diff = [tol](float a, float b) { return std::abs(a - b) > tol; };

    // Position
    const auto& ip = instance_node->local_transform.position;
    const auto& pp = prefab_node->local_transform.position;
    if (diff(ip.x, pp.x) || diff(ip.y, pp.y) || diff(ip.z, pp.z)) {
        out.push_back({
            "position",
            serialize_float3(ip.x, ip.y, ip.z),
            serialize_float3(pp.x, pp.y, pp.z)
        });
    }

    // Rotation
    const auto& ir = instance_node->local_transform.rotation;
    const auto& pr = prefab_node->local_transform.rotation;
    if (diff(ir.x, pr.x) || diff(ir.y, pr.y) || diff(ir.z, pr.z) || diff(ir.w, pr.w)) {
        out.push_back({
            "rotation",
            serialize_float4(ir.x, ir.y, ir.z, ir.w),
            serialize_float4(pr.x, pr.y, pr.z, pr.w)
        });
    }

    // Scale
    const auto& is = instance_node->local_transform.scale;
    const auto& ps = prefab_node->local_transform.scale;
    if (diff(is.x, ps.x) || diff(is.y, ps.y) || diff(is.z, ps.z)) {
        out.push_back({
            "scale",
            serialize_float3(is.x, is.y, is.z),
            serialize_float3(ps.x, ps.y, ps.z)
        });
    }

    // Active flag
    bool ia = static_cast<bool>(instance_node->flags & NODE_FLAG_ACTIVE);
    bool pa = static_cast<bool>(prefab_node->flags & NODE_FLAG_ACTIVE);
    if (ia != pa) {
        out.push_back({"active", ia ? "true" : "false", pa ? "true" : "false"});
    }

    // Visible flag
    bool iv = static_cast<bool>(instance_node->flags & NODE_FLAG_VISIBLE);
    bool pv = static_cast<bool>(prefab_node->flags & NODE_FLAG_VISIBLE);
    if (iv != pv) {
        out.push_back({"visible", iv ? "true" : "false", pv ? "true" : "false"});
    }
}

void PrefabManager::apply_prefab_defaults(SceneNode* instance_node, const SceneNode* prefab_node) {
    instance_node->local_transform.position = prefab_node->local_transform.position;
    instance_node->local_transform.rotation = prefab_node->local_transform.rotation;
    instance_node->local_transform.scale    = prefab_node->local_transform.scale;
    instance_node->flags = prefab_node->flags;
}

} // namespace uz
