/// Post-Process Test — Entry Point
///
/// Renders 3 cubes with post-processing (tonemap + FXAA) via SceneRenderer.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--frames <N>` — Total frames to render (0 = run until window closed)
/// - `--compose-grid output.png [--cell-size N] img.png "Label" "Sub" ...`
/// - `--diff output.png imgA.png "A" imgB.png "B"` — Pixel diff (no GPU)

#include "postprocess_test.h"

UZ_APPLICATION_MAIN(uz::PostProcessTest)
