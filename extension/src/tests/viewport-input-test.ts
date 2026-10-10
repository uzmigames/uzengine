/// Viewport Input Round-Trip Test (3.6 + 5.3 + 5.4)
///
/// Tests that all keyboard and mouse events flow correctly from
/// the webview through the Extension Host to the native runtime.
///
/// Run manually: import and call `runViewportInputTest(addon)` from
/// the developer console or a test command.

import type { UzEngineAddon } from '../uzengine';

interface InputTestResult {
  passed: number;
  failed: number;
  results: { name: string; ok: boolean; detail?: string }[];
}

/// Tests all KEY_MAP entries by calling onKeyEx and verifying no crash.
/// (Full round-trip verification requires reading back engine input state,
/// which is not exposed via N-API yet — this validates the N-API path.)
export function runViewportInputTest(addon: UzEngineAddon): InputTestResult {
  const results: InputTestResult = { passed: 0, failed: 0, results: [] };

  const check = (name: string, fn: () => void) => {
    try {
      fn();
      results.results.push({ name, ok: true });
      results.passed++;
    } catch (e: unknown) {
      results.results.push({ name, ok: false, detail: e instanceof Error ? e.message : String(e) });
      results.failed++;
    }
  };

  // ── Keyboard tests ──────────────────────────────────────────────────

  const KEY_MAP: Record<string, number> = {
    KeyW: 0x57,
    KeyA: 0x41,
    KeyS: 0x53,
    KeyD: 0x44,
    KeyQ: 0x51,
    KeyE: 0x45,
    KeyF: 0x46,
    KeyR: 0x52,
    KeyT: 0x54,
    KeyG: 0x47,
    KeyZ: 0x5a,
    KeyX: 0x58,
    Space: 0x20,
    Escape: 0x1b,
    Tab: 0x09,
    Enter: 0x0d,
    ShiftLeft: 0x10,
    ControlLeft: 0x11,
    AltLeft: 0x12,
    ArrowUp: 0x26,
    ArrowDown: 0x28,
    ArrowLeft: 0x25,
    ArrowRight: 0x27,
    F1: 0x70,
    F2: 0x71,
    F5: 0x74,
    F11: 0x7a,
    F12: 0x7b,
    Delete: 0x2e,
    Home: 0x24,
    End: 0x23,
    PageUp: 0x21,
    Digit1: 0x31,
    Digit0: 0x30,
  };

  for (const [code, vk] of Object.entries(KEY_MAP)) {
    check(`key down ${code} (VK=0x${vk.toString(16)})`, () => {
      addon.onKeyEx(vk, true, 0);
    });
    check(`key up ${code}`, () => {
      addon.onKeyEx(vk, false, 0);
    });
  }

  // ── Modifier combinations ───────────────────────────────────────────

  check('Ctrl+S (save)', () => {
    addon.onKeyEx(0x53, true, 2); // Ctrl + S
    addon.onKeyEx(0x53, false, 2);
  });

  check('Shift+F5 (debug)', () => {
    addon.onKeyEx(0x74, true, 1); // Shift + F5
    addon.onKeyEx(0x74, false, 1);
  });

  check('Ctrl+Shift+Alt+Meta all modifiers', () => {
    addon.onKeyEx(0x41, true, 0xf); // All 4 modifiers + A
    addon.onKeyEx(0x41, false, 0);
  });

  // ── Mouse tests ────────────────────────────────────────────────────

  check('mouse move absolute', () => {
    addon.onMouseMove(640, 360);
  });

  check('mouse move delta', () => {
    addon.onMouseMove(5, -3); // Relative delta
  });

  check('left click', () => {
    addon.onMouseButton(0, true);
    addon.onMouseButton(0, false);
  });

  check('right click', () => {
    addon.onMouseButton(2, true);
    addon.onMouseButton(2, false);
  });

  check('middle click', () => {
    addon.onMouseButton(1, true);
    addon.onMouseButton(1, false);
  });

  check('scroll up', () => {
    addon.onScroll(120);
  });

  check('scroll down', () => {
    addon.onScroll(-120);
  });

  return results;
}

/// Event loop responsiveness test (5.1).
/// Measures setImmediate callback latency while render loop is active.
export async function measureEventLoopLatency(durationMs: number = 3000): Promise<{
  samples: number;
  avgMs: number;
  maxMs: number;
  p99Ms: number;
}> {
  const latencies: number[] = [];
  const start = Date.now();

  return new Promise((resolve) => {
    const measure = () => {
      const t0 = process.hrtime.bigint();
      setImmediate(() => {
        const t1 = process.hrtime.bigint();
        const ms = Number(t1 - t0) / 1_000_000;
        latencies.push(ms);

        if (Date.now() - start < durationMs) {
          measure();
        } else {
          latencies.sort((a, b) => a - b);
          const avg = latencies.reduce((a, b) => a + b, 0) / latencies.length;
          const max = latencies[latencies.length - 1];
          const p99 = latencies[Math.floor(latencies.length * 0.99)];

          resolve({
            samples: latencies.length,
            avgMs: Math.round(avg * 100) / 100,
            maxMs: Math.round(max * 100) / 100,
            p99Ms: Math.round(p99 * 100) / 100,
          });
        }
      });
    };
    measure();
  });
}
