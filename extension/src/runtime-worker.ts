/// UzEngine runtime worker process.
///
/// This script runs as a child process spawned by `RuntimeClient` via
/// `child_process.fork()`. It loads the `uzengine.node` N-API addon in
/// isolation so that any crash or hang in the native DX12 rendering code
/// kills only this worker — the VSCode Extension Host process survives.
///
/// Communication protocol:
///   Parent → Worker : `WorkerCommand` objects via `process.send()`
///   Worker → Parent : `WorkerResponse` objects via `process.send()`
///
/// Render loop push model:
///   When `startRenderLoop` is executed, the native addon fires two N-API
///   ThreadSafeFunctions that marshal callbacks to the Node.js event loop
///   inside this worker. Each callback calls `process.send()` with a stats
///   or frame message. The parent RuntimeClient receives these and forwards
///   them to the webview.
///
/// Frame serialisation:
///   IPC serialises to JSON, so `Buffer` cannot be transferred directly.
///   NAL unit bytes are sent as a base64 string and reconstructed on the
///   parent side via `Buffer.from(data, 'base64')`.

import * as path from 'node:path';
import * as fs from 'node:fs';
import * as os from 'node:os';
import { FrameGate } from './frame-gate.js';
import { isAnnexBKeyframe } from './nal-utils.js';

// ── IPC message types ─────────────────────────────────────────────────────────

/**
 * Commands sent from the parent (RuntimeClient) to this worker.
 * Each command carries a numeric `id` used to correlate the response.
 */
export type WorkerCommand =
  | { id: number; cmd: 'init' }
  | { id: number; cmd: 'shutdown' }
  | { id: number; cmd: 'setEngineDir'; path: string }
  | { id: number; cmd: 'createViewport'; parent: number; width: number; height: number }
  | { id: number; cmd: 'destroyViewport' }
  | { id: number; cmd: 'setEditorMode'; enabled: boolean }
  | { id: number; cmd: 'setContentRoot'; path: string }
  | { id: number; cmd: 'loadScene'; path: string }
  | { id: number; cmd: 'saveScene'; path: string }
  | { id: number; cmd: 'resizeViewport'; width: number; height: number }
  | { id: number; cmd: 'setViewportPosition'; x: number; y: number; width: number; height: number }
  | { id: number; cmd: 'startRenderLoop'; fps: number }
  | { id: number; cmd: 'stopRenderLoop' }
  | { id: number; cmd: 'requestKeyframe' }
  | { id: number; cmd: 'getCodecExtradata' }
  | { id: number; cmd: 'setEncodeBitrate'; kbps: number }
  | { id: number; cmd: 'getVersion' }
  | { id: number; cmd: 'getBackend' }
  | { id: number; cmd: 'getSceneTree' }
  | { id: number; cmd: 'createEntity'; name: string }
  | { id: number; cmd: 'destroyEntity'; entityId: number }
  | { id: number; cmd: 'getComponentJson'; entityId: number; componentType: string }
  | {
      id: number;
      cmd: 'setComponentProperty';
      entityId: number;
      componentType: string;
      property: string;
      valueJson: string;
    }
  | { id: number; cmd: 'captureScreenshot' }
  | { id: number; cmd: 'mouseMove'; x: number; y: number }
  | { id: number; cmd: 'mouseButton'; button: number; pressed: boolean }
  | { id: number; cmd: 'key'; keycode: number; pressed: boolean }
  | { id: number; cmd: 'keyEx'; keycode: number; pressed: boolean; modifiers: number }
  | { id: number; cmd: 'scroll'; delta: number }
  | { id: number; cmd: 'getEntityComponents'; entityId: number }
  | { id: number; cmd: 'selectEntity'; entityId: number }
  | { id: number; cmd: 'getSelectedEntity' }
  | { id: number; cmd: 'play' }
  | { id: number; cmd: 'stop' }
  | { id: number; cmd: 'isPlaying' };

/**
 * Responses sent from this worker to the parent (RuntimeClient).
 *
 * Two categories:
 *  - **Reply** messages (`id` present): correlate to a WorkerCommand `id`.
 *  - **Push** messages (`type` present, no `id`): unsolicited events (logs,
 *    frame data, render stats).
 */
