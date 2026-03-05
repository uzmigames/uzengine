// scene_saver.cpp — .uzscene serializer implementation.
//
// Serializes a Scene to the .uzscene INI-like text format.
// Output is round-trip safe with scene_loader.

#include "engine/scene/scene_saver.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"

#include <cstdio>
#include "common/safe_string.h"
#include <string>

namespace uz {

// ============================================================================
// Serialization
// ============================================================================

std::string scene_save_to_string(const Scene* scene) {
    if (!scene) {
        return {};
    }

    std::string out;
    // Reserve a reasonable initial capacity to reduce reallocations.
    out.reserve(4096);

    char buf[4096];

    // ---- [uzscene] header ----
    out += "[uzscene]\n";

    std::snprintf(buf, sizeof(buf), "name = %s\n", scene->name);
    out += buf;

    std::snprintf(buf, sizeof(buf), "version = %u\n", scene->version);
    out += buf;

    // ---- [node] sections ----
    for (uint32_t i = 1; i < scene->next_id; ++i) {
        const SceneNode& node = scene->nodes[i];
        if (!node.alive) {
            continue;
        }

        out += "\n[node]\n";

        // Name.
        std::snprintf(buf, sizeof(buf), "name = %s\n", node.name);
        out += buf;

        // Type.
        std::snprintf(buf, sizeof(buf), "type = %s\n", node_type_to_string(node.type));
        out += buf;

        // Parent: empty string if kNodeIdInvalid, otherwise the parent's name.
        if (node.parent_id == kNodeIdInvalid) {
            out += "parent =\n";
        } else {
            const SceneNode* parent = scene_get_node(scene, node.parent_id);
            if (parent) {
                std::snprintf(buf, sizeof(buf), "parent = %s\n", parent->name);
                out += buf;
            } else {
                out += "parent =\n";
            }
        }

        // Position (3 floats: x y z).
        std::snprintf(buf, sizeof(buf), "position = %.6f %.6f %.6f\n",
                       static_cast<double>(node.local_transform.position.x),
                       static_cast<double>(node.local_transform.position.y),
                       static_cast<double>(node.local_transform.position.z));
        out += buf;

        // Rotation (4 floats: x y z w).
        std::snprintf(buf, sizeof(buf), "rotation = %.6f %.6f %.6f %.6f\n",
                       static_cast<double>(node.local_transform.rotation.x),
                       static_cast<double>(node.local_transform.rotation.y),
                       static_cast<double>(node.local_transform.rotation.z),
                       static_cast<double>(node.local_transform.rotation.w));
        out += buf;

        // Scale (3 floats: x y z).
        std::snprintf(buf, sizeof(buf), "scale = %.6f %.6f %.6f\n",
                       static_cast<double>(node.local_transform.scale.x),
                       static_cast<double>(node.local_transform.scale.y),
                       static_cast<double>(node.local_transform.scale.z));
        out += buf;

        // Active.
        out += (node.flags & NODE_FLAG_ACTIVE) ? "active = true\n" : "active = false\n";

        // Visible.
        out += (node.flags & NODE_FLAG_VISIBLE) ? "visible = true\n" : "visible = false\n";

        // Component-specific properties.
        if (node.type == NodeType::Camera) {
            const CameraData* cd = component_get<CameraData>(&scene->components, node.id);
            if (cd) {
                std::snprintf(buf, sizeof(buf), "fov = %.6f\n",
                               static_cast<double>(cd->fov_y));
                out += buf;
                std::snprintf(buf, sizeof(buf), "near = %.6f\n",
                               static_cast<double>(cd->near_plane));
                out += buf;
                std::snprintf(buf, sizeof(buf), "far = %.6f\n",
                               static_cast<double>(cd->far_plane));
                out += buf;
            }
        }

        if (node.type == NodeType::DirectionalLight) {
            const DirectionalLightData* ld = component_get<DirectionalLightData>(&scene->components, node.id);
            if (ld) {
                std::snprintf(buf, sizeof(buf), "light_color = %.6f %.6f %.6f\n",
                               static_cast<double>(ld->color.x),
                               static_cast<double>(ld->color.y),
                               static_cast<double>(ld->color.z));
                out += buf;
                std::snprintf(buf, sizeof(buf), "light_intensity = %.6f\n",
                               static_cast<double>(ld->intensity));
                out += buf;
            }
        }
    }

    // ---- [editor] section (camera state for the editor) ----
    {
        const EditorState& es = scene->editor_state;
        out += "\n[editor]\n";

        std::snprintf(buf, sizeof(buf), "camera_position = %.6f %.6f %.6f\n",
                       static_cast<double>(es.cam_pos_x),
                       static_cast<double>(es.cam_pos_y),
                       static_cast<double>(es.cam_pos_z));
        out += buf;

        std::snprintf(buf, sizeof(buf), "camera_yaw = %.6f\n",
                       static_cast<double>(es.cam_yaw));
        out += buf;

        std::snprintf(buf, sizeof(buf), "camera_pitch = %.6f\n",
                       static_cast<double>(es.cam_pitch));
        out += buf;

        std::snprintf(buf, sizeof(buf), "camera_speed = %.6f\n",
                       static_cast<double>(es.cam_speed));
        out += buf;
    }

    return out;
}

// ============================================================================
// File I/O
// ============================================================================

SceneSaveResult scene_save_file(const Scene* scene, const char* path) {
    if (!scene) {
        return SceneSaveResult::Err("scene_save_file: scene is null");
    }
    if (!path) {
        return SceneSaveResult::Err("scene_save_file: path is null");
    }

    std::string text = scene_save_to_string(scene);

    FILE* f = uz::file_open(path, "wb");
    if (!f) {
        return SceneSaveResult::Err(
            std::string("scene_save_file: cannot open file for writing: ") + path);
    }

    size_t written = std::fwrite(text.c_str(), 1, text.size(), f);
    std::fclose(f);

    if (written != text.size()) {
        return SceneSaveResult::Err(
            std::string("scene_save_file: failed to write file: ") + path);
    }

    return SceneSaveResult::Ok(true);
}

} // namespace uz
