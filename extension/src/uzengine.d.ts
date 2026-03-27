/// UzEngine N-API addon TypeScript declarations.
///
/// These types describe the native module loaded via:
/// ```typescript
/// const addon: UzEngineAddon = require('./native/uzengine.node');
/// ```
///
/// All functions call directly into the C++ runtime via N-API — zero IPC
/// overhead. The addon is a shared library (.node file) compiled from
/// `napi/src/addon.cpp` and linked against the Zig-built runtime static lib.
///
/// # Thread Safety
/// All functions must be called from the Node.js main thread (the thread
/// that created the viewport), except `setLogCallback` which installs a
/// ThreadSafeFunction that safely marshals log events from internal C++
/// threads back to the JS event loop.
///
/// # Memory Ownership
/// `captureScreenshot()` returns a `Buffer` that is already a Node-owned
/// copy — the underlying C allocation is freed before the function returns.
/// No manual cleanup is required.

/// Per-frame rendering statistics returned by `renderFrame()`.
///
/// All fields are zero when no viewport is active or the frame was skipped
/// (e.g. window minimised).
export interface FrameStats {
    /** Smoothed frames-per-second (1000 / frameMs). */
    fps: number;
    /** Wall-clock frame time in milliseconds. */
    frameMs: number;
    /** Number of draw calls submitted this frame. */
    drawCalls: number;
    /** Total vertices processed this frame. */
    vertices: number;
    /** Total triangles processed this frame. */
    triangles: number;
    /** Monotonically increasing frame counter (wraps at 2^32). */
    frameIndex: number;
}

/// Log severity levels matching the uz_log_callback `level` argument.
export const enum LogLevel {
    Trace   = 0,
    Debug   = 1,
    Info    = 2,
    Warning = 3,
    Error   = 4,
    Fatal   = 5,
}

/// Callback type used by `setLogCallback`.
///
/// # Arguments
/// - `level` — Severity; compare against `LogLevel` members.
/// - `category` — Subsystem name, e.g. `"RHI"`, `"Scene"`, `"Engine"`.
/// - `message` — Formatted log message.
export type LogCallback = (level: number, category: string, message: string) => void;

/// UzEngine native addon interface.
///
/// Obtain an instance by requiring the compiled .node file:
/// ```typescript
/// import type { UzEngineAddon } from './uzengine';
/// const uz = require('./native/uzengine.node') as UzEngineAddon;
/// ```
export interface UzEngineAddon {

    // ── Lifecycle ──────────────────────────────────────────────────────────────

    /**
     * Override the engine resource directory for DLL/shader discovery.
     * Call before `init()`. When running as a .node addon inside Electron,
     * pass the addon's directory so the engine finds dxcompiler.dll etc.
     */
    setEngineDir(path: string): void;

    /**
     * Returns the version string of the UzEngine runtime, e.g. `"0.7.0"`.
     * Safe to call before `init()`.
     */
    getVersion(): string;

    /**
     * Returns the active rendering backend identifier.
     * Possible values: `"dx12"`, `"dx11"`, `"vulkan"`, `"metal"`, `"none"`.
     * Returns `"none"` before `init()` or after `shutdown()`.
     */
    getBackend(): string;

    /**
     * Initialises the UzEngine runtime.
     *
     * Sets up the logging system and global engine state. Must be called
     * before any other function (except `getVersion`).
     *
     * @throws Error if initialisation fails.
     */
    init(): void;

    /**
     * Shuts down the UzEngine runtime.
     *
     * Destroys the viewport (if active), flushes the GPU, and releases
     * all resources. Safe to call if `init()` failed or was never called.
     */
    shutdown(): void;

    // ── Viewport ───────────────────────────────────────────────────────────────

    /**
     * Creates a DX12 viewport and full rendering pipeline.
     *
     * Initialises the RHI device, swapchain, SceneRenderer, World, VFS, and
     * Lua scripting subsystems.
     *
     * @param parentHwnd - Native HWND of the parent window as a JS number.
     *   Pass `0` to create a standalone top-level window.
     *   On Windows, HWNDs fit in 53-bit JS number precision.
     * @param width  - Initial viewport width in pixels. Must be > 0.
     * @param height - Initial viewport height in pixels. Must be > 0.
     * @throws Error if the viewport could not be created (device creation
     *   failed, a viewport is already active, etc.).
     */
    createViewport(parentHwnd: number, width: number, height: number): void;

    /**
     * Destroys the active viewport and all associated GPU resources.
     *
     * Waits for GPU idle before releasing resources. Safe to call when no
     * viewport is active (no-op).
     */
    destroyViewport(): void;

    /**
     * Resizes the viewport swapchain and all dependent render targets.
     *
     * No-op if no viewport is active or if the dimensions match the current
     * size.
     *
     * @param width  - New width in pixels. Must be > 0.
     * @param height - New height in pixels. Must be > 0.
     */
    resizeViewport(width: number, height: number): void;

