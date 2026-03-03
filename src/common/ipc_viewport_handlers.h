#pragma once

// ipc_viewport_handlers.h -- Register viewport.* IPC handlers into an IpcRegistry.
//
// Call viewport_register_handlers() once at startup, passing a pointer to the
// live ViewportRegistry. All subsequent viewport.* IPC calls operate on that
// registry.

namespace uz {
class IpcRegistry;
class ViewportRegistry;

/// Register all viewport.* handlers into the given registry.
/// reg must remain valid for the lifetime of the IPC registry.
void viewport_register_handlers(IpcRegistry& registry, ViewportRegistry* reg);

} // namespace uz
