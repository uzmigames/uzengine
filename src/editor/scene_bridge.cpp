// ---------------------------------------------------------------------------
// scene_bridge.cpp -- Scene loading and EditorNode construction.
// ---------------------------------------------------------------------------
#include "scene_bridge.h"

#include "engine/renderer/core/render_path.h"
#include "engine/scene/scene_loader.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_registry.h"
#include "engine/core/crash_reporter.h"
#include "engine/core/diag.h"
#include "engine/core/profiler.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

// ---------------------------------------------------------------------------
// Thread-safe pending scene load queue (single slot — latest wins)
// ---------------------------------------------------------------------------
static std::mutex g_pendingMutex;
static std::unique_ptr<PendingSceneLoad> g_pendingScene;

void enqueuePendingScene(std::unique_ptr<PendingSceneLoad> load) {
    std::lock_guard<std::mutex> lk(g_pendingMutex);
    g_pendingScene = std::move(load);
}

std::unique_ptr<PendingSceneLoad> dequeuePendingScene() {
    std::lock_guard<std::mutex> lk(g_pendingMutex);
    return std::move(g_pendingScene);
}

std::vector<uz::Renderer::EditorNode> buildEditorNodes(const uz::Scene* scene) {
    std::vector<uz::Renderer::EditorNode> editorNodes;
    if (!scene) return editorNodes;

    for (uint32_t i = 1; i < scene->next_id; ++i) {
        const uz::SceneNode& sn = scene->nodes[i];
        if (!sn.alive) continue;

        uz::Renderer::EditorNode edNode;
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
        edNode.nodeId   = i;

        // Look up type metadata from the registry — zero hardcoded switch arms.
        const uz::NodeTypeDef* def = uz::ComponentRegistry::find(sn.type_name);
        if (def) {
            edNode.nodeType = def->type_index;
            edNode.color    = {def->editor_color[0], def->editor_color[1], def->editor_color[2]};
        } else {
            edNode.nodeType = 0;
            edNode.color    = {0.8f, 0.8f, 0.8f};
        }
        editorNodes.push_back(edNode);
    }
    return editorNodes;
}

// ---------------------------------------------------------------------------
// resolveScenePath — shared path resolution logic
// ---------------------------------------------------------------------------
static std::string resolveScenePath(const std::string& scenePath) {
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir(exePath);
    auto lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash);
    std::string projectRoot = exeDir + "/../../";

    if (scenePath.empty()) {
        return projectRoot + "assets/scenes/default.uzscene";
    }

    // If the path doesn't exist as-is, try resolving from the project root.
    FILE* test = fopen(scenePath.c_str(), "r");
    if (!test) {
        std::string tryRoot = projectRoot + scenePath;
        FILE* test2 = fopen(tryRoot.c_str(), "r");
        if (test2) {
            fclose(test2);
            DIAG_FMT("Scene path resolved via project root: %s", tryRoot.c_str());
            return tryRoot;
        }
    } else {
        fclose(test);
    }
    return scenePath;
}

// ---------------------------------------------------------------------------
// loadSceneFileDeferred — CPU-only scene parsing, queues for main thread.
// Safe to call from any thread (no GPU work, no renderer calls).
// ---------------------------------------------------------------------------
SceneLoadResult loadSceneFileDeferred(const std::string& scenePath) {
    std::string resolvedPath = resolveScenePath(scenePath);

    DIAG_FMT("Loading scene (deferred): %s", resolvedPath.c_str());
    auto pending = std::make_unique<PendingSceneLoad>();
    pending->scene = std::make_unique<uz::Scene>();
    uz::scene_init(pending->scene.get(), "DefaultScene");

    auto loadResult = uz::scene_load_file(pending->scene.get(), resolvedPath.c_str());
    if (!loadResult.is_ok()) {
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "[UzEngine] WARNING: Could not load scene: %s\n",
                 resolvedPath.c_str());
        OutputDebugStringA(dbg);
        return {"", 0};
    }

    DIAG("Scene parsed OK, building editor nodes (CPU only)...");
    uz::scene_update_world_transforms(pending->scene.get());
    pending->editor_nodes = buildEditorNodes(pending->scene.get());

    if (pending->scene->editor_state.valid) {
        pending->cam_pos_x = pending->scene->editor_state.cam_pos_x;
        pending->cam_pos_y = pending->scene->editor_state.cam_pos_y;
        pending->cam_pos_z = pending->scene->editor_state.cam_pos_z;
        pending->cam_yaw   = pending->scene->editor_state.cam_yaw;
        pending->cam_pitch = pending->scene->editor_state.cam_pitch;
        pending->cam_speed = pending->scene->editor_state.cam_speed;
        pending->has_camera = true;
    }

    SceneLoadResult result;
    result.name = pending->scene->name;
    result.node_count = static_cast<int>(pending->scene->next_id - 1);

    // Set crash metadata.
    uz::CrashReporter::set_metadata("active_scene", resolvedPath);
    uz::CrashReporter::set_metadata("scene_node_count",
                                     std::to_string(result.node_count));

    char dbg[256];
    snprintf(dbg, sizeof(dbg), "[UzEngine] Scene queued for main thread: %s (%d nodes)\n",
             resolvedPath.c_str(), result.node_count);
    OutputDebugStringA(dbg);

    enqueuePendingScene(std::move(pending));
    return result;
}

