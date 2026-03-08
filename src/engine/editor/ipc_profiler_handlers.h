#pragma once

// ---------------------------------------------------------------------------
// ipc_profiler_handlers.h -- Register profiler.* IPC handlers.
//
// Handlers:
//   profiler.enable         — {} -> { ok }
//   profiler.disable        — {} -> { ok }
//   profiler.get_frame_data — { frames? } -> { ok, zones: [...] }
// ---------------------------------------------------------------------------

namespace uz {

class IpcRegistry;

/// Register all profiler.* handlers into the given registry.
void profiler_register_ipc_handlers(IpcRegistry& registry);

} // namespace uz
