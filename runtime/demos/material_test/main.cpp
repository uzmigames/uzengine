/// Material Test — Entry Point
///
/// Renders 5 objects demonstrating all material blend modes: Emissive, Masked,
/// Translucent, Additive, and standard Opaque — plus a ground plane, sun, sky,
/// and sky light for full lighting context.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--frames <N>` — Total frames to render (0 = run until window closed)
/// - `--compose-grid output.png [--cell-size N] img.png "Label" "Sub" ...`
/// - `--diff output.png imgA.png "A" imgB.png "B"` — Pixel diff (no GPU)

#include "material_test.h"

UZ_APPLICATION_MAIN(uz::MaterialTest)
