#pragma once

// ipc_shader_cmd_handlers.h -- Register shader_cmd.* IPC handlers.
//
// Call shader_cmd_register_handlers() once at startup, passing a pointer to a
// CommandExecutor. All subsequent shader_cmd.* IPC calls operate on that executor.
//
// Handlers:
//   shader_cmd.execute — { type, params }          -> { ok, data?, error?, events? }
//   shader_cmd.batch   — { commands: [...] }       -> { ok, results?, error?, events? }
//   shader_cmd.query   — { query, ...params }      -> { ok, data?, error? }

namespace uz {

class IpcRegistry;

namespace sg {
class CommandExecutor;
} // namespace sg

/// Register all shader_cmd.* handlers into the given registry.
/// registry and executor must remain valid for the lifetime of the handlers.
void shader_cmd_register_handlers(IpcRegistry& registry, sg::CommandExecutor* executor);

} // namespace uz
