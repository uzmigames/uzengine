/// UzEngine RuntimeClient — IPC proxy for the isolated runtime worker process.
///
/// The RuntimeClient spawns `out/runtime-worker.js` as a child process via
/// `child_process.fork()`. All UzEngineAddon method calls are translated to
/// `WorkerCommand` IPC messages, and replies are deserialized back as Promises.
///
/// Architecture rationale:
///   Loading `uzengine.node` inside the VSCode Extension Host process means a
///   GPU driver crash or SEH exception in the native DX12 code kills VSCode
///   entirely. By isolating the addon in a child process, crashes are contained
///   and the extension can show an error and offer a restart without data loss.
///
/// Lifecycle:
///   1. `new RuntimeClient(log)` — does NOT spawn the worker yet.
///   2. First call to any method → `ensureWorker()` spawns the worker lazily.
///   3. Worker crashes → `onWorkerExit` fires, pending Promises are rejected,
///      state resets to "disconnected". Caller can invoke `restart()`.
///   4. `dispose()` → sends `shutdown` command, then kills the process.
///
/// Frame streaming:
///   `startRenderLoop()` registers `onStats` and `onFrame` callbacks. The
///   worker's native render thread fires these via N-API ThreadSafeFunctions
///   and forwards them as IPC messages. The client receives them and invokes
///   the registered JS callbacks on the Extension Host event loop.
///
/// Log forwarding:
///   The worker sends `{ type: 'log', … }` push messages. The client routes
///   these to the `LogOutputChannel` and also to any `onLog` subscriber
///   registered via `setLogListener()`.

import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';
import type { WorkerCommand, WorkerResponse } from './runtime-worker';
import type { FrameStats } from './uzengine';

// ── Types ─────────────────────────────────────────────────────────────────────

/** Signature for per-frame render statistics delivered from the worker. */
export type StatsCallback = (stats: FrameStats) => void;

/** Signature for encoded H.264 NAL unit frames delivered from the worker. */
/// Frame data is passed as a base64 string (not Buffer) because VSCode's
/// webview.postMessage cannot reliably transfer typed arrays across the
/// extension host ↔ webview boundary.
export type FrameCallback = (nalBase64: string, key: boolean) => void;

export class RuntimeClientError extends Error {
  constructor(
    public readonly code: 'timeout' | 'worker-exit' | 'disposed' | 'backpressure' | 'native',
    message: string,
  ) {
    super(message);
    this.name = 'RuntimeClientError';
  }
}

const kMaxPendingRequests = 64;
const requestBudgets: Partial<Record<WorkerCommand['cmd'], number>> = {
  init: 30_000,
  createViewport: 60_000,
  loadScene: 60_000,
  saveScene: 30_000,
};

/** Signature for log messages forwarded from the worker process. */
export type LogListener = (level: number, category: string, message: string) => void;

// Internal pending-call record.
interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  /** Human-readable label for timeout error messages. */
  label: string;
  timer: ReturnType<typeof setTimeout>;
}

// ── RuntimeClient ─────────────────────────────────────────────────────────────

/**
 * IPC client that proxies all UzEngineAddon calls to an isolated worker process.
 *
 * Methods mirror the `UzEngineAddon` interface but are asynchronous — each
 * returns a `Promise` that resolves when the worker replies.
 *
 * Input forwarding methods (`mouseMove`, `mouseButton`, etc.) are fire-and-forget
 * — they send the command but do NOT await a reply for minimum latency.
 */
export class RuntimeClient implements vscode.Disposable {
  // ── State ─────────────────────────────────────────────────────────────────

  private worker: cp.ChildProcess | undefined;
  private disposed = false;
  private inputBacklogged = false;

  /** Monotonically increasing counter used to generate unique command IDs. */
  private nextId = 1;

  /** Map from command ID → pending Promise resolve/reject callbacks. */
  private readonly pending = new Map<number, PendingCall>();

