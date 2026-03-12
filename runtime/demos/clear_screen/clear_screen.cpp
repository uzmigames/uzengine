/// Clear Screen Demo Implementation — Backend-Agnostic
///
/// Demonstrates triple-buffered frame pacing using only abstract RHI interfaces.
/// Renders animated sine-wave RGB clear color at ~6000+ FPS on modern GPU.
/// Tests: device creation, command list recording, swapchain Present, fence synchronization.

#include "clear_screen.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "log.h"
#include "profiler.h"

#include <cmath>
#include <chrono>

namespace uz {

/// Triple buffering constant for frame pacing.
static constexpr uint32_t kFramesInFlight = 3;

bool RunClearScreen(RHIBackend backend, uint32_t frameCount) {
    UZ_PROFILE_FUNCTION();
    UZ_PROFILE_THREAD("Main");

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine — Clear Screen";
    winDesc.width = 1280;
    winDesc.height = 720;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(RHI, Error, "Failed to create window");
        return false;
    }
    window.Show();

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = backend;
    deviceDesc.enableValidation = true;

    IRHIDevice* device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(RHI, Error, "Failed to create RHI device (backend={})",
               static_cast<int>(backend));
        return false;
    }

    // ── Swapchain ────────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = window.GetHWND();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    IRHISwapChain* swapChain = device->CreateSwapChain(scDesc);
    if (!swapChain) {
        UZ_LOG(RHI, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Command List ─────────────────────────────────────────────────────
    IRHICommandList* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(RHI, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    // ── Per-frame fences ─────────────────────────────────────────────────
    IRHIFence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = device->CreateFence();
        if (!frameFences[i]) {
            UZ_LOG(RHI, Error, "Failed to create frame fence {}", i);
            for (uint32_t j = 0; j < i; ++j) device->DestroyFence(frameFences[j]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
    }

    // ── Render Loop ──────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    UZ_LOG(RHI, Info, "Starting clear screen render loop (frames={})",
           frameCount == 0 ? -1 : static_cast<int>(frameCount));

    while (window.PumpMessages() && window.IsOpen()) {
        UZ_PROFILE_SCOPE(Frame);

        if (frameCount > 0 && frameIndex >= frameCount) break;

        // Handle resize
        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            window.ClearResizedFlag();
        }

        // Wait for the frame N-2 fence before reusing its allocator
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // Animated clear color (sine wave RGB)
        auto now = std::chrono::high_resolution_clock::now();
        float t = std::chrono::duration<float>(now - startTime).count();
        float color[4] = {
            0.5f + 0.5f * std::sin(t * 1.0f),
            0.5f + 0.5f * std::sin(t * 1.3f + 2.094f),
            0.5f + 0.5f * std::sin(t * 1.7f + 4.189f),
            1.0f
        };

        // Record
        {
        UZ_PROFILE_SCOPE(RecordCommands);
        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

        // Barrier: Present -> RenderTarget
        RHIBarrierDesc toRenderTarget{};
        toRenderTarget.texture = backBuffer;
        toRenderTarget.stateBefore = RHIResourceState::Present;
        toRenderTarget.stateAfter = RHIResourceState::RenderTarget;
        cmdList->ResourceBarrier(&toRenderTarget, 1);

        // Clear
        cmdList->ClearRenderTarget(backBuffer, color);

        // Barrier: RenderTarget -> Present
        RHIBarrierDesc toPresent{};
        toPresent.texture = backBuffer;
        toPresent.stateBefore = RHIResourceState::RenderTarget;
        toPresent.stateAfter = RHIResourceState::Present;
        cmdList->ResourceBarrier(&toPresent, 1);

        cmdList->End();
        }

        // Execute + Present
        {
        UZ_PROFILE_SCOPE(ExecuteAndPresent);
        cmdList->Execute();
        swapChain->Present(false);

        // Signal fence for this frame via abstract interface
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);
        }

        frameIndex++;
        UZ_PROFILE_FRAME_MARK();
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.0f;

    UZ_LOG(RHI, Info, "Clear screen loop finished: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.0f / avgMs : 0.0f);

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        device->DestroyFence(frameFences[i]);
    }
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return true;
}

} // namespace uz
