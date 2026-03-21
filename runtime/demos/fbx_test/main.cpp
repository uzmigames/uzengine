/// FBX Test — Entry Point
///
/// Loads a FBX character mesh, converts to .uzmesh, applies PBR textures,
/// and renders with the deferred pipeline.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--frames <N>` — Total frames to render (0 = run until window closed)
/// - `--compose-grid output.png [--cell-size N] img.png "Label" "Sub" ...`
/// - `--diff output.png imgA.png "A" imgB.png "B"` — Pixel diff (no GPU)

#include "fbx_test.h"

UZ_APPLICATION_MAIN(uz::FBXTest)
