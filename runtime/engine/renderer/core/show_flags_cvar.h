/// CVar bridge for ShowFlags — maps `r.Show.<Flag>` CVars to ShowFlags bits.
///
/// Call `SyncShowFlagsFromCVars()` once per frame (before render pass
/// registration) to propagate CVar changes into the active ShowFlags.
/// Call `SyncShowFlagsToCVars()` when the toolbar UI or IPC modifies
/// ShowFlags directly so the CVars stay in sync.

#pragma once

namespace uz {

struct ShowFlags;

/// Registers all `r.Show.*` CVars. Safe to call multiple times (idempotent).
/// Called automatically at static init time by the .cpp — no explicit call needed.
void RegisterShowFlagCVars();

/// Reads all `r.Show.*` CVars and updates the corresponding ShowFlags bits.
/// Call once per frame before render pass registration.
void SyncShowFlagsFromCVars(ShowFlags& flags);

/// Writes all ShowFlags bits into the corresponding `r.Show.*` CVars.
/// Call after toolbar/IPC modifies ShowFlags directly.
void SyncShowFlagsToCVars(const ShowFlags& flags);

} // namespace uz
