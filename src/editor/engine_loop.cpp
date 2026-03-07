// ---------------------------------------------------------------------------
// engine_loop.cpp -- Main render/message loop for the engine.
// ---------------------------------------------------------------------------
#include "engine_loop.h"
#include "stream.h"

#include "common/timer.h"
#include "common/types.h"
#include "common/ipc_poller.h"
#include "engine/renderer/core/renderer.h"
#include "engine/renderer/scene/scene_params.h"

#include <string>

int runMainLoop(HWND /*hwnd*/,
                uz::Renderer& renderer,
                uz::Timer& timer,
                uz::SceneParams& /*sceneParams*/,
                uz::EngineMetrics& g_metrics,
                uz::IpcPoller& ipcPoller) {

    // FPS tracking.
    LARGE_INTEGER fpsQpcFreq;
    QueryPerformanceFrequency(&fpsQpcFreq);
    LARGE_INTEGER fpsLastTime;
    QueryPerformanceCounter(&fpsLastTime);
    uint32_t fpsFrameCounter = 0;

    // Frame-rate limiter.
    constexpr double kTargetFrameTime = 1.0 / static_cast<double>(uz::kTargetFPS);
    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    LARGE_INTEGER qpcLast;
    QueryPerformanceCounter(&qpcLast);

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            // Frame-rate limiter: sleep + spin-wait.
            LARGE_INTEGER qpcNow;
            QueryPerformanceCounter(&qpcNow);
            double elapsed = static_cast<double>(qpcNow.QuadPart - qpcLast.QuadPart)
                           / static_cast<double>(qpcFreq.QuadPart);
            if (elapsed < kTargetFrameTime) {
                double sleepMs = (kTargetFrameTime - elapsed) * 1000.0 - 1.0;
                if (sleepMs > 1.0) {
                    ::Sleep(static_cast<DWORD>(sleepMs));
                }
                do {
                    QueryPerformanceCounter(&qpcNow);
                    elapsed = static_cast<double>(qpcNow.QuadPart - qpcLast.QuadPart)
                            / static_cast<double>(qpcFreq.QuadPart);
                } while (elapsed < kTargetFrameTime);
            }
            qpcLast = qpcNow;

            const double delta = timer.tick();
            renderer.update(timer.elapsed(), delta);

            // Only readback when the ring has room.  The encode thread drains
            // quickly (pops + drops rate-limited frames), so the ring stays
            // near-empty and readback fires on almost every render loop tick.
            const bool needReadback = g_frameRing.size() < kStreamRingSize;
            renderer.render(needReadback);

            // Update resolution metric.
            {
                const std::string currentRes =
                    std::to_string(renderer.getWidth()) + "x"
                    + std::to_string(renderer.getHeight());
                if (currentRes != g_metrics.resolution) {
                    g_metrics.resolution = currentRes;
                }
            }

            // FPS tracking.
            ++fpsFrameCounter;
            g_metrics.frameCount.fetch_add(1, std::memory_order_relaxed);

            {
                LARGE_INTEGER fpsNow;
                QueryPerformanceCounter(&fpsNow);
                const double fpsElapsed =
                    static_cast<double>(fpsNow.QuadPart - fpsLastTime.QuadPart)
                    / static_cast<double>(fpsQpcFreq.QuadPart);
                if (fpsElapsed >= 1.0) {
                    g_metrics.fps.store(
                        static_cast<float>(fpsFrameCounter) / static_cast<float>(fpsElapsed),
                        std::memory_order_relaxed);
                    fpsFrameCounter = 0;
                    fpsLastTime = fpsNow;
                }
            }

            // Dispatch frame to the encode ring.  The ring has kStreamRingSize
            // slots; push only when there is room (already guaranteed by the
            // needReadback gate above, but double-checked for safety).
            if (needReadback) {
                uz::FrameData frame = renderer.getLatestFrame();
                if (!frame.pixels.empty() && g_frameRing.size() < kStreamRingSize) {
                    g_frameRing.push(std::move(frame));
                }
            }
        }
    }

    // Cleanup.
    ipcPoller.stop();

    return static_cast<int>(msg.wParam);
}
