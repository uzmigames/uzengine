/// RenderDoc capture integration stub.
///
/// Provides inline no-op stubs for RenderDoc API functions when the full
/// renderdoc_capture module is not available. The demo calls these from
/// initialization and the F11 key handler; without RenderDoc loaded they
/// are safe no-ops.

#pragma once

#include <cstdint>

namespace uz {

/// Attempts to load the RenderDoc in-process API at startup.
/// Returns true if loaded successfully, false otherwise.
inline bool RenderDocInit() { return false; }

/// Sets default capture options (ref all resources, etc). No-op if not loaded.
inline void RenderDocSetCaptureOptions() {}

} // namespace uz

/// Returns true if the RenderDoc in-process API was loaded at startup.
inline bool RenderDocIsLoaded() { return false; }

/// Triggers a single-frame RenderDoc capture. No-op if not loaded.
inline void RenderDocTriggerCapture() {}