  /** Registered callbacks for the active render loop. */
  private statsCallback: StatsCallback | undefined;
  private frameCallback: FrameCallback | undefined;

  /** External log subscriber (typically EngineConsole). */
  private logListener: LogListener | undefined;

  /** VSCode logging channel for extension-side diagnostics. */
  private readonly log: vscode.LogOutputChannel;

  /** Whether the worker has been initialised (setEngineDir + init called). */
  private workerInitialised = false;

  /** Remembered engine dir — applied to every new worker after restart. */
  private rememberedEngineDir: string | undefined;

  /** Path to the compiled worker JS, resolved relative to this file's output dir. */
  private readonly workerScriptPath: string;

  // ── Constructor ───────────────────────────────────────────────────────────

  /**
   * @param log  The extension's `LogOutputChannel` for diagnostics.
   *             This is the "UzEngine" channel, NOT the "UzEngine Console"
   *             channel (which is for C++ runtime messages).
   */
  constructor(log: vscode.LogOutputChannel) {
    this.log = log;
    // __dirname in the compiled .js is `out/`.
    // The worker script is compiled to `out/runtime-worker.js`.
    this.workerScriptPath = path.join(__dirname, 'runtime-worker.js');
  }

  // ── Public: worker lifecycle ──────────────────────────────────────────────

  /**
   * Registers a callback that receives log messages forwarded from the worker.
   *
   * Replaces any previously registered listener. Pass `undefined` to remove.
   */
  setLogListener(listener: LogListener | undefined): void {
    this.logListener = listener;
  }

  /**
   * Restarts the worker process after a crash or deliberate shutdown.
   *
   * Any pending Promises from the previous worker are already rejected by the
   * `exit` handler. After `restart()`, new calls will spawn a fresh worker.
   */
  restart(): void {
    this.log.info('[RuntimeClient] restart() called');
    this.killWorker();
    this.workerInitialised = false;
    // ensureWorker() will be invoked on the next API call.
  }

