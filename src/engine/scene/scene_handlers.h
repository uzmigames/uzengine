#pragma once

// scene_handlers.h -- Register all scene.* IPC handlers into an IpcRegistry.
//
// Call scene_register_handlers() once at startup, passing a pointer to the
// live Scene. All subsequent IPC calls operate on that scene.

#include "common/ipc_registry.h"
#include "engine/scene/scene.h"

namespace uz {

struct SceneParams;

/// Register all scene.* handlers into the given registry.
/// scene must remain valid for the lifetime of the registry.
/// params may be null if editor camera persistence is not needed.
void scene_register_handlers(IpcRegistry& registry, Scene* scene, SceneParams* params = nullptr);

} // namespace uz