// ---------------------------------------------------------------------------
// applyPendingScene — MAIN THREAD ONLY.
// Swaps scene data, calls renderer.setScene() (IBL baking), applies camera
// state, and drains accumulated input to prevent camera jumps.
// ---------------------------------------------------------------------------
void applyPendingScene(PendingSceneLoad& pending,
                       std::unique_ptr<uz::Scene>& sceneData,
                       uz::Renderer& renderer,
                       uz::SceneParams& sceneParams) {
    UZ_PROFILE_ZONE("applyPendingScene");

    // Swap scene ownership to the main scene pointer.
    sceneData = std::move(pending.scene);

    // Apply render path from scene file (0 = Forward, 1 = Deferred).
    renderer.setRenderPath(sceneData->render_path == 0
        ? uz::RenderPath::Forward : uz::RenderPath::Deferred);

    // Apply post-process settings from scene.
    renderer.setAoParams(sceneData->ao_radius, sceneData->ao_intensity);
    renderer.setContactShadows(sceneData->contact_shadows, sceneData->contact_shadow_length);
    renderer.setTaaEnabled(sceneData->taa_enabled);
    renderer.setAutoExposure(sceneData->exposure_mode == 0, sceneData->exposure_value);
    renderer.setSsr(sceneData->ssr_enabled, sceneData->ssr_max_roughness);
    renderer.setColorGrading(sceneData->color_temperature, sceneData->color_tint,
                              sceneData->color_saturation, sceneData->color_contrast);
    renderer.setCinematicPost(sceneData->chromatic_aberration, sceneData->vignette_intensity,
                               sceneData->vignette_falloff, sceneData->film_grain_strength);
    renderer.setDof(sceneData->dof_focus_distance, sceneData->dof_aperture, sceneData->dof_focal_length);
    renderer.setFog(sceneData->fog_density, sceneData->fog_color,
                     sceneData->fog_height_falloff, sceneData->fog_base_height, sceneData->fog_max_opacity);

    // GPU work: setScene does IBL cubemap generation + baking.
    // This is safe here because we're on the main render thread.
    {
        UZ_PROFILE_ZONE("applyPending/setScene");
        renderer.setScene(sceneData.get());
    }
    renderer.loadEditorScene(std::move(pending.editor_nodes));

    // Apply camera state from the scene file.
    if (pending.has_camera) {
        sceneParams.camPosX  = pending.cam_pos_x;
        sceneParams.camPosY  = pending.cam_pos_y;
        sceneParams.camPosZ  = pending.cam_pos_z;
        sceneParams.camYaw   = pending.cam_yaw;
        sceneParams.camPitch = pending.cam_pitch;
        sceneParams.camSpeed = pending.cam_speed;
    }

    // Drain accumulated camera input deltas to prevent a sudden camera
    // jump from input that piled up during scene loading.
    auto& ci = sceneParams.cameraInput;
    ci.mouseDeltaX.store(0.0f, std::memory_order_relaxed);
    ci.mouseDeltaY.store(0.0f, std::memory_order_relaxed);
    ci.scrollDelta.store(0.0f, std::memory_order_relaxed);
    ci.moveForward.store(0.0f, std::memory_order_relaxed);
    ci.moveBack.store(0.0f,    std::memory_order_relaxed);
    ci.moveLeft.store(0.0f,    std::memory_order_relaxed);
    ci.moveRight.store(0.0f,   std::memory_order_relaxed);
    ci.moveUp.store(0.0f,      std::memory_order_relaxed);
    ci.moveDown.store(0.0f,    std::memory_order_relaxed);

    DIAG("Scene applied on main thread (GPU work done safely).");
}
