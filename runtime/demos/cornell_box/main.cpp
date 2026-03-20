/// Cornell Box Demo — Entry Point
///
/// Renders a classic Cornell box scene for testing GI color bleeding.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--screenshot-stdout` — Capture back buffer as base64 PNG to stdout
/// - `--frames <N>` — Total frames to render (0 = run until window closed)
/// - `--headless` — Create window but never show it

#include "cornell_box.h"

UZ_APPLICATION_MAIN(uz::CornellBoxDemo)
