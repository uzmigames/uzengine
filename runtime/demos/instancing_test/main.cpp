/// Instancing Test -- Entry Point
///
/// GPU instancing stress test: 1000 identical cubes in a 10x10x10 grid.
/// With instancing the InstanceBatcher collapses ~1000 draw calls into 1-2.
///
/// Supported flags:
/// - `--screenshot <path>` -- Capture back buffer and save as PNG
/// - `--frames <N>` -- Total frames to render (0 = run until window closed)
/// - `--compose-grid output.png [--cell-size N] img.png "Label" "Sub" ...`
/// - `--diff output.png imgA.png "A" imgB.png "B"` -- Pixel diff (no GPU)

#include "instancing_test.h"

UZ_APPLICATION_MAIN(uz::InstancingTest)
