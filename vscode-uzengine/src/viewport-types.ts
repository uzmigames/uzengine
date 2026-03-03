/**
 * viewport-types.ts — Shared type definitions for the ViewportManager system.
 *
 * These types are the single source of truth for viewport identity, lifecycle
 * state, classification, and configuration across the VSCode extension.
 * They mirror the C-side enums in src/engine/renderer/viewport_manager.h.
 */

// ---------------------------------------------------------------------------
// Viewport identity
// ---------------------------------------------------------------------------

/** Unique integer identifier for a viewport, assigned by the engine on creation.
 *  The value 0 (VIEWPORT_ID_INVALID) is reserved and means "not yet assigned". */
export type ViewportId = number;

/** Sentinel value for an unassigned viewport ID. Mirrors C-side VIEWPORT_ID_INVALID = 0. */
export const VIEWPORT_ID_INVALID: ViewportId = 0;

// ---------------------------------------------------------------------------
// Viewport lifecycle state
// ---------------------------------------------------------------------------

/**
 * The lifecycle state of a viewport, mirroring the C-side ViewportState enum.
 *
 * Transitions:
 *   inactive  → active     (setActive true, tab becomes visible)
 *   active    → suspended  (setActive false, tab hidden or window unfocused)
 *   suspended → active     (setActive true, tab regains focus)
 *   any       → destroyed  (viewport.destroy IPC call / panel disposed)
 */
export type ViewportState = 'inactive' | 'active' | 'suspended' | 'destroyed';

// ---------------------------------------------------------------------------
// Viewport type
// ---------------------------------------------------------------------------

/**
 * The rendering purpose of a viewport. Determines which render graph the
 * engine instantiates inside the viewport. Mirrors C-side ViewportType enum.
 *
 * - 'scene'      : Main 3D world view (Scene editor panel)
 * - 'vfx_preview': Isolated VFX effect preview (VFX node editor)
 * - 'material'   : Sphere/plane with material applied (Material inspector)
 * - 'mesh'       : Mesh thumbnail + detail view (Asset browser)
 * - 'texture'    : Texture viewer with mip/channel control (Asset browser)
 * - 'prefab'     : Isolated prefab viewport (Prefab editor)
 * - 'animation'  : Character + animation clip viewer (Animation editor)
 */
export type ViewportType =
    | 'scene'
    | 'vfx_preview'
    | 'material'
    | 'mesh'
    | 'texture'
    | 'prefab'
    | 'animation';

// ---------------------------------------------------------------------------
// Viewport configuration (used when creating a viewport)
// ---------------------------------------------------------------------------

/**
 * Parameters passed to the engine when creating a new viewport.
 * All fields except type/width/height have sensible defaults.
 */
export interface ViewportConfig {
    /** Rendering purpose — determines which scene/pass the engine renders. */
    readonly type: ViewportType;
    /** Panel title shown in the VSCode tab bar. */
    readonly title: string;
    /** Initial render width in pixels. */
    readonly width: number;
    /** Initial render height in pixels. */
    readonly height: number;
    /** Target render frame rate. 0 = uncapped. Defaults to 60. */
    readonly targetFps?: number;
    /** JPEG encode quality 75–95. Lower = less bandwidth. Defaults to 85. */
    readonly jpegQuality?: number;
    /** Human-readable name for engine-side debug logs. */
    readonly debugName?: string;
}

// ---------------------------------------------------------------------------
// Viewport status (returned by engine IPC or status push)
// ---------------------------------------------------------------------------

/**
 * Runtime metrics snapshot for a single viewport.
 * Mirrors C-side ViewportStatus struct.
 */
export interface ViewportStatus {
    /** The viewport's unique ID. */
    readonly id: ViewportId;
    /** Current lifecycle state. */
    readonly state: ViewportState;
    /** Measured FPS (exponential moving average over 30 frames). */
    readonly fps: number;
    /** Approximate VRAM used by this viewport's render targets in bytes. */
    readonly vramBytes: number;
    /** Current render width (may differ from config after resize). */
    readonly width: number;
    /** Current render height (may differ from config after resize). */
    readonly height: number;
}

// ---------------------------------------------------------------------------
// IPC result shapes (responses from engine IPC calls)
// ---------------------------------------------------------------------------

/** Shape of the `result` field returned by a successful `viewport.create` IPC call. */
export interface ViewportCreateResult {
    /** The unique ID assigned by the engine to the new viewport. */
    readonly viewport_id: ViewportId;
    /** WebSocket port for this viewport's JPEG stream (shared multiplexed connection). */
    readonly ws_port: number;
    /** Initial VRAM allocation in bytes. */
    readonly vram_bytes: number;
}

/** Shape of the `result` field returned by `viewport.setActive`. */
export interface ViewportSetActiveResult {
    /** New state of the viewport after the call. */
    readonly state: ViewportState;
}

/** Shape of the `result` field returned by `viewport.getStatus`. */
export interface ViewportGetStatusResult {
    readonly viewport_id: ViewportId;
    readonly state: ViewportState;
    readonly fps: number;
    readonly vram_bytes: number;
    readonly width: number;
    readonly height: number;
}

// ---------------------------------------------------------------------------
// WebSocket control channel push messages (engine → extension)
// ---------------------------------------------------------------------------

/** Discriminated union of all engine-initiated push messages on the control channel. */
export type ViewportPushMessage =
    | ViewportSuspendedPush
    | ViewportDestroyedPush
    | ViewportStatusPush;

/** Emitted when the engine suspends a viewport due to resource pressure. */
export interface ViewportSuspendedPush {
    readonly type: 'viewport.suspended';
    readonly viewport_id: ViewportId;
    readonly reason: string;
}

/** Emitted when the engine destroys a viewport. */
export interface ViewportDestroyedPush {
    readonly type: 'viewport.destroyed';
    readonly viewport_id: ViewportId;
}

/** Emitted once per second per active viewport with current metrics. */
export interface ViewportStatusPush {
    readonly type: 'viewport.status';
    readonly viewport_id: ViewportId;
    readonly fps: number;
    readonly vram_bytes: number;
}