  /** Tears down the worker and all resources. Safe to call multiple times. */
  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.log.info('[RuntimeClient] dispose()');
    // Best-effort graceful shutdown: send the command and immediately kill.
    if (this.worker && this.worker.connected) {
      try {
        const cmd: WorkerCommand = { id: this.nextId++, cmd: 'shutdown' };
        this.worker.send(cmd);
      } catch {
        // Ignore send errors during teardown.
      }
    }
    this.killWorker();
  }

  // ── Public: UzEngineAddon-compatible async API ────────────────────────────

  /** Sets the engine resource directory before calling `init()`. */
  async setEngineDir(dirPath: string): Promise<void> {
    this.rememberedEngineDir = dirPath;
    await this.send({ id: 0, cmd: 'setEngineDir', path: dirPath }, 'setEngineDir');
  }

  /** Returns the runtime version string, e.g. `"0.7.0"`. */
  async getVersion(): Promise<string> {
    return (await this.send({ id: 0, cmd: 'getVersion' }, 'getVersion')) as string;
  }

  /** Returns the active rendering backend identifier. */
  async getBackend(): Promise<string> {
    return (await this.send({ id: 0, cmd: 'getBackend' }, 'getBackend')) as string;
  }

  /**
   * Initialises the UzEngine runtime inside the worker.
   *
   * Also wires up the log callback inside the worker so C++ log messages are
   * forwarded to the extension via IPC.
   */
  async init(): Promise<void> {
    // Re-apply setEngineDir on every init (handles worker restart).
    if (this.rememberedEngineDir) {
      await this.send(
        { id: 0, cmd: 'setEngineDir', path: this.rememberedEngineDir },
        'setEngineDir',
      );
    }
    await this.send({ id: 0, cmd: 'init' }, 'init');
  }

  /** Shuts down the runtime and exits the worker process. */
  async shutdown(): Promise<void> {
    await this.send({ id: 0, cmd: 'shutdown' }, 'shutdown');
  }

  /** Creates the DX12 viewport in the worker. */
  async createViewport(parentHwnd: number, width: number, height: number): Promise<void> {
    await this.send(
      { id: 0, cmd: 'createViewport', parent: parentHwnd, width, height },
      'createViewport',
    );
  }

  /** Destroys the active viewport and all GPU resources. */
  async destroyViewport(): Promise<void> {
    await this.send({ id: 0, cmd: 'destroyViewport' }, 'destroyViewport');
  }

  /** Resizes the viewport swapchain and render targets. */
  async resizeViewport(width: number, height: number): Promise<void> {
    await this.send({ id: 0, cmd: 'resizeViewport', width, height }, 'resizeViewport');
  }

  /** Repositions and resizes the viewport child window inside its parent. */
  async setViewportPosition(x: number, y: number, width: number, height: number): Promise<void> {
    await this.send(
      { id: 0, cmd: 'setViewportPosition', x, y, width, height },
      'setViewportPosition',
    );
  }

  /**
   * Starts the native render loop in the worker.
   *
   * Registers `onStats` and `onFrame` callbacks. These are invoked whenever
   * the worker pushes `stats` or `frame` IPC messages.
   *
   * @param fps      Target frames per second.
   * @param onStats  Callback fired with per-frame statistics each rendered frame.
   * @param onFrame  Callback fired with the H.264 NAL `Buffer` for each encoded frame.
   */
  async startRenderLoop(
    fps: number,
    onStats: StatsCallback,
    onFrame: FrameCallback,
  ): Promise<void> {
    this.statsCallback = onStats;
    this.frameCallback = onFrame;
    await this.send({ id: 0, cmd: 'startRenderLoop', fps }, 'startRenderLoop');
  }

  /** Stops the render loop. */
  async stopRenderLoop(): Promise<void> {
    this.statsCallback = undefined;
    this.frameCallback = undefined;
    await this.send({ id: 0, cmd: 'stopRenderLoop' }, 'stopRenderLoop');
  }

  /** Forces the next encoded frame to be an IDR keyframe. */
  async requestKeyframe(): Promise<void> {
    await this.send({ id: 0, cmd: 'requestKeyframe' }, 'requestKeyframe');
  }

  /**
   * Returns the codec extradata (SPS/PPS) needed by `VideoDecoder.configure()`.
   *
   * @returns `Buffer` with the extradata bytes, or `null` if the encoder is not
   *   initialised.
   */
  async getCodecExtradata(): Promise<Buffer | null> {
    const b64 = (await this.send({ id: 0, cmd: 'getCodecExtradata' }, 'getCodecExtradata')) as
      | string
      | null;
    if (!b64) return null;
    return Buffer.from(b64, 'base64');
  }

  /** Updates the H.264 encoder target bitrate. */
  async setEncodeBitrate(kbps: number): Promise<void> {
    await this.send({ id: 0, cmd: 'setEncodeBitrate', kbps }, 'setEncodeBitrate');
  }

  /** Enables or disables editor overlays. */
  async setEditorMode(enabled: boolean): Promise<void> {
    await this.send({ id: 0, cmd: 'setEditorMode', enabled }, 'setEditorMode');
  }

  /** Sets the VFS content root path. */
  async setContentRoot(contentPath: string): Promise<void> {
    await this.send({ id: 0, cmd: 'setContentRoot', path: contentPath }, 'setContentRoot');
  }

  /** Loads a `.uzscene` file into the World. */
  async loadScene(scenePath: string): Promise<void> {
    await this.send({ id: 0, cmd: 'loadScene', path: scenePath }, 'loadScene');
  }

  /** Saves the current scene to disk. */
  async saveScene(scenePath: string): Promise<void> {
    await this.send({ id: 0, cmd: 'saveScene', path: scenePath }, 'saveScene');
  }

  /**
   * Returns the JSON entity hierarchy string, or `null` if no scene is loaded.
   */
  async getSceneTree(): Promise<string | null> {
    return (await this.send({ id: 0, cmd: 'getSceneTree' }, 'getSceneTree')) as string | null;
  }

  /** Creates a new entity and returns its numeric ID. */
  async createEntity(name: string): Promise<number> {
    return (await this.send({ id: 0, cmd: 'createEntity', name }, 'createEntity')) as number;
  }

  /** Destroys an entity. */
  async destroyEntity(entityId: number): Promise<void> {
    await this.send({ id: 0, cmd: 'destroyEntity', entityId }, 'destroyEntity');
  }

  /** Returns JSON describing a component on an entity, or `null`. */
  async getComponentJson(entityId: number, componentType: string): Promise<string | null> {
    return (await this.send(
      { id: 0, cmd: 'getComponentJson', entityId, componentType },
      'getComponentJson',
    )) as string | null;
  }

  /** Sets a single property on a component. Returns `true` on success. */
  async setComponentProperty(
    entityId: number,
    componentType: string,
    property: string,
    valueJson: string,
  ): Promise<boolean> {
    return (await this.send(
      { id: 0, cmd: 'setComponentProperty', entityId, componentType, property, valueJson },
      'setComponentProperty',
    )) as boolean;
  }

  /**
   * Returns a JSON string listing all component types attached to an entity.
   *
   * @returns JSON array string, or `null` if the entity was not found.
   */
  async getEntityComponents(entityId: number): Promise<string | null> {
    return (await this.send(
      { id: 0, cmd: 'getEntityComponents', entityId },
      'getEntityComponents',
    )) as string | null;
  }

  /**
   * Sets the editor selection to the specified entity.
   * Pass `0` to deselect.
   */
  async selectEntity(entityId: number): Promise<void> {
    await this.send({ id: 0, cmd: 'selectEntity', entityId }, 'selectEntity');
  }

  /** Returns the currently selected entity ID, or `0` if nothing is selected. */
  async getSelectedEntity(): Promise<number> {
    return (await this.send({ id: 0, cmd: 'getSelectedEntity' }, 'getSelectedEntity')) as number;
  }

  /** Enters play mode. Returns `true` on success. */
  async play(): Promise<boolean> {
    return (await this.send({ id: 0, cmd: 'play' }, 'play')) as boolean;
  }

  /** Exits play mode. Returns `true` on success. */
  async stop(): Promise<boolean> {
    return (await this.send({ id: 0, cmd: 'stop' }, 'stop')) as boolean;
  }

  /** Returns whether the engine is currently in play mode. */
  async isPlaying(): Promise<boolean> {
    return (await this.send({ id: 0, cmd: 'isPlaying' }, 'isPlaying')) as boolean;
  }

  /**
   * Captures the current frame as PNG.
   *
   * @returns `Buffer` with PNG data, or `null` on failure.
   */
  async captureScreenshot(): Promise<Buffer | null> {
    const b64 = (await this.send({ id: 0, cmd: 'captureScreenshot' }, 'captureScreenshot')) as
      | string
      | null;
    if (!b64) return null;
    return Buffer.from(b64, 'base64');
  }

  // ── Public: fire-and-forget input forwarding ──────────────────────────────
  // These methods are called at high frequency (every mouse move, key event).
  // Using Promises would create unnecessary GC pressure. We send the command
  // without registering a pending entry — the worker replies are ignored.

  /** Forwards a mouse move event (fire-and-forget). */
  mouseMove(x: number, y: number): void {
    this.fireAndForget({ id: 0, cmd: 'mouseMove', x, y });
  }

  /** Forwards a mouse button event (fire-and-forget). */
  mouseButton(button: number, pressed: boolean): void {
    this.fireAndForget({ id: 0, cmd: 'mouseButton', button, pressed });
  }

  /** Forwards a key event (fire-and-forget). */
  key(keycode: number, pressed: boolean): void {
    this.fireAndForget({ id: 0, cmd: 'key', keycode, pressed });
  }

  /** Forwards a key event with modifier state (fire-and-forget). */
  keyEx(keycode: number, pressed: boolean, modifiers: number): void {
    this.fireAndForget({ id: 0, cmd: 'keyEx', keycode, pressed, modifiers });
  }

  /** Forwards a scroll event (fire-and-forget). */
  scroll(delta: number): void {
    this.fireAndForget({ id: 0, cmd: 'scroll', delta });
  }

  // ── Private: worker management ────────────────────────────────────────────

  /**
   * Lazily spawns the worker process on the first call.
   *
   * Idempotent — if the worker is already running, returns immediately.
   */
  private ensureWorker(): void {
    if (this.worker && !this.worker.killed) return;

    this.log.info(`[RuntimeClient] Spawning worker: ${this.workerScriptPath}`);
    this.workerInitialised = false;

    this.worker = cp.fork(this.workerScriptPath, [], {
      // Inherit stdio so worker's stderr appears in the VSCode Debug Console.
      silent: false,
      // Detach = false: the worker is tightly coupled to the extension host.
      detached: false,
    });
    const worker = this.worker;

    // ── Incoming message handler ──────────────────────────────────────────
    worker.on('message', (raw: WorkerResponse) => {
      if (this.worker !== worker) return;
      this.handleWorkerMessage(raw);
    });

    // ── Worker exit / crash handling ──────────────────────────────────────
    worker.on('exit', (code, signal) => {
      // kill() is asynchronous: an old process may exit after its replacement starts.
      if (this.worker !== worker) return;
      const reason = signal
        ? `killed by signal ${signal}`
        : `exited with code ${code ?? 'unknown'}`;
      this.log.warn(`[RuntimeClient] Worker ${reason}`);

      // Reject all pending Promises so callers don't hang.
      const err = new RuntimeClientError('worker-exit', `UzEngine worker ${reason}`);
      for (const [, pending] of this.pending) {
        clearTimeout(pending.timer);
        pending.reject(err);
      }
      this.pending.clear();
      this.statsCallback = undefined;
      this.frameCallback = undefined;
      this.worker = undefined;
      this.workerInitialised = false;
      this.inputBacklogged = false;

      if (!this.disposed && code !== 0) {
        void vscode.window
          .showErrorMessage(
            `UzEngine runtime worker crashed (${reason}). ` +
              'Open a scene or use "UzEngine: Start" to restart.',
            'Restart',
          )
          .then((choice) => {
            if (choice === 'Restart' && !this.worker && !this.disposed) {
              this.restart();
            }
          });
      }
    });

    worker.on('error', (err) => {
      if (this.worker !== worker) return;
      this.log.error(`[RuntimeClient] Worker process error: ${err.message}`);
      // A failed fork can emit error/close without exit. Settle its calls now.
      this.killWorker(new RuntimeClientError('worker-exit', err.message));
    });
  }

  /**
   * Kills the worker process immediately without a graceful shutdown.
   *
   * All pending Promises are rejected. Safe to call when the worker is
   * already dead.
   */
  private killWorker(
    reason = new RuntimeClientError(
      this.disposed ? 'disposed' : 'worker-exit',
      'RuntimeClient disposed',
    ),
  ): void {
    const worker = this.worker;
    this.worker = undefined;
    if (worker) {
      if (!worker.killed) {
        worker.kill('SIGTERM');
      }
    }
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(reason);
    }
    this.pending.clear();
    this.statsCallback = undefined;
    this.frameCallback = undefined;
    this.inputBacklogged = false;
  }

  // ── Private: IPC messaging ────────────────────────────────────────────────

  /**
   * Sends a command to the worker and returns a Promise that resolves with
   * the reply's `result` field, or rejects with an `Error` on failure.
   *
   * The `cmd` parameter is a `WorkerCommand` with `id: 0` as a placeholder;
   * the actual ID is assigned here before the message is sent. Using the full
   * command type (rather than `Omit<WorkerCommand, 'id'>`) avoids TypeScript's
   * inability to narrow object literals against distributed union types.
   *
   * @param cmd   Complete command (pass `id: 0` — overwritten by this method).
   * @param label Human-readable name for error messages.
   */
  private send(cmd: WorkerCommand, label: string): Promise<unknown> {
    if (this.disposed) {
      return Promise.reject(new RuntimeClientError('disposed', 'RuntimeClient disposed'));
    }
    if (this.pending.size >= kMaxPendingRequests) {
      return Promise.reject(
        new RuntimeClientError('backpressure', `RuntimeClient: too many pending requests (cmd=${label})`),
      );
    }
    this.ensureWorker();

    const worker = this.worker;
    if (!worker || !worker.connected) {
      return Promise.reject(
        new RuntimeClientError('worker-exit', `RuntimeClient: worker not connected (cmd=${label})`),
      );
    }

    const id = this.nextId++;
    // Overwrite the placeholder id with the real one.
    (cmd as { id: number }).id = id;

    return new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new RuntimeClientError('timeout', `RuntimeClient: request timed out (cmd=${label})`));
      }, requestBudgets[cmd.cmd] ?? 10_000);
      this.pending.set(id, { resolve, reject, label, timer });
      try {
        worker.send(cmd, (error) => {
          if (!error || !this.pending.delete(id)) return;
          clearTimeout(timer);
          reject(new RuntimeClientError('worker-exit', error.message));
        });
      } catch (e: unknown) {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(new RuntimeClientError('worker-exit', e instanceof Error ? e.message : String(e)));
      }
    });
  }

  /**
   * Sends a command without awaiting a reply (for high-frequency input events).
   *
   * Pass `id: 0` — it is replaced internally before sending.
   */
  private fireAndForget(cmd: WorkerCommand): void {
    if (!this.worker || !this.worker.connected || this.inputBacklogged) return;
    const worker = this.worker;
    (cmd as { id: number }).id = this.nextId++;
    try {
      let completed = false;
      const accepted = worker.send(cmd, () => {
        completed = true;
        if (this.worker === worker) this.inputBacklogged = false;
      });
      if (!accepted && !completed) this.inputBacklogged = true;
    } catch {
      // Fire-and-forget: silently swallow send errors.
    }
  }

  // ── Private: message dispatch ─────────────────────────────────────────────

  private handleWorkerMessage(msg: WorkerResponse): void {
    // Discriminate by the shape of the message.
    if ('id' in msg) {
      // Reply to a pending command.
      const pending = this.pending.get(msg.id);
      if (!pending) {
        // Late reply for a command that timed out or was cancelled.
        return;
      }
      this.pending.delete(msg.id);
      clearTimeout(pending.timer);
      if (msg.ok) {
        pending.resolve(msg.result);
      } else {
        pending.reject(new RuntimeClientError('native', msg.error));
      }
    } else if (msg.type === 'log') {
      // Log message from the C++ runtime — route to the console panel.
      if (this.logListener) {
        this.logListener(msg.level, msg.category, msg.message);
      }
      // Also mirror high-severity messages to the extension log channel.
      if (msg.level >= 4) {
        // Error or Fatal
        this.log.error(`[Worker][${msg.category}] ${msg.message}`);
      }
    } else if (msg.type === 'stats') {
      // Per-frame statistics pushed by the render thread.
      if (this.statsCallback) {
        this.statsCallback({
          fps: msg.fps,
          frameMs: msg.frameMs,
          drawCalls: msg.drawCalls,
          vertices: msg.vertices,
          triangles: msg.triangles,
          frameIndex: 0, // Not tracked at IPC level; worker doesn't send it.
        });
      }
    } else if (msg.type === 'frame') {
      // Pass base64 string directly to the callback. The webview will
      // decode it with atob(). This avoids Uint8Array/Buffer serialization
      // issues in VSCode's webview.postMessage (typed arrays don't survive
      // the extension host → webview boundary reliably).
      if (this.frameCallback) {
        this.frameCallback(msg.data, msg.key);
      }
    }
  }
}