    /**
     * Repositions and resizes the viewport child window inside its parent.
     *
     * Called when the VSCode panel containing the viewport is moved or
     * resized by the user.
     *
     * @param x      - New X position in parent-client coordinates.
     * @param y      - New Y position in parent-client coordinates.
     * @param width  - New width in pixels. Must be > 0.
     * @param height - New height in pixels. Must be > 0.
     */
    setViewportPosition(x: number, y: number, width: number, height: number): void;

    // ── Rendering ──────────────────────────────────────────────────────────────

    /**
     * Renders one frame and returns per-frame statistics.
     *
     * Drives one iteration of the render loop: fence wait → timing update →
     * Lua tick → SceneRenderer::RenderFrame → Present → fence signal.
     *
     * Must be called from the thread that created the viewport.
     *
     * @returns `FrameStats` for the completed frame. All fields are zero if
     *   no viewport is active.
     */
    renderFrame(): FrameStats;

    /**
     * Starts an asynchronous render thread that fires callbacks on the Node.js
     * event loop via N-API ThreadSafeFunction.
     *
     * The render thread runs at `targetFps` and, on each completed frame:
     *  1. Calls `onFrame` with the per-frame stats.
     *  2. Calls `onEncodedFrame` with the H.264 NAL Buffer produced by the
     *     hardware encoder (one call per encoded output, which may be once
     *     every N frames depending on the encoder pipeline depth).
     *
     * Calling `startRenderLoop` while the loop is already running is a no-op
     * (the existing loop continues at its current rate).
     *
     * @param targetFps      - Desired frames per second (e.g. `60`).
     * @param onFrame        - Callback fired with `FrameStats` each rendered frame.
     * @param onEncodedFrame - Callback fired with a H.264 NAL `Buffer` whenever
     *   the encoder produces output.
     * @throws Error if no viewport is active.
     */
    startRenderLoop(
        targetFps: number,
        onFrame: (stats: FrameStats) => void,
        onEncodedFrame: (nalBuffer: Buffer) => void
    ): void;

    /**
     * Stops the asynchronous render thread started by `startRenderLoop`.
     *
     * Blocks until the render thread has exited and all in-flight callbacks
     * have been flushed. Safe to call when the loop is not running (no-op).
     */
    stopRenderLoop(): void;

    /**
     * Captures the current frame as a PNG image.
     *
     * This is a synchronous (blocking) GPU readback. The returned `Buffer`
     * contains the complete PNG file data in a Node-owned allocation — no
     * manual cleanup is required.
     *
     * @returns PNG data as a `Buffer`, or `null` on failure (no viewport,
     *   GPU error).
     */
    captureScreenshot(): Buffer | null;

    // ── Scene ──────────────────────────────────────────────────────────────────

    /**
     * Loads a `.uzscene` file into the World.
     *
     * Clears the current scene and loads entities from the specified path.
     * The path is resolved through the active VFS (relative to the content
     * root set by `setContentRoot()`).
     *
     * @param path - VFS-relative scene path, e.g. `"scenes/main.uzscene"`.
     * @throws Error if the scene could not be loaded (file not found, parse
     *   error, no active viewport).
     */
    loadScene(path: string): void;

    /**
     * Saves the current scene to a `.uzscene` file on disk.
     *
     * @param path - Output file path (absolute, or relative to the working
     *   directory).
     * @throws Error if the file could not be written.
     */
    saveScene(path: string): void;

    /**
     * Returns a JSON string describing the entity hierarchy.
     *
     * Format:
     * ```json
     * { "entities": [{ "id": 42, "name": "Player", "children": [] }] }
     * ```
     *
     * @returns JSON string, or `null` if no viewport / world is active.
     */
    getSceneTree(): string | null;

    // ── Entity ─────────────────────────────────────────────────────────────────

    /**
     * Creates a new named entity in the World.
     *
     * @param name - Human-readable entity name.
     * @returns Numeric entity ID (slot index > 0).
     * @throws Error if the entity could not be created (no active viewport).
     */
    createEntity(name: string): number;

    /**
     * Destroys an entity and all its components.
     *
     * No-op if the entity does not exist or no viewport is active.
     *
     * @param entityId - Entity slot index returned by `createEntity()`.
     */
    destroyEntity(entityId: number): void;

    /**
     * Returns a JSON string describing a component attached to an entity.
     *
     * @param entityId      - Entity slot index.
     * @param componentType - Component name: `"transform"`, `"mesh"`,
     *   `"light"`, etc.
     * @returns JSON string with component data, or `null` if the entity /
     *   component was not found.
     */
    getComponentJson(entityId: number, componentType: string): string | null;

