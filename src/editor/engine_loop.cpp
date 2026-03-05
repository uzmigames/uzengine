// ---------------------------------------------------------------------------
// engine_loop.cpp -- Main render/message loop for the engine.
// ---------------------------------------------------------------------------
#include "engine_loop.h"
#include "stream.h"

#include "common/timer.h"
#include "common/types.h"
#include "common/ipc_poller.h"
#include "engine/renderer/renderer.h"
#include "engine/renderer/scene_params.h"

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

            // Only readback when encoders have room.
            const bool needReadback = g_frameRingA.size() < 2 || g_frameRingB.size() < 2;
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

            // Dispatch frame to encoder threads.
            if (needReadback) {
                uz::FrameData frame = renderer.getLatestFrame();
                if (!frame.pixels.empty()) {
                    auto* preferred = g_dispatchToggle ? &g_frameRingA : &g_frameRingB;
                    auto* fallback  = g_dispatchToggle ? &g_frameRingB : &g_frameRingA;
                    g_dispatchToggle = !g_dispatchToggle;

                    if (preferred->size() < 2) {
                        preferred->push(std::move(frame));
                    } else if (fallback->size() < 2) {
                        fallback->push(std::move(frame));
                    }
                }
            }
        }
    }

    // Cleanup.
    ipcPoller.stop();

    return static_cast<int>(msg.wParam);
}
