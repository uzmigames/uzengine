#pragma once

// ipc_asset_handlers.h -- Register asset.* IPC handlers into an IpcRegistry.
//
// Call asset_register_handlers() once at startup, passing a pointer to the
// live AssetRegistry. All subsequent asset.* IPC calls operate on that registry.
//
// Handlers:
//   asset.list           — { type? }  -> { ok, assets: [...] }
//   asset.import         — { path }   -> { ok, error? }
//   asset.getInfo        — { path }   -> { ok, info: {...} }
//   asset.createMaterial — { name }   -> { ok, path }
//   asset.createScene    — { name }   -> { ok, path }
//   asset.createShader   — { name }   -> { ok, path }

namespace uz {

class IpcRegistry;
class AssetRegistry;

/// Register all asset.* handlers into the given registry.
/// registry and reg must remain valid for the lifetime of the handlers.
void asset_register_handlers(IpcRegistry& registry, AssetRegistry* reg);

} // namespace uz