    /**
     * Sets a single property on a component attached to an entity.
     *
     * @param entityId      - Entity slot index.
     * @param componentType - Component name, e.g. `"transform"`.
     * @param property      - Property name, e.g. `"position"`.
     * @param valueJson     - JSON-encoded value, e.g. `"[1.0, 2.0, 3.0]"`.
     * @returns `true` on success; `false` if the entity, component, or
     *   property was not found, or if the JSON value could not be parsed.
     */
    setComponentProperty(
        entityId: number,
        componentType: string,
        property: string,
        valueJson: string
    ): boolean;

    // ── Input Forwarding ───────────────────────────────────────────────────────

    /**
     * Forwards a mouse move event to the viewport's input system.
     *
     * @param x - Mouse X position in viewport-local pixels.
     * @param y - Mouse Y position in viewport-local pixels.
     */
    onMouseMove(x: number, y: number): void;

    /**
     * Forwards a mouse button press or release event.
     *
     * @param button  - Button index: `0` = Left, `1` = Right, `2` = Middle.
     * @param pressed - `true` if the button was pressed, `false` if released.
     */
    onMouseButton(button: number, pressed: boolean): void;

    /**
     * Forwards a key press or release event.
     *
     * @param keycode - Win32 virtual key code (`VK_*` constant).
     * @param pressed - `true` if the key was pressed, `false` if released.
     */
    onKey(keycode: number, pressed: boolean): void;

    /**
     * Forwards a key event with modifier state from the editor.
     *
     * When the viewport runs inside VSCode, the native HWND does not
     * receive WM_KEYDOWN for modifier keys.  This extended variant accepts
     * a bitmask so the input system reflects the true modifier state.
     *
     * @param keycode   - Win32 virtual key code (`VK_*` constant).
     * @param pressed   - `true` if the key was pressed, `false` if released.
     * @param modifiers - Bitmask: bit 0 = Shift, bit 1 = Ctrl, bit 2 = Alt,
     *   bit 3 = Meta.
     */
    onKeyEx(keycode: number, pressed: boolean, modifiers: number): void;

    /**
     * Forwards a scroll wheel event.
     *
     * @param delta - Scroll amount. Positive = scroll up, negative = scroll down.
     */
    onScroll(delta: number): void;

    // ── H.264 Frame Encoding ────────────────────────────────────────────────────

    /**
     * Encodes the most recently rendered frame as H.264 NAL units.
     *
     * Call after `renderFrame()`. The returned Buffer contains one or more
     * H.264 NAL units suitable for WebCodecs `VideoDecoder`. The buffer is
     * a Node-owned copy — safe to hold across frames.
     *
     * Only available when editor mode is active (`setEditorMode(true)`).
     *
     * @returns H.264 NAL data as a `Buffer`, or `null` if no frame was
     *   produced (encoder buffering) or the encoder is not initialized.
     */
    encodeFrame(): Buffer | null;

    /**
     * Forces the next encoded frame to be an IDR keyframe.
     *
     * Call when the WebCodecs decoder needs a clean reference point
     * (e.g., after a panel is revealed or a decode error occurs).
     */
    requestKeyframe(): void;

    /**
     * Returns the codec extradata (SPS/PPS) needed by WebCodecs.
     *
     * Pass this as the `description` field in `VideoDecoder.configure()`.
     * The data is valid for the encoder's lifetime and does not change
     * unless the viewport is resized.
     *
     * @returns Extradata bytes as a `Buffer`, or `null` if the encoder
     *   is not initialized.
     */
    getCodecExtradata(): Buffer | null;

    /**
     * Updates the H.264 encoder bitrate for adaptive quality.
     *
     * Lower during high motion (faster encoding), higher on static scenes
     * (maximum quality).
     *
     * @param kbps - Target bitrate in kilobits per second.
     */
    setEncodeBitrate(kbps: number): void;

    // ── Diagnostics ────────────────────────────────────────────────────────────

    /**
     * Registers a callback to receive log messages from the engine.
     *
     * The callback is fired asynchronously on the Node.js event loop,
     * regardless of which internal C++ thread produced the message. Only one
     * callback can be active at a time — calling this replaces any previous
     * registration.
     *
     * @param callback - Function invoked with `(level, category, message)`,
     *   or `null` to unregister the current callback.
     */
    setLogCallback(callback: LogCallback | null): void;

    /**
     * Enables or disables editor overlays (grid, stats, gizmos).
     *
     * @param enabled - `true` to show editor overlays; `false` for game-only view.
     */
    setEditorMode(enabled: boolean): void;

    /**
     * Sets the content root path for the VFS.
     *
     * All scene and asset paths resolved by `loadScene()` are relative to
     * this root. Must be called before `loadScene()` when using a
     * non-default content root.
     *
     * @param path - Absolute path to the content directory.
     */
    setContentRoot(path: string): void;
}
