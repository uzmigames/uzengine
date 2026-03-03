#pragma once

// ipc_shader_handlers.h -- Register shader.* IPC handlers into an IpcRegistry.
//
// Call shader_register_handlers() once at startup, passing a pointer to the
// live ShaderManager. All subsequent shader.* IPC calls operate on that manager.

namespace uz {
class IpcRegistry;
class ShaderManager;

/// Register all shader.* handlers into the given registry.
/// mgr must remain valid for the lifetime of the registry.
void shader_register_handlers(IpcRegistry& registry, ShaderManager* mgr);

} // namespace uz
