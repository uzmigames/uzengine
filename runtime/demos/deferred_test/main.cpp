/// Deferred Test — Entry Point
///
/// Renders 5 PBR cubes with G-Buffer debug view modes (keys 0-9).
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--frames <N>` — Total frames to render (0 = run until window closed)
/// - `--compose-grid output.png [--cell-size N] img.png "Label" "Sub" ...`
/// - `--diff output.png imgA.png "A" imgB.png "B"` — Pixel diff (no GPU)

#include "deferred_test.h"

UZ_APPLICATION_MAIN(uz::DeferredTest)
