/// ViewportToolbarState — Snapshot of the viewport toolbar's current control state.
///
/// Plain C++ struct. Aggregates the editor's active transform mode, coordinate
/// space, debug view mode, show flags, quality level, and camera parameters into
/// one cohesive value. Updated each frame from SceneRenderer / EditorInputHandler.
///
/// No JSON serialization — this is a runtime-only, frame-local snapshot.

#pragma once

#include "debug_view_mode.h"
#include "editor_input_handler.h"
#include "show_flags.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ViewportToolbarState
// ─────────────────────────────────────────────────────────────────────────────

/// Toolbar profile — controls which button groups are visible.
enum class ToolbarProfile : uint8_t {
    Full,            ///< Level editor — all button groups (transform, view, show flags, quality).
    MaterialPreview, ///< Material editor — view mode + lighting buttons only.
    MeshPreview,     ///< Mesh browser — view modes + wireframe + LOD, no show flags.
    Minimal,         ///< View mode only (single dropdown).
};

/// Current state of the viewport toolbar.
///
/// SceneRenderer populates this from EditorInputHandler + SceneRenderer APIs
/// at the start of each frame, then passes it to ViewportToolbar::Update().
struct ViewportToolbarState {
    GizmoMode     transformMode  = GizmoMode::Translate; ///< Q/W/E/R transform tool.
    GizmoSpace    gizmoSpace     = GizmoSpace::World;    ///< World / Local coord space.
    DebugViewMode viewMode       = DebugViewMode::Lit;   ///< Active visualization mode.
    uint64_t      showFlags      = ShowFlags::AllEnabled().bits; ///< 64-bit ShowFlags snapshot.
    uint8_t       qualityLevel   = 3;                    ///< 0=Low,1=Med,2=High,3=Ultra.
    float         fovY           = 60.f;                 ///< Camera vertical FOV (degrees).
    float         nearPlane      = 10.f;                 ///< Camera near plane (cm).
    float         farPlane       = 1'000'000.f;          ///< Camera far plane (cm).
    ToolbarProfile profile       = ToolbarProfile::Full; ///< Active toolbar profile.
};

} // namespace uz