export type WorkerResponse =
  | { id: number; ok: true; result?: unknown }
  | { id: number; ok: false; error: string }
  | { type: 'log'; level: number; category: string; message: string }
  | {
      type: 'stats';
      fps: number;
      frameMs: number;
      drawCalls: number;
      vertices: number;
      triangles: number;
    }
  | { type: 'frame'; data: string; key: boolean }; // base64-encoded H.264 NAL bytes

// ── Worker entry-point guard ──────────────────────────────────────────────────
// This file is both imported as a module (for its types) and executed directly
// as the forked child. Only run the IPC loop when executed as the main module.

// Node.js sets `require.main === module` in CommonJS when the file is run directly.
// In the compiled JS (CommonJS), this check identifies the entry-point execution.
if (require.main === module) {
  runWorker();
}

// ── Worker implementation ─────────────────────────────────────────────────────

type WorkerHost = Pick<
  NodeJS.Process,
  'env' | 'platform' | 'pid' | 'version' | 'arch' | 'send' | 'on' | 'exit'
>;
type UzAddon = import('./uzengine').UzEngineAddon;

/// Host and native loader boundaries are injectable for IPC contract tests.
export function runWorker(
  host: WorkerHost = process,
  loadAddon: (addonPath: string) => UzAddon = (addonPath) => {
    // Native .node addons use the CommonJS loader.
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require(addonPath) as UzAddon;
  },
): void {
  // Crash-proof log file — centralized in %LOCALAPPDATA%/UzEngine/logs/.
  // Falls back to native/worker.log if the directory can't be created.
  let logDir = path.join(__dirname, '..', 'native'); // fallback
  try {
    const localAppData =
      host.env['LOCALAPPDATA'] ??
      (host.platform === 'win32' ? '' : path.join(os.homedir(), '.local', 'share'));
    if (localAppData) {
      const uzLogDir = path.join(localAppData, 'UzEngine', 'logs');
      fs.mkdirSync(uzLogDir, { recursive: true });
      logDir = uzLogDir;
    }
  } catch {
    /* use fallback */
  }
  // Timestamped filename so each session gets its own file.
  const now = new Date();
  const ts0 =
    now.getFullYear().toString() +
    '-' +
    String(now.getMonth() + 1).padStart(2, '0') +
    '-' +
    String(now.getDate()).padStart(2, '0') +
    '_' +
    String(now.getHours()).padStart(2, '0') +
    '-' +
    String(now.getMinutes()).padStart(2, '0') +
    '-' +
    String(now.getSeconds()).padStart(2, '0');
  const logPath = path.join(logDir, `worker_${ts0}.log`);
  function wlog(msg: string): void {
    const ts = new Date().toISOString();
    try {
      fs.appendFileSync(logPath, `[${ts}] ${msg}\n`);
    } catch {
      /* disk full? */
    }
  }

  wlog('=== Worker starting ===');
  wlog(`pid=${host.pid} node=${host.version} arch=${host.arch} platform=${host.platform}`);
  wlog(`__dirname=${__dirname}`);

  // The worker is compiled to out/runtime-worker.js.
  // The native addon lives at out/../native/uzengine.node (i.e. native/).
  // __dirname inside the compiled .js is the `out/` directory.
  const nativeDir = path.join(__dirname, '..', 'native');
  const addonSrc = path.join(nativeDir, 'uzengine.node');
  const dllSrc = path.join(nativeDir, 'uzengine_runtime.dll');

  // Copy uzengine.node + uzengine_runtime.dll to a temp directory so the
  // originals are NEVER locked by the Windows loader. This allows rebuilding
  // and deploying the DLL while VSCode is running.
  // Other DLLs (dxcompiler, ffmpeg, etc.) are found via PATH prepend.
  const tmpDir = path.join(os.tmpdir(), `uzengine-worker-${host.pid}`);
  let addonPath = addonSrc; // fallback if copy fails
  try {
    fs.mkdirSync(tmpDir, { recursive: true });
    const tmpAddon = path.join(tmpDir, 'uzengine.node');
    const tmpDll = path.join(tmpDir, 'uzengine_runtime.dll');
    fs.copyFileSync(addonSrc, tmpAddon);
    fs.copyFileSync(dllSrc, tmpDll);
    // Prepend native dir to PATH so dependent DLLs (dxcompiler, ffmpeg, etc.)
    // are found by the Windows loader when loading from the temp dir.
    host.env['PATH'] = nativeDir + path.delimiter + (host.env['PATH'] ?? '');
    addonPath = tmpAddon;
    wlog(`Copied addon to temp: ${tmpDir}`);
  } catch (copyErr: unknown) {
    wlog(
      `WARN: temp copy failed (${copyErr instanceof Error ? copyErr.message : String(copyErr)}), loading from native/ directly`,
    );
  }

  wlog(`addonPath=${addonPath}`);

  // Clean up temp dir on exit (best-effort).
  host.on('exit', () => {
    try {
      fs.rmSync(tmpDir, { recursive: true, force: true });
    } catch {
      /* ignore */
    }
  });

  let addon: UzAddon | undefined;
  let addonLoadError: string | undefined;
  const frameGate = new FrameGate();
  let framesInFlight = 0;
  let statsInFlight = 0;
  let frameChannelBacklogged = false;

  try {
    wlog('require(uzengine.node) ...');
    addon = loadAddon(addonPath);
    wlog('require(uzengine.node) OK');
  } catch (e: unknown) {
    addonLoadError = e instanceof Error ? e.message : String(e);
    wlog(`require(uzengine.node) FAILED: ${addonLoadError}`);
    // Don't exit — respond to commands with errors so the parent knows what failed.
  }

  // ── Helpers ───────────────────────────────────────────────────────────────

  /** Send a reply to a command. */
  function reply(id: number, result?: unknown): void {
    const msg: WorkerResponse = { id, ok: true, result };
    host.send!(msg);
  }

  /** Send an error reply to a command. */
  function replyError(id: number, err: unknown): void {
    const msg: WorkerResponse = {
      id,
      ok: false,
      error: err instanceof Error ? err.message : String(err),
    };
    host.send!(msg);
  }

  /** Assert the addon is loaded; throws a descriptive error if not. */
  function requireAddon(): UzAddon {
    if (!addon) {
      throw new Error(`UzEngine native addon not loaded: ${addonLoadError ?? 'unknown error'}`);
    }
    return addon;
  }

  // ── Log callback — wired once after the addon is confirmed alive ──────────

  function wireLogCallback(): void {
    if (!addon) return;
    let cppLogCount = 0;
    addon.setLogCallback((level: number, category: string, message: string) => {
      // Mirror first 50 C++ log messages to worker.log for crash diagnosis.
      cppLogCount++;
      if (cppLogCount <= 50 || level >= 3) {
        const levels = ['TRACE', 'DEBUG', 'INFO', 'WARN', 'ERROR', 'FATAL'];
        wlog(`[C++] [${levels[level] ?? level}] [${category}] ${message}`);
      }

      const msg: WorkerResponse = { type: 'log', level, category, message };
      if (host.send) {
        host.send(msg);
      }
    });
  }

  // ── IPC message handler ───────────────────────────────────────────────────

  host.on('message', (msg: WorkerCommand) => {
    const { id, cmd } = msg;
    wlog(`IPC cmd=${cmd} id=${id}`);
    try {
      switch (cmd) {
        // ── Lifecycle ─────────────────────────────────────────────────
        case 'setEngineDir': {
          wlog(`setEngineDir: ${msg.path}`);
          requireAddon().setEngineDir(msg.path);
          wlog('setEngineDir OK');
          reply(id);
          break;
        }
        case 'getVersion': {
          const version = requireAddon().getVersion();
          wlog(`getVersion: ${version}`);
          reply(id, version);
          break;
        }
        case 'getBackend': {
          const backend = requireAddon().getBackend();
          wlog(`getBackend: ${backend}`);
          reply(id, backend);
          break;
        }
        case 'init': {
          wlog('init...');
          requireAddon().init();
          wlog('init OK, wiring log callback');
          wireLogCallback();
          reply(id);
          break;
        }
        case 'shutdown': {
          if (addon) {
            try {
              addon.stopRenderLoop();
            } catch {
              /* ignore */
            }
            // Unregister the log callback first so the addon stops
            // calling back into the JS function after it is freed.
            try {
              addon.setLogCallback(null);
            } catch {
              /* ignore */
            }
            try {
              addon.destroyViewport();
            } catch {
              /* ignore */
            }
            try {
              addon.shutdown();
            } catch {
              /* ignore */
            }
          }
          reply(id);
          // Give the IPC a tick to flush the reply before exiting.
          setImmediate(() => host.exit(0));
          break;
        }

        // ── Viewport ──────────────────────────────────────────────────
        case 'createViewport': {
          wlog(`createViewport: parent=${msg.parent} ${msg.width}x${msg.height}`);
          requireAddon().createViewport(msg.parent, msg.width, msg.height);
          wlog('createViewport OK');
          reply(id);
          break;
        }
        case 'destroyViewport': {
          requireAddon().destroyViewport();
          reply(id);
          break;
        }
        case 'resizeViewport': {
          requireAddon().resizeViewport(msg.width, msg.height);
          reply(id);
          break;
        }
        case 'setViewportPosition': {
          requireAddon().setViewportPosition(msg.x, msg.y, msg.width, msg.height);
          reply(id);
          break;
        }

        // ── Render loop ───────────────────────────────────────────────
        case 'startRenderLoop': {
          wlog(`startRenderLoop: fps=${msg.fps}`);
          frameGate.arm();
          let statsCount = 0;
          let frameCount = 0;
          requireAddon().startRenderLoop(
            msg.fps,
            (stats) => {
              if (!host.send || statsInFlight >= 4) return;
              statsCount++;
              if (statsCount <= 3 || statsCount % 120 === 0) {
                wlog(
                  `stats #${statsCount}: fps=${stats.fps.toFixed(0)} drawCalls=${stats.drawCalls} tris=${stats.triangles}`,
                );
              }
              const statsMsg: WorkerResponse = {
                type: 'stats',
                fps: stats.fps,
                frameMs: stats.frameMs,
                drawCalls: stats.drawCalls,
                vertices: stats.vertices,
                triangles: stats.triangles,
              };
              statsInFlight++;
              host.send(statsMsg, undefined, undefined, () => statsInFlight--);
            },
            (nalBuffer: Buffer) => {
              if (!host.send) return;
              const key = isAnnexBKeyframe(nalBuffer);
              if (framesInFlight >= 2 || frameChannelBacklogged) {
                frameGate.onDropped();
                requireAddon().requestKeyframe();
                return;
              }
              if (!frameGate.shouldDeliver(key)) return;
              frameCount++;
              if (frameCount <= 3 || frameCount % 120 === 0) {
                wlog(`frame #${frameCount}: ${nalBuffer.byteLength} bytes`);
              }
              const frameMsg: WorkerResponse = {
                type: 'frame',
                data: nalBuffer.toString('base64'),
                key,
              };
              framesInFlight++;
              let completed = false;
              const accepted = host.send(frameMsg, undefined, undefined, (error) => {
                completed = true;
                framesInFlight--;
                frameChannelBacklogged = false;
                if (error) {
                  frameGate.onDropped();
                  requireAddon().requestKeyframe();
                }
              });
              if (!accepted && !completed) frameChannelBacklogged = true;
            },
          );
          wlog('startRenderLoop OK');
          reply(id);
          break;
        }
        case 'stopRenderLoop': {
          requireAddon().stopRenderLoop();
          reply(id);
          break;
        }
        case 'requestKeyframe': {
          requireAddon().requestKeyframe();
          reply(id);
          break;
        }
        case 'getCodecExtradata': {
          const extradata = requireAddon().getCodecExtradata();
          const result = extradata ? extradata.toString('base64') : null;
          wlog(`getCodecExtradata: ${extradata ? extradata.byteLength + ' bytes' : 'null'}`);
          reply(id, result);
          break;
        }
        case 'setEncodeBitrate': {
          requireAddon().setEncodeBitrate(msg.kbps);
          reply(id);
          break;
        }

        // ── Editor helpers ────────────────────────────────────────────
        case 'setEditorMode': {
          wlog(`setEditorMode: ${msg.enabled}`);
          requireAddon().setEditorMode(msg.enabled);
          wlog('setEditorMode OK');
          reply(id);
          break;
        }
        case 'setContentRoot': {
          wlog(`setContentRoot: ${msg.path}`);
          requireAddon().setContentRoot(msg.path);
          wlog('setContentRoot OK');
          reply(id);
          break;
        }

        // ── Scene ─────────────────────────────────────────────────────
        case 'loadScene': {
          wlog(`loadScene: ${msg.path}`);
          requireAddon().loadScene(msg.path);
          wlog('loadScene OK');
          reply(id);
          break;
        }
        case 'saveScene': {
          requireAddon().saveScene(msg.path);
          reply(id);
          break;
        }
        case 'getSceneTree': {
          const tree = requireAddon().getSceneTree();
          reply(id, tree);
          break;
        }

        // ── Entity ────────────────────────────────────────────────────
        case 'createEntity': {
          const entityId = requireAddon().createEntity(msg.name);
          reply(id, entityId);
          break;
        }
        case 'destroyEntity': {
          requireAddon().destroyEntity(msg.entityId);
          reply(id);
          break;
        }
        case 'getComponentJson': {
          const json = requireAddon().getComponentJson(msg.entityId, msg.componentType);
          reply(id, json);
          break;
        }
        case 'setComponentProperty': {
          const ok = requireAddon().setComponentProperty(
            msg.entityId,
            msg.componentType,
            msg.property,
            msg.valueJson,
          );
          reply(id, ok);
          break;
        }
        case 'captureScreenshot': {
          const buf = requireAddon().captureScreenshot();
          const result = buf ? buf.toString('base64') : null;
          reply(id, result);
          break;
        }

        // ── Input forwarding ──────────────────────────────────────────
        case 'mouseMove': {
          requireAddon().onMouseMove(msg.x, msg.y);
          reply(id);
          break;
        }
        case 'mouseButton': {
          requireAddon().onMouseButton(msg.button, msg.pressed);
          reply(id);
          break;
        }
        case 'key': {
          requireAddon().onKey(msg.keycode, msg.pressed);
          reply(id);
          break;
        }
        case 'keyEx': {
          requireAddon().onKeyEx(msg.keycode, msg.pressed, msg.modifiers);
          reply(id);
          break;
        }
        case 'scroll': {
          requireAddon().onScroll(msg.delta);
          reply(id);
          break;
        }

        // ── Entity introspection + selection ─────────────────────────
        case 'getEntityComponents': {
          const json = requireAddon().getEntityComponents(msg.entityId);
          reply(id, json);
          break;
        }
        case 'selectEntity': {
          requireAddon().selectEntity(msg.entityId);
          reply(id);
          break;
        }
        case 'getSelectedEntity': {
          const entityId = requireAddon().getSelectedEntity();
          reply(id, entityId);
          break;
        }

        // ── Play mode ────────────────────────────────────────────────
        case 'play': {
          const ok = requireAddon().play();
          reply(id, ok);
          break;
        }
        case 'stop': {
          const ok = requireAddon().stop();
          reply(id, ok);
          break;
        }
        case 'isPlaying': {
          const playing = requireAddon().isPlaying();
          reply(id, playing);
          break;
        }

        default: {
          // Exhaustiveness guard — TypeScript should catch unreachable
          // branches at compile time, but handle gracefully at runtime.
          replyError(id, `Unknown command: ${(msg as WorkerCommand).cmd}`);
          break;
        }
      }
    } catch (e: unknown) {
      const errMsg = e instanceof Error ? e.message : String(e);
      wlog(`ERROR cmd=${cmd}: ${errMsg}`);
      replyError(id, e);
    }
  });

  // ── Unhandled error handling ───────────────────────────────────────────────
  // Forward uncaught exceptions to the parent before the process dies so the
  // parent can show a meaningful error message.

  host.on('uncaughtException', (err: Error) => {
    wlog(`FATAL uncaughtException: ${err.message}\n${err.stack ?? ''}`);
    if (host.send) {
      const msg: WorkerResponse = {
        type: 'log',
        level: 5, // Fatal
        category: 'Worker',
        message: `Uncaught exception: ${err.message}\n${err.stack ?? ''}`,
      };
      host.send(msg);
    }
    host.exit(1);
  });

  host.on('unhandledRejection', (reason: unknown) => {
    if (host.send) {
      const msg: WorkerResponse = {
        type: 'log',
        level: 5, // Fatal
        category: 'Worker',
        message: `Unhandled rejection: ${reason instanceof Error ? reason.message : String(reason)}`,
      };
      host.send(msg);
    }
  });
}
