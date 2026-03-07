// ---------------------------------------------------------------------------
// scene_bridge.h -- Scene loading and EditorNode construction.
//
// THREAD SAFETY: Scene loading does CPU work (file parsing, node building)
// on the IPC thread, then queues the result via enqueuePendingScene().
// The main render thread consumes the pending load via dequeuePendingScene()
// and applies it (renderer.setScene, IBL baking, camera state) — ensuring
// all GPU work happens on a single thread.
// ---------------------------------------------------------------------------
#pragma once

#include "engine/renderer/core/renderer.h"
#include "engine/renderer/scene/scene_params.h"
#include "engine/scene/scene.h"

#include <memory>
#include <string>
#include <vector>

/// Build EditorNode representations from all alive nodes in a Scene.
std::vector<uz::Renderer::EditorNode> buildEditorNodes(const uz::Scene* scene);

// ---------------------------------------------------------------------------
// PendingSceneLoad — thread-safe scene load handoff (IPC thread → main thread)
// ---------------------------------------------------------------------------
struct PendingSceneLoad {
    std::unique_ptr<uz::Scene> scene;
    std::vector<uz::Renderer::EditorNode> editor_nodes;
    float cam_pos_x = 0, cam_pos_y = 0, cam_pos_z = 0;
    float cam_yaw = 0, cam_pitch = -0.6f, cam_speed = 10.0f;
    bool has_camera = false;
};

/// Queue a loaded scene for the main thread to apply.  Thread-safe.
void enqueuePendingScene(std::unique_ptr<PendingSceneLoad> load);

/// Consume the pending scene load (returns nullptr if none).  Thread-safe.
std::unique_ptr<PendingSceneLoad> dequeuePendingScene();

/// Load a .uzscene file (CPU work only) and enqueue for main-thread application.
/// If scenePath is empty, uses the default scene relative to the executable.
/// Returns {scene_name, node_count} for IPC response, or {"", 0} on failure.
struct SceneLoadResult { std::string name; int node_count; };
SceneLoadResult loadSceneFileDeferred(const std::string& scenePath);

/// Apply a pending scene load on the main thread: swaps scene data, calls
/// renderer.setScene(), applies camera state, and drains stale camera input.
void applyPendingScene(PendingSceneLoad& pending,
                       std::unique_ptr<uz::Scene>& sceneData,
                       uz::Renderer& renderer,
                       uz::SceneParams& sceneParams);
