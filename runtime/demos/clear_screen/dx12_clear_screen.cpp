/// DX12 Clear Screen Demo Implementation
///
/// Demonstrates triple-buffered frame pacing with DX12 rendering.
/// Renders animated sine-wave RGB clear color at ~6000+ FPS on modern GPU.
/// Tests: device creation, command list recording, swapchain Present, fence synchronization.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_clear_screen.h"
#include "dx12_device.h"
#include "dx12_swapchain.h"
#include "dx12_command_list.h"
#include "dx12_fence.h"
#include "platform_window.h"
#include "log.h"

#include <cmath>
#include <chrono>
#include <d3d12.h>

namespace uz {

/// Triple buffering constant for frame pacing.
static constexpr uint32_t kFramesInFlight = 3;

bool RunClearScreen(uint32_t frameCount) {
    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine — DX12 Clear Screen";
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
    deviceDesc.backend = RHIBackend::DX12;
    deviceDesc.enableValidation = true;

    auto* rhiDevice = CreateRHIDevice(deviceDesc);
    if (!rhiDevice) {
        UZ_LOG(RHI, Error, "Failed to create RHI device");
        return false;
    }

    auto* device = static_cast<DX12Device*>(rhiDevice);

    // ── Swapchain ────────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = window.GetHWND();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    auto* rhiSwapChain = device->CreateSwapChain(scDesc);
    if (!rhiSwapChain) {
        UZ_LOG(RHI, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }
    auto* swapChain = static_cast<DX12SwapChain*>(rhiSwapChain);

    // ── Command List ─────────────────────────────────────────────────────
    auto* rhiCmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!rhiCmdList) {
        UZ_LOG(RHI, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }
    auto* cmdList = static_cast<DX12CommandList*>(rhiCmdList);

    // ── Per-frame fences ─────────────────────────────────────────────────
    DX12Fence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        auto* f = static_cast<DX12Fence*>(device->CreateFence());
        if (!f) {
            UZ_LOG(RHI, Error, "Failed to create frame fence {}", i);
            // Cleanup already-created fences
            for (uint32_t j = 0; j < i; ++j) device->DestroyFence(frameFences[j]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
        frameFences[i] = f;
    }

    // ── Render Loop ──────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    UZ_LOG(RHI, Info, "Starting clear screen render loop (frames={})", frameCount == 0 ? -1 : static_cast<int>(frameCount));

    while (window.PumpMessages() && window.IsOpen()) {
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
        cmdList->Begin();

        ID3D12Resource* backBuffer = swapChain->GetCurrentBackBufferResource();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain->GetCurrentRTV();

        // Barrier: Present → RenderTarget
        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        // Clear
        cmdList->ClearRTV(rtv, color);

        // Barrier: RenderTarget → Present
        cmdList->TransitionBarrier(backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);

        cmdList->End();
        cmdList->Execute();

        // Present
        swapChain->Present(false);

        // Signal fence for this frame
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        device->GetDirectQueue()->Signal(
            frameFences[allocFrame]->GetNativeFence(), globalFenceValue);

        frameIndex++;
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
