/// Camera Test — Entry Point
///
/// Renders a lit cube using the uz::Application abstraction.
/// SceneRenderer handles all GPU infrastructure; this demo only defines scene logic.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--frames <N>` — Total frames to render (0 = run until window closed)
/// - `--compose-grid output.png [--cell-size N] img.png "Label" "Sub" ...`
/// - `--diff output.png imgA.png "A" imgB.png "B"` — Pixel diff (no GPU)

#include "camera_test.h"

UZ_APPLICATION_MAIN(uz::CameraTest)
