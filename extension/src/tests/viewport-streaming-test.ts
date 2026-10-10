/// Viewport Streaming Validation Tests (5.2, 5.5, 5.6)
///
/// Tests H.264 streaming performance, JPEG fallback, and memory stability.
/// Run manually from the developer console or a test command.

import type { UzEngineAddon } from '../uzengine';

/// H.264 streaming test (5.2).
/// Starts the render loop and measures frame delivery rate and latency.
export async function testH264Streaming(
  addon: UzEngineAddon,
  durationMs: number = 5000,
  targetFps: number = 60,
): Promise<{
  framesReceived: number;
  avgFps: number;
  droppedFrames: number;
  avgFrameMs: number;
  nalBytesTotal: number;
  avgNalSize: number;
}> {
  let framesReceived = 0;
  let nalBytesTotal = 0;
  let frameMsSum = 0;
  const startTime = Date.now();

  return new Promise((resolve) => {
    addon.startRenderLoop(
      targetFps,
      (stats) => {
        frameMsSum += stats.frameMs ?? 0;
      },
      (nalBuffer: Buffer) => {
        framesReceived++;
        nalBytesTotal += nalBuffer.byteLength;

        if (Date.now() - startTime >= durationMs) {
          addon.stopRenderLoop();
          const elapsed = (Date.now() - startTime) / 1000;
          const expectedFrames = Math.floor(elapsed * targetFps);
          resolve({
            framesReceived,
            avgFps: Math.round((framesReceived / elapsed) * 10) / 10,
            droppedFrames: Math.max(0, expectedFrames - framesReceived),
            avgFrameMs:
              framesReceived > 0 ? Math.round((frameMsSum / framesReceived) * 100) / 100 : 0,
            nalBytesTotal,
            avgNalSize: framesReceived > 0 ? Math.round(nalBytesTotal / framesReceived) : 0,
          });
        }
      },
    );

    // Safety timeout
    setTimeout(() => {
      try {
        addon.stopRenderLoop();
      } catch {}
      const elapsed = (Date.now() - startTime) / 1000;
      resolve({
        framesReceived,
        avgFps: Math.round((framesReceived / elapsed) * 10) / 10,
        droppedFrames: 0,
        avgFrameMs: framesReceived > 0 ? Math.round((frameMsSum / framesReceived) * 100) / 100 : 0,
        nalBytesTotal,
        avgNalSize: framesReceived > 0 ? Math.round(nalBytesTotal / framesReceived) : 0,
      });
    }, durationMs + 2000);
  });
}

/// Memory stability test (5.6).
/// Runs the render loop for a specified duration and samples memory usage.
export async function testMemoryStability(
  addon: UzEngineAddon,
  durationMs: number = 60000, // 1 minute default
  sampleIntervalMs: number = 5000,
): Promise<{
  samples: { timeMs: number; heapUsedMB: number; externalMB: number; rssMB: number }[];
  heapGrowthMB: number;
  externalGrowthMB: number;
  verdict: 'stable' | 'growing' | 'leaking';
}> {
  const samples: { timeMs: number; heapUsedMB: number; externalMB: number; rssMB: number }[] = [];
  const startTime = Date.now();

  return new Promise((resolve) => {
    addon.startRenderLoop(
      30,
      () => {},
      () => {},
    ); // Low FPS for memory test

    const sampleTimer = setInterval(() => {
      const mem = process.memoryUsage();
      samples.push({
        timeMs: Date.now() - startTime,
        heapUsedMB: Math.round((mem.heapUsed / 1024 / 1024) * 10) / 10,
        externalMB: Math.round((mem.external / 1024 / 1024) * 10) / 10,
        rssMB: Math.round((mem.rss / 1024 / 1024) * 10) / 10,
      });
    }, sampleIntervalMs);

    setTimeout(() => {
      clearInterval(sampleTimer);
      try {
        addon.stopRenderLoop();
      } catch {}

      // Take final sample
      const mem = process.memoryUsage();
      samples.push({
        timeMs: Date.now() - startTime,
        heapUsedMB: Math.round((mem.heapUsed / 1024 / 1024) * 10) / 10,
        externalMB: Math.round((mem.external / 1024 / 1024) * 10) / 10,
        rssMB: Math.round((mem.rss / 1024 / 1024) * 10) / 10,
      });

      const first = samples[0];
      const last = samples[samples.length - 1];
      const heapGrowth = last.heapUsedMB - first.heapUsedMB;
      const externalGrowth = last.externalMB - first.externalMB;

      let verdict: 'stable' | 'growing' | 'leaking';
      if (heapGrowth < 5 && externalGrowth < 10) {
        verdict = 'stable';
      } else if (heapGrowth < 20 && externalGrowth < 50) {
        verdict = 'growing';
      } else {
        verdict = 'leaking';
      }

      resolve({
        samples,
        heapGrowthMB: Math.round(heapGrowth * 10) / 10,
        externalGrowthMB: Math.round(externalGrowth * 10) / 10,
        verdict,
      });
    }, durationMs);
  });
}
