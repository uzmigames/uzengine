// ---------------------------------------------------------------------------
// scene_bridge.h -- Scene loading and EditorNode construction.
// ---------------------------------------------------------------------------
#pragma once

#include "engine/renderer/renderer.h"
#include "engine/renderer/scene_params.h"
#include "engine/scene/scene.h"

#include <memory>
#include <string>
#include <vector>

/// Build EditorNode representations from all alive nodes in a Scene.
std::vector<uz::Renderer::EditorNode> buildEditorNodes(const uz::Scene* scene);

/// Load a .uzscene file, build EditorNodes, and configure the renderer + camera.
/// If scenePath is empty, uses the default scene relative to the executable.
void loadSceneFile(std::unique_ptr<uz::Scene>& sceneData,
                   uz::Renderer& renderer,
                   uz::SceneParams& sceneParams,
                   const std::string& scenePath);
