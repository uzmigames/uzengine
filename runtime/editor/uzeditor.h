/// UzEditor — Single umbrella header for editor consumers.
///
/// Include this one header to access all public editor APIs:
/// ```cpp
/// #include "uzeditor.h"
/// ```
///
/// Provides: EditorGrid, StatsOverlay, BitmapFont, TextRenderer,
/// ScreenshotCapture, CompareGrid, ImageDiff.
///
/// This header is only available when linking against `editor.lib`
/// (i.e., when building with `zig build -Deditor=true`).

#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Grid Overlay
// ─────────────────────────────────────────────────────────────────────────────
#include "grid/editor_grid.h"

// ─────────────────────────────────────────────────────────────────────────────
// Font & Text Rendering
// ─────────────────────────────────────────────────────────────────────────────
#include "font/bitmap_font.h"
#include "font/text_renderer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Stats Overlay
// ─────────────────────────────────────────────────────────────────────────────
#include "stats/stats_overlay.h"

// ─────────────────────────────────────────────────────────────────────────────
// Screenshot & Visual Testing
// ─────────────────────────────────────────────────────────────────────────────
#include "screenshot/screenshot_capture.h"
#include "screenshot/compare_grid.h"

// ─────────────────────────────────────────────────────────────────────────────
// Editor Input & Selection
// ─────────────────────────────────────────────────────────────────────────────
#include "input/editor_input_handler.h"
#include "selection/selection_manager.h"
