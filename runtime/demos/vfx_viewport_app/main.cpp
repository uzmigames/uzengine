/// VFX Viewport Application — Entry Point
///
/// Uses the engine's Application framework instead of the legacy DemoApplication.
/// All RHI management, window lifecycle, rendering, and VFX simulation are handled
/// by the engine. The application only creates ECS entities and handles input.
///
/// This is the canonical engine-integrated version of the VFX viewport.
/// The legacy DemoApplication version remains at demos/vfx_viewport/ for
/// standalone testing and as a reference implementation.

#include "vfx_viewport_app.h"

UZ_APPLICATION_MAIN(uz::VFXViewportApp)
