// ---------------------------------------------------------------------------
// scene_bridge.cpp -- Scene loading and EditorNode construction.
// ---------------------------------------------------------------------------
#include "scene_bridge.h"

#include "engine/scene/scene_loader.h"
#include "engine/scene/scene_node.h"
#include "engine/core/diag.h"

#include <windows.h>

#include <cstdio>

std::vector<uz::DX12Renderer::EditorNode> buildEditorNodes(const uz::Scene* scene) {
    std::vector<uz::DX12Renderer::EditorNode> editorNodes;
    if (!scene) return editorNodes;

    for (uint32_t i = 1; i < scene->next_id; ++i) {
        const uz::SceneNode& sn = scene->nodes[i];
        if (!sn.alive) continue;

        uz::DX12Renderer::EditorNode edNode;
        edNode.position = {
            sn.local_transform.position.x,
            sn.local_transform.position.y,
            sn.local_transform.position.z
        };
        edNode.rotation = {0.0f, 0.0f, 0.0f}; // legacy euler (unused)
        edNode.rotationQuat = {
            sn.local_transform.rotation.x,
            sn.local_transform.rotation.y,
            sn.local_transform.rotation.z,
            sn.local_transform.rotation.w
        };
        edNode.scale = {
            sn.local_transform.scale.x,
            sn.local_transform.scale.y,
            sn.local_transform.scale.z
        };
        edNode.nodeType = static_cast<uint16_t>(sn.type);
        edNode.nodeId   = i;

        switch (sn.type) {
            case uz::NodeType::Camera:
                edNode.color = {0.9f, 0.9f, 0.2f}; break;
            case uz::NodeType::DirectionalLight:
            case uz::NodeType::PointLight:
                edNode.color = {0.9f, 0.7f, 0.2f}; break;
            case uz::NodeType::StaticMesh:
                edNode.color = {0.2f, 0.8f, 0.9f}; break;
            default:
                edNode.color = {0.8f, 0.8f, 0.8f}; break;
        }
        editorNodes.push_back(edNode);
    }
    return editorNodes;
}

void loadSceneFile(std::unique_ptr<uz::Scene>& sceneData,
                   uz::DX12Renderer& renderer,
                   uz::SceneParams& sceneParams,
                   const std::string& scenePath) {
    // Resolve scene path.
    std::string resolvedPath = scenePath;
    if (resolvedPath.empty()) {
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        auto lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash);
        resolvedPath = exeDir + "/../../assets/scenes/default.uzscene";
    }

    DIAG_FMT("Loading scene: %s", resolvedPath.c_str());
    sceneData = std::make_unique<uz::Scene>();
    uz::scene_init(sceneData.get(), "DefaultScene");

    auto loadResult = uz::scene_load_file(sceneData.get(), resolvedPath.c_str());
    if (loadResult.is_ok()) {
        DIAG("Scene loaded OK, building editor nodes...");
        uz::scene_update_world_transforms(sceneData.get());

        auto editorNodes = buildEditorNodes(sceneData.get());
        renderer.setScene(sceneData.get());
        renderer.loadEditorScene(std::move(editorNodes));

        if (sceneData->editor_state.valid) {
            sceneParams.camPosX  = sceneData->editor_state.cam_pos_x;
            sceneParams.camPosY  = sceneData->editor_state.cam_pos_y;
            sceneParams.camPosZ  = sceneData->editor_state.cam_pos_z;
            sceneParams.camYaw   = sceneData->editor_state.cam_yaw;
            sceneParams.camPitch = sceneData->editor_state.cam_pitch;
            sceneParams.camSpeed = sceneData->editor_state.cam_speed;
        }

        char dbg[256];
        snprintf(dbg, sizeof(dbg), "[UzEngine] Auto-loaded scene: %s (%u nodes)\n",
                 resolvedPath.c_str(), static_cast<unsigned>(sceneData->next_id - 1));
        OutputDebugStringA(dbg);
    } else {
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "[UzEngine] WARNING: Could not load scene: %s\n",
                 resolvedPath.c_str());
        OutputDebugStringA(dbg);
    }
}
