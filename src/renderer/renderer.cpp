// ---------------------------------------------------------------------------
// renderer.cpp -- DX12Renderer implementation
//
// Renders a spinning colored pyramid using Direct3D 12.
// The MVP matrix is passed to the vertex shader via root 32-bit constants
// (not a constant buffer), keeping the setup minimal and low-latency.
//
// Each frame is copied to a readback buffer after rendering so that the
// pixel data can be consumed by downstream encoding stages.
// ---------------------------------------------------------------------------

#include "renderer.h"

#include <stdexcept>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

// ---------------------------------------------------------------------------
// Inline helper: convert a failed HRESULT into a std::runtime_error.
// ---------------------------------------------------------------------------
inline void ThrowIfFailed(HRESULT hr, const char* context = nullptr) {
    if (SUCCEEDED(hr)) {
        return;
    }

    std::string msg = "D3D12 call failed with HRESULT 0x";

    // Build hex string without <sstream> to keep includes lean.
    char hex[16];
    snprintf(hex, sizeof(hex), "%08lX", static_cast<unsigned long>(hr));
    msg += hex;

    if (context) {
        msg += " (";
        msg += context;
        msg += ")";
    }

    throw std::runtime_error(msg);
}

} // anonymous namespace

namespace uz {

// ===========================================================================
// Public API
// ===========================================================================

void DX12Renderer::init(HWND hwnd, uint32_t width, uint32_t height) {
    if (initialized_) {
        return;
    }

    width_  = width;
    height_ = height;

    createDevice();
    createCommandQueue();
    createSwapChain(hwnd);
    createRTVHeap();
    createRenderTargets();
    createRootSignature();
    createPipelineState();
    createVertexBuffer();
    createConstantBuffer();
    createDepthBuffer();
    createFence();
    createReadbackBuffer();

    // Editor-mode geometry and pipeline (grid + wireframe cube).
    createLinePipelineState();
    createGridVertexBuffer();
    createCubeWireVertexBuffer();

    // -----------------------------------------------------------------------
    // Viewport covers the full render area.
    // -----------------------------------------------------------------------
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width    = static_cast<float>(width_);
    viewport_.Height   = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    // -----------------------------------------------------------------------
    // Scissor rect matches the viewport.
    // -----------------------------------------------------------------------
    scissorRect_.left   = 0;
    scissorRect_.top    = 0;
    scissorRect_.right  = static_cast<LONG>(width_);
    scissorRect_.bottom = static_cast<LONG>(height_);

    // Identity matrix as a safe default before the first update().
    XMStoreFloat4x4(&worldViewProj_, XMMatrixIdentity());
    XMStoreFloat4x4(&viewProj_, XMMatrixIdentity());

    // Record the stream start time so frame timestamps are relative to init.
    {
        LARGE_INTEGER freq;
        LARGE_INTEGER now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        streamStartMicros_ = static_cast<uint64_t>(
            now.QuadPart * 1'000'000 / freq.QuadPart);
    }

    initialized_ = true;
}

bool DX12Renderer::resize(int width, int height) {
    // Validate dimensions.
    if (width < 64 || width > 7680 || height < 64 || height > 4320) {
        return false;
    }
    if (!initialized_ || !swapChain_ || !device_) {
        return false;
    }

    // Skip no-op resizes.
    if (static_cast<uint32_t>(width) == width_ &&
        static_cast<uint32_t>(height) == height_) {
        return true;
    }

    // Drain the GPU so no in-flight work references the resources we are
    // about to release.
    waitForGpu();

    // Release all back-buffer references -- ResizeBuffers requires that no
    // outstanding references to the swap chain buffers exist.
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        renderTargets_[i].Reset();
    }

    // Release depth buffer (will be recreated at new dimensions).
    depthBuffer_.Reset();

    // Release readback buffers (will be recreated at new dimensions).
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        readbackBuffers_[i].Reset();
    }

    const uint32_t newW = static_cast<uint32_t>(width);
    const uint32_t newH = static_cast<uint32_t>(height);

    // Resize the swap chain buffers.
    HRESULT hr = swapChain_->ResizeBuffers(
        kFrameCount,
        newW,
        newH,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0);

    if (FAILED(hr)) {
        OutputDebugStringA("[UzEngine] SwapChain::ResizeBuffers failed\n");
        return false;
    }

    // Store new dimensions only AFTER successful swap chain resize.
    width_  = newW;
    height_ = newH;

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    // CRITICAL: After waitForGpu() drained the pipeline, the fence's completed
    // value was advanced.  ResizeBuffers may change the back-buffer index.  We
    // must re-sync ALL per-frame fence values so that the next moveToNextFrame()
    // doesn't signal a value *lower* than the fence's current completed value
    // (which would cause the fence to go backwards → deadlock).
    {
        const uint64_t syncVal = fence_->GetCompletedValue() + 1;
        for (uint32_t i = 0; i < kFrameCount; ++i) {
            fenceValues_[i] = syncVal;
        }
    }

    // Recreate render target views from the resized swap chain buffers.
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            rtvHeap_->GetCPUDescriptorHandleForHeapStart();

        for (uint32_t i = 0; i < kFrameCount; ++i) {
            ThrowIfFailed(
                swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])),
                "SwapChain::GetBuffer (resize)");

            device_->CreateRenderTargetView(
                renderTargets_[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += rtvDescriptorSize_;
        }
    }

    // Recreate the depth buffer at the new dimensions (reuses existing
    // DSV heap -- the descriptor is overwritten in place).
    {
        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Alignment        = 0;
        depthDesc.Width            = width_;
        depthDesc.Height           = height_;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels        = 1;
        depthDesc.Format           = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc       = {1, 0};
        depthDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format               = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth   = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type                 = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask     = 1;
        heapProps.VisibleNodeMask      = 1;

        ThrowIfFailed(
            device_->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clearValue,
                IID_PPV_ARGS(&depthBuffer_)),
            "CreateCommittedResource (depth buffer resize)");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;

        device_->CreateDepthStencilView(
            depthBuffer_.Get(), &dsvDesc,
            dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    }

    // Recreate readback buffers at the new dimensions.
    {
        const uint32_t alignedPitch =
            (width_ * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

        const uint64_t bufferSize =
            static_cast<uint64_t>(alignedPitch) * height_;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type                 = D3D12_HEAP_TYPE_READBACK;
        heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask     = 1;
        heapProps.VisibleNodeMask      = 1;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment        = 0;
        bufferDesc.Width            = bufferSize;
        bufferDesc.Height           = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels        = 1;
        bufferDesc.Format           = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc       = {1, 0};
        bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

        for (uint32_t i = 0; i < kFrameCount; ++i) {
            ThrowIfFailed(
                device_->CreateCommittedResource(
                    &heapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &bufferDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(&readbackBuffers_[i])),
                "CreateCommittedResource (readback buffer resize)");
        }
    }

    // Update viewport and scissor rect to match the new dimensions.
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width    = static_cast<float>(width_);
    viewport_.Height   = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left   = 0;
    scissorRect_.top    = 0;
    scissorRect_.right  = static_cast<LONG>(width_);
    scissorRect_.bottom = static_cast<LONG>(height_);

    return true;
}

void DX12Renderer::update(double /*totalTime*/, double deltaTime) {
    // Update the FPS counter (rolling average over 60 samples).
    fpsOverlay_.update(deltaTime);

    // -- Check for pending viewport resize before any rendering work. ---------
    // Always consume the pending resize (clears the atomic) but only apply
    // it if the dimensions actually changed.  The resize itself is expensive
    // (GPU drain + resource recreation) so we just store the latest request
    // and apply once per frame — the webview debounces on its end too.
    if (sceneParams_) {
        auto [rw, rh] = sceneParams_->consumeResize();
        if (rw > 0 && rh > 0 &&
            (static_cast<uint32_t>(rw) != width_ ||
             static_cast<uint32_t>(rh) != height_)) {
            resize(rw, rh);
        }
    }

    // Read rotation speeds from IPC-controlled scene params, falling back to
    // the original hardcoded values when no SceneParams is attached.
    float rotYSpeed = 1.2f;
    float rotXTilt  = 0.3f;
    if (sceneParams_) {
        rotYSpeed = sceneParams_->rotY.load(std::memory_order_relaxed);
        rotXTilt  = sceneParams_->rotX.load(std::memory_order_relaxed);

        // Copy background color under lock (three floats must be coherent).
        {
            std::lock_guard<std::mutex> lk(sceneParams_->bgMutex);
            clearR_ = sceneParams_->bgR;
            clearG_ = sceneParams_->bgG;
            clearB_ = sceneParams_->bgB;
        }
    }

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);

    if (editorMode_) {
        // -----------------------------------------------------------------
        // Unreal Engine 5-style fly camera controller.
        // Input is written by the network thread (WS text messages) into
        // sceneParams_->cameraInput atomics.  Camera state lives in the
        // sceneParams_ plain floats (written ONLY here on the render thread).
        // -----------------------------------------------------------------
        const float dt = std::min(static_cast<float>(deltaTime), 0.1f);

        if (sceneParams_) {
            auto& ci  = sceneParams_->cameraInput;
            auto& sp  = *sceneParams_;

            // -- Scroll wheel → adjust camera speed (multiplicative) ------
            const float scroll = ci.scrollDelta.exchange(0.0f, std::memory_order_relaxed);
            if (scroll != 0.0f) {
                sp.camSpeed *= std::pow(1.1f, scroll);
                sp.camSpeed  = std::clamp(sp.camSpeed, 0.1f, 500.0f);
            }

            // -- Mouse look (only when RMB held) --------------------------
            const float mdx = ci.mouseDeltaX.exchange(0.0f, std::memory_order_relaxed);
            const float mdy = ci.mouseDeltaY.exchange(0.0f, std::memory_order_relaxed);
            const bool  rmb = ci.rightMouseDown.load(std::memory_order_relaxed);

            if (rmb) {
                sp.camYaw   += mdx * sp.camSensitivity;
                sp.camPitch -= mdy * sp.camSensitivity;  // Inverted Y (UE5 convention)

                // Clamp pitch to +/-89 degrees to avoid gimbal lock at the poles.
                constexpr float kMaxPitch = 89.0f * (XM_PI / 180.0f);
                sp.camPitch = std::clamp(sp.camPitch, -kMaxPitch, kMaxPitch);
            }
            // When RMB is not held, deltas were already consumed (exchanged to 0)
            // so they won't accumulate.

            // -- Forward / right / up vectors from yaw + pitch ------------
            // Left-handed coordinate system: +X right, +Y up, +Z forward.
            // Yaw 0 = looking along +Z, pitch 0 = horizontal.
            const float cosYaw   = std::cos(sp.camYaw);
            const float sinYaw   = std::sin(sp.camYaw);
            const float cosPitch = std::cos(sp.camPitch);
            const float sinPitch = std::sin(sp.camPitch);

            const XMVECTOR forward = XMVectorSet(
                sinYaw * cosPitch,   // X
                sinPitch,            // Y (negative pitch = looking down)
                cosYaw * cosPitch,   // Z
                0.0f);

            const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            const XMVECTOR right   = XMVector3Normalize(XMVector3Cross(worldUp, forward));

            // -- WASD + EQ movement (only when RMB held) ------------------
            if (rmb) {
                const float fwd    = ci.moveForward.load(std::memory_order_relaxed)
                                   - ci.moveBack.load(std::memory_order_relaxed);
                const float strafe = ci.moveRight.load(std::memory_order_relaxed)
                                   - ci.moveLeft.load(std::memory_order_relaxed);
                const float vert   = ci.moveUp.load(std::memory_order_relaxed)
                                   - ci.moveDown.load(std::memory_order_relaxed);

                const float boost = ci.shiftDown.load(std::memory_order_relaxed) ? 3.0f : 1.0f;
                const float speed = sp.camSpeed * boost * dt;

                // Accumulate position along each axis.
                const XMVECTOR movement = forward * fwd * speed
                                        + right   * strafe * speed
                                        + worldUp * vert   * speed;

                sp.camPosX += XMVectorGetX(movement);
                sp.camPosY += XMVectorGetY(movement);
                sp.camPosZ += XMVectorGetZ(movement);
            }

            // -- Build view matrix ----------------------------------------
            const XMVECTOR eye = XMVectorSet(sp.camPosX, sp.camPosY, sp.camPosZ, 1.0f);
            const XMVECTOR at  = XMVectorAdd(eye, forward);
            const XMMATRIX view = XMMatrixLookAtLH(eye, at, worldUp);
            const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 500.0f);

            const XMMATRIX vp = view * proj;
            XMStoreFloat4x4(&viewProj_, vp);
            XMStoreFloat4x4(&worldViewProj_, vp);
        } else {
            // No scene params attached -- use default static camera.
            const XMVECTOR eye = XMVectorSet(0.0f, 15.0f, -20.0f, 1.0f);
            const XMVECTOR at  = XMVectorSet(0.0f, 0.0f,   0.0f, 1.0f);
            const XMVECTOR up  = XMVectorSet(0.0f, 1.0f,   0.0f, 0.0f);
            const XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
            const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 500.0f);

            const XMMATRIX vp = view * proj;
            XMStoreFloat4x4(&viewProj_, vp);
            XMStoreFloat4x4(&worldViewProj_, vp);
        }
    } else {
        // Legacy pyramid spinning camera.
        const float dt = static_cast<float>(deltaTime);
        rotAngleY_ += dt * rotYSpeed;
        rotAngleX_ += dt * rotXTilt;

        const XMMATRIX world = XMMatrixRotationY(rotAngleY_)
                             * XMMatrixRotationX(0.3f + rotAngleX_);

        const XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -3.0f, 1.0f);
        const XMVECTOR at  = XMVectorSet(0.0f, 0.0f,  0.0f, 1.0f);
        const XMVECTOR up  = XMVectorSet(0.0f, 1.0f,  0.0f, 0.0f);
        const XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 100.0f);

        const XMMATRIX vp = view * proj;
        XMStoreFloat4x4(&viewProj_, vp);
        XMStoreFloat4x4(&worldViewProj_, world * vp);
    }
}

void DX12Renderer::render(bool readback) {
    // -----------------------------------------------------------------------
    // Reset the command allocator and command list for the current frame.
    // -----------------------------------------------------------------------
    auto& allocator = commandAllocators_[frameIndex_];
    ThrowIfFailed(allocator->Reset(), "CommandAllocator::Reset");
    ThrowIfFailed(commandList_->Reset(allocator.Get(), pipelineState_.Get()),
                  "CommandList::Reset");

    // -----------------------------------------------------------------------
    // Set pipeline-wide state.
    // -----------------------------------------------------------------------
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);

    // -----------------------------------------------------------------------
    // Transition the back buffer: PRESENT -> RENDER_TARGET.
    // -----------------------------------------------------------------------
    D3D12_RESOURCE_BARRIER barrierToRT{};
    barrierToRT.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierToRT.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrierToRT.Transition.pResource   = renderTargets_[frameIndex_].Get();
    barrierToRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierToRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrierToRT.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &barrierToRT);

    // -----------------------------------------------------------------------
    // Clear and bind the render target.
    // -----------------------------------------------------------------------
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(frameIndex_) * rtvDescriptorSize_;

    const float clearColor[4] = { clearR_, clearG_, clearB_, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Clear and bind the depth-stencil view so occluded faces are discarded.
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->ClearDepthStencilView(
        dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // -----------------------------------------------------------------------
    // Draw the scene.  Editor mode renders a ground grid and wireframe cubes
    // at each loaded node position; legacy mode renders the spinning pyramid.
    // -----------------------------------------------------------------------
    const bool paused = sceneParams_ && sceneParams_->paused.load(std::memory_order_relaxed);

    if (editorMode_) {
        // -- Ground grid --
        commandList_->SetPipelineState(linePipelineState_.Get());
        commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

        // Grid uses identity world, so worldViewProj_ already equals viewProj_.
        commandList_->SetGraphicsRoot32BitConstants(0, 16, &worldViewProj_, 0);

        // Grid vertex colors are baked; pass white (1,1,1) as the color multiplier.
        const float white[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        commandList_->SetGraphicsRoot32BitConstants(1, 4, white, 0);

        commandList_->IASetVertexBuffers(0, 1, &gridVertexBufferView_);
        commandList_->DrawInstanced(gridVertexCount_, 1, 0, 0);

        // -- Wireframe cubes at each editor node --
        commandList_->IASetVertexBuffers(0, 1, &cubeWireVertexBufferView_);
        {
            std::lock_guard<std::mutex> lk(editorNodesMutex_);
            const XMMATRIX vp = XMLoadFloat4x4(&viewProj_);

            for (const auto& node : editorNodes_) {
                const XMMATRIX nodeWorld =
                    XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z)
                    * XMMatrixRotationRollPitchYaw(node.rotation.x, node.rotation.y, node.rotation.z)
                    * XMMatrixTranslation(node.position.x, node.position.y, node.position.z);
                const XMMATRIX nodeMVP = nodeWorld * vp;

                XMFLOAT4X4 mvp;
                XMStoreFloat4x4(&mvp, nodeMVP);
                commandList_->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);

                const float col[4] = {node.color.x, node.color.y, node.color.z, 0.0f};
                commandList_->SetGraphicsRoot32BitConstants(1, 4, col, 0);

                commandList_->DrawInstanced(cubeWireVertexCount_, 1, 0, 0);
            }
        }
    } else if (!paused) {
        // Legacy pyramid draw.
        commandList_->SetGraphicsRoot32BitConstants(0, 16, &worldViewProj_, 0);

        const float white[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        commandList_->SetGraphicsRoot32BitConstants(1, 4, white, 0);

        commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
        commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList_->DrawInstanced(12, 1, 0, 0);
    }

    if (readback) {
        // -------------------------------------------------------------------
        // Transition for readback: RENDER_TARGET -> COPY_SOURCE.
        // -------------------------------------------------------------------
        D3D12_RESOURCE_BARRIER barrierToCopySrc{};
        barrierToCopySrc.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierToCopySrc.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierToCopySrc.Transition.pResource   = renderTargets_[frameIndex_].Get();
        barrierToCopySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierToCopySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrierToCopySrc.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        commandList_->ResourceBarrier(1, &barrierToCopySrc);

        // -------------------------------------------------------------------
        // Copy the render target into the readback buffer for CPU access.
        // -------------------------------------------------------------------
        const uint32_t alignedPitch = (width_ * 4 + 255) & ~255u;

        D3D12_TEXTURE_COPY_LOCATION srcLocation{};
        srcLocation.pResource        = renderTargets_[frameIndex_].Get();
        srcLocation.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLocation{};
        dstLocation.pResource                          = readbackBuffers_[frameIndex_].Get();
        dstLocation.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint.Offset             = 0;
        dstLocation.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        dstLocation.PlacedFootprint.Footprint.Width    = width_;
        dstLocation.PlacedFootprint.Footprint.Height   = height_;
        dstLocation.PlacedFootprint.Footprint.Depth    = 1;
        dstLocation.PlacedFootprint.Footprint.RowPitch = alignedPitch;

        commandList_->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

        // -------------------------------------------------------------------
        // Transition: COPY_SOURCE -> PRESENT.
        // -------------------------------------------------------------------
        D3D12_RESOURCE_BARRIER barrierToPresent{};
        barrierToPresent.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierToPresent.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierToPresent.Transition.pResource   = renderTargets_[frameIndex_].Get();
        barrierToPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrierToPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        commandList_->ResourceBarrier(1, &barrierToPresent);
    } else {
        // -------------------------------------------------------------------
        // No readback — go directly RENDER_TARGET -> PRESENT.
        // -------------------------------------------------------------------
        D3D12_RESOURCE_BARRIER barrierToPresent{};
        barrierToPresent.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierToPresent.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierToPresent.Transition.pResource   = renderTargets_[frameIndex_].Get();
        barrierToPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrierToPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        commandList_->ResourceBarrier(1, &barrierToPresent);
    }

    // -----------------------------------------------------------------------
    // Close and execute.
    // -----------------------------------------------------------------------
    ThrowIfFailed(commandList_->Close(), "CommandList::Close");

    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    // -----------------------------------------------------------------------
    // Present without vsync to minimize latency (~16ms saved).
    // -----------------------------------------------------------------------
    ThrowIfFailed(swapChain_->Present(0, 0), "SwapChain::Present");

    // -----------------------------------------------------------------------
    // Readback: wait for GPU copy completion, then map and copy pixels to CPU.
    // Only performed when readback was requested (GPU copy was recorded above).
    // -----------------------------------------------------------------------
    if (readback) {
        performReadback();
    }

    moveToNextFrame();
}

FrameData DX12Renderer::getLatestFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestFrame_;
}

FrameData DX12Renderer::getLastFrameCopy() const {
    return getLatestFrame();
}

void DX12Renderer::setSceneParams(SceneParams* params) {
    sceneParams_ = params;
}

void DX12Renderer::shutdown() {
    if (!initialized_) {
        return;
    }

    // Ensure the GPU is completely idle before releasing resources.
    waitForGpu();

    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }

    if (readbackEvent_) {
        CloseHandle(readbackEvent_);
        readbackEvent_ = nullptr;
    }

    // Release readback resources.
    readbackFence_.Reset();
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        readbackBuffers_[i].Reset();
    }

    // Release editor-mode resources.
    cubeWireVertexBuffer_.Reset();
    gridVertexBuffer_.Reset();
    linePipelineState_.Reset();

    // Release all COM resources in reverse-creation order.
    fence_.Reset();

    constantBuffer_.Reset();
    cbvDataBegin_ = nullptr;

    vertexBuffer_.Reset();
    depthBuffer_.Reset();
    dsvHeap_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        renderTargets_[i].Reset();
        commandAllocators_[i].Reset();
    }

    commandList_.Reset();
    rtvHeap_.Reset();
    swapChain_.Reset();
    commandQueue_.Reset();
    device_.Reset();
    factory_.Reset();

    initialized_ = false;
}

// ===========================================================================
// Private -- device & factory creation
// ===========================================================================

void DX12Renderer::createDevice() {
    UINT dxgiFactoryFlags = 0;

    // -----------------------------------------------------------------
    // Debug layer (only in debug builds).
    // -----------------------------------------------------------------
#ifdef UZENGINE_DEBUG
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    // -----------------------------------------------------------------
    // DXGI factory.
    // -----------------------------------------------------------------
    ThrowIfFailed(
        CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory_)),
        "CreateDXGIFactory2");

    // -----------------------------------------------------------------
    // Enumerate adapters and pick the first hardware adapter that
    // supports Direct3D 12 feature level 12_0.
    // -----------------------------------------------------------------
    ComPtr<IDXGIAdapter1> adapter;
    bool adapterFound = false;

    for (UINT adapterIndex = 0;
         factory_->EnumAdapterByGpuPreference(
             adapterIndex,
             DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        // Skip software / WARP adapters.
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Check feature-level support without creating the device yet.
        if (SUCCEEDED(D3D12CreateDevice(
                adapter.Get(),
                D3D_FEATURE_LEVEL_12_0,
                _uuidof(ID3D12Device),
                nullptr)))
        {
            adapterFound = true;
            break;
        }
    }

    if (!adapterFound) {
        throw std::runtime_error(
            "No hardware adapter found supporting D3D feature level 12_0");
    }

    // -----------------------------------------------------------------
    // Create the device on the chosen adapter.
    // -----------------------------------------------------------------
    ThrowIfFailed(
        D3D12CreateDevice(
            adapter.Get(),
            D3D_FEATURE_LEVEL_12_0,
            IID_PPV_ARGS(&device_)),
        "D3D12CreateDevice");
}

// ===========================================================================
// Private -- command queue
// ===========================================================================

void DX12Renderer::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    ThrowIfFailed(
        device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_)),
        "CreateCommandQueue");
}

// ===========================================================================
// Private -- swap chain
// ===========================================================================

void DX12Renderer::createSwapChain(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width       = width_;
    swapChainDesc.Height      = height_;
    swapChainDesc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo      = FALSE;
    swapChainDesc.SampleDesc  = { 1, 0 }; // No MSAA.
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = kFrameCount;
    swapChainDesc.Scaling     = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags       = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(
        factory_->CreateSwapChainForHwnd(
            commandQueue_.Get(), // Swap chain needs the queue for Present.
            hwnd,
            &swapChainDesc,
            nullptr,  // No fullscreen desc.
            nullptr,  // No restrict-to-output.
            &swapChain1),
        "CreateSwapChainForHwnd");

    // Disable Alt+Enter fullscreen toggle -- we manage it ourselves.
    ThrowIfFailed(
        factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
        "MakeWindowAssociation");

    // Obtain the IDXGISwapChain3 interface for GetCurrentBackBufferIndex().
    ThrowIfFailed(
        swapChain1.As(&swapChain_),
        "QueryInterface IDXGISwapChain3");

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

// ===========================================================================
// Private -- render-target-view descriptor heap
// ===========================================================================

void DX12Renderer::createRTVHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = kFrameCount;
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask       = 0;

    ThrowIfFailed(
        device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)),
        "CreateDescriptorHeap (RTV)");

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

// ===========================================================================
// Private -- render targets (one per back buffer)
// ===========================================================================

void DX12Renderer::createRenderTargets() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        ThrowIfFailed(
            swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])),
            "SwapChain::GetBuffer");

        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize_;
    }

    // Create a command allocator per frame.
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        ThrowIfFailed(
            device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&commandAllocators_[i])),
            "CreateCommandAllocator");
    }
}

// ===========================================================================
// Private -- root signature
// ===========================================================================

void DX12Renderer::createRootSignature() {
    // Root parameter 0: MVP matrix (16 x 32-bit constants at b0).
    // Root parameter 1: Color multiplier (4 x 32-bit constants at b1).
    D3D12_ROOT_PARAMETER rootParams[2]{};

    rootParams[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister  = 0;    // b0
    rootParams[0].Constants.RegisterSpace   = 0;
    rootParams[0].Constants.Num32BitValues  = 16;   // sizeof(XMFLOAT4X4) / 4
    rootParams[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParams[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.ShaderRegister  = 1;    // b1
    rootParams[1].Constants.RegisterSpace   = 0;
    rootParams[1].Constants.Num32BitValues  = 4;    // float3 + padding
    rootParams[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters     = 2;
    rootSigDesc.pParameters       = rootParams;
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.pStaticSamplers   = nullptr;
    rootSigDesc.Flags             =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS      |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS    |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error);

    if (FAILED(hr)) {
        std::string msg = "D3D12SerializeRootSignature failed";
        if (error) {
            msg += ": ";
            msg += static_cast<const char*>(error->GetBufferPointer());
        }
        throw std::runtime_error(msg);
    }

    ThrowIfFailed(
        device_->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)),
        "CreateRootSignature");
}

// ===========================================================================
// Private -- pipeline state object (PSO)
// ===========================================================================

void DX12Renderer::createPipelineState() {
    // -----------------------------------------------------------------
    // Compile shaders from the embedded HLSL source strings.
    // -----------------------------------------------------------------
    UINT compileFlags = 0;
#ifdef UZENGINE_DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompile(
        kVertexShaderSrc,
        strlen(kVertexShaderSrc),
        "VertexShader",  // Source name for diagnostics.
        nullptr,         // No defines.
        nullptr,         // No includes.
        "VSMain",
        "vs_5_0",
        compileFlags,
        0,
        &vertexShader,
        &errors);

    if (FAILED(hr)) {
        std::string msg = "Vertex shader compilation failed";
        if (errors) {
            msg += ": ";
            msg += static_cast<const char*>(errors->GetBufferPointer());
        }
        throw std::runtime_error(msg);
    }

    errors.Reset();

    hr = D3DCompile(
        kPixelShaderSrc,
        strlen(kPixelShaderSrc),
        "PixelShader",
        nullptr,
        nullptr,
        "PSMain",
        "ps_5_0",
        compileFlags,
        0,
        &pixelShader,
        &errors);

    if (FAILED(hr)) {
        std::string msg = "Pixel shader compilation failed";
        if (errors) {
            msg += ": ";
            msg += static_cast<const char*>(errors->GetBufferPointer());
        }
        throw std::runtime_error(msg);
    }

    // -----------------------------------------------------------------
    // Input layout -- matches the Vertex struct.
    // -----------------------------------------------------------------
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
            "POSITION", 0,
            DXGI_FORMAT_R32G32B32_FLOAT, 0,
            0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "COLOR", 0,
            DXGI_FORMAT_R32G32B32_FLOAT, 0,
            sizeof(XMFLOAT3),  // 12 bytes offset after POSITION.
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        }
    };

    // -----------------------------------------------------------------
    // PSO descriptor.
    // -----------------------------------------------------------------
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature        = rootSignature_.Get();
    psoDesc.VS                    = { vertexShader->GetBufferPointer(),
                                      vertexShader->GetBufferSize() };
    psoDesc.PS                    = { pixelShader->GetBufferPointer(),
                                      pixelShader->GetBufferSize() };

    // Rasterizer -- default, back-face culling, solid fill.
    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;
    psoDesc.RasterizerState.MultisampleEnable     = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount     = 0;
    psoDesc.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Blend -- opaque, no blending.
    psoDesc.BlendState.AlphaToCoverageEnable  = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth testing enabled for correct 3D pyramid rendering.
    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable  = FALSE;
    psoDesc.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;

    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc            = { 1, 0 }; // No MSAA.

    ThrowIfFailed(
        device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_)),
        "CreateGraphicsPipelineState");

    // -----------------------------------------------------------------
    // Create the command list (initially closed -- we reset it each frame).
    // -----------------------------------------------------------------
    ThrowIfFailed(
        device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocators_[frameIndex_].Get(),
            pipelineState_.Get(),
            IID_PPV_ARGS(&commandList_)),
        "CreateCommandList");

    // The command list is created in the recording state. Close it so that
    // the render loop can call Reset() at the top of each frame.
    ThrowIfFailed(commandList_->Close(), "Initial CommandList::Close");
}

// ===========================================================================
// Private -- vertex buffer (upload heap)
// ===========================================================================

void DX12Renderer::createVertexBuffer() {
    // Named reference points for readability.
    //   Apex  : (0, 0.7, 0)       -- yellow
    //   Base0 : (0, -0.3, 0.6)    -- red    (front)
    //   Base1 : (-0.52, -0.3, -0.3) -- green (back-left)
    //   Base2 : (0.52, -0.3, -0.3)  -- blue  (back-right)
    //
    // CullMode=BACK with FrontCounterClockwise=FALSE means front faces are
    // clockwise when viewed from the camera.  We wind each triangle CW as
    // seen from outside the pyramid.
    constexpr XMFLOAT3 apex  = {  0.0f,   0.7f,  0.0f  };
    constexpr XMFLOAT3 base0 = {  0.0f,  -0.3f,  0.6f  };
    constexpr XMFLOAT3 base1 = { -0.52f, -0.3f, -0.3f  };
    constexpr XMFLOAT3 base2 = {  0.52f, -0.3f, -0.3f  };

    constexpr XMFLOAT3 cYellow = { 1.0f, 1.0f, 0.0f };
    constexpr XMFLOAT3 cRed    = { 1.0f, 0.0f, 0.0f };
    constexpr XMFLOAT3 cGreen  = { 0.0f, 1.0f, 0.0f };
    constexpr XMFLOAT3 cBlue   = { 0.0f, 0.0f, 1.0f };

    // 4 faces x 3 vertices = 12 vertices (no index buffer).
    const Vertex pyramidVertices[] = {
        // Front face: Apex, Base2, Base0
        { apex,  cYellow }, { base2, cBlue }, { base0, cRed },
        // Right face: Apex, Base0, Base1
        { apex,  cYellow }, { base0, cRed  }, { base1, cGreen },
        // Left face:  Apex, Base1, Base2
        { apex,  cYellow }, { base1, cGreen }, { base2, cBlue },
        // Bottom face: Base0, Base2, Base1
        { base0, cRed },   { base2, cBlue },  { base1, cGreen },
    };

    const UINT vertexBufferSize = sizeof(pyramidVertices);

    // For this small, immutable triangle an upload heap is perfectly fine.
    // A default heap + copy would only add complexity with no measurable gain.
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment          = 0;
    bufferDesc.Width              = vertexBufferSize;
    bufferDesc.Height             = 1;
    bufferDesc.DepthOrArraySize   = 1;
    bufferDesc.MipLevels          = 1;
    bufferDesc.Format             = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc         = { 1, 0 };
    bufferDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(
        device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertexBuffer_)),
        "CreateCommittedResource (vertex buffer)");

    // Map, copy, unmap.
    void* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, 0 }; // We do not intend to read from this resource.
    ThrowIfFailed(
        vertexBuffer_->Map(0, &readRange, &mappedData),
        "VertexBuffer::Map");

    std::memcpy(mappedData, pyramidVertices, vertexBufferSize);
    vertexBuffer_->Unmap(0, nullptr);

    // Vertex buffer view.
    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.StrideInBytes  = sizeof(Vertex);
    vertexBufferView_.SizeInBytes    = vertexBufferSize;
}

// ===========================================================================
// Private -- depth-stencil buffer and DSV heap
// ===========================================================================

void DX12Renderer::createDepthBuffer() {
    // DSV descriptor heap (single descriptor).
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask       = 0;

    ThrowIfFailed(
        device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_)),
        "CreateDescriptorHeap (DSV)");

    // Depth buffer resource (D32_FLOAT, same dimensions as render targets).
    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment          = 0;
    depthDesc.Width              = width_;
    depthDesc.Height             = height_;
    depthDesc.DepthOrArraySize   = 1;
    depthDesc.MipLevels          = 1;
    depthDesc.Format             = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc         = { 1, 0 };
    depthDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format               = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth   = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    ThrowIfFailed(
        device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&depthBuffer_)),
        "CreateCommittedResource (depth buffer)");

    // Create the DSV.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;

    device_->CreateDepthStencilView(
        depthBuffer_.Get(), &dsvDesc,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

// ===========================================================================
// Private -- constant buffer (unused -- we use root 32-bit constants)
// ===========================================================================

void DX12Renderer::createConstantBuffer() {
    // The MVP matrix is uploaded to the GPU each frame via
    // SetGraphicsRoot32BitConstants(), so a dedicated constant buffer resource
    // is not required.  This method exists to satisfy the interface declared
    // in renderer.h and is intentionally a no-op.  The constantBuffer_ and
    // cbvDataBegin_ members remain nullptr.
}

// ===========================================================================
// Private -- fence and synchronization event
// ===========================================================================

void DX12Renderer::createFence() {
    ThrowIfFailed(
        device_->CreateFence(
            fenceValues_[frameIndex_],
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence_)),
        "CreateFence");

    ++fenceValues_[frameIndex_];

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        throw std::runtime_error("CreateEvent failed for GPU fence");
    }
}

// ===========================================================================
// Private -- readback buffer creation
// ===========================================================================

void DX12Renderer::createReadbackBuffer() {
    const uint32_t alignedPitch =
        (width_ * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    const uint64_t bufferSize =
        static_cast<uint64_t>(alignedPitch) * height_;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type                 = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment          = 0;
    bufferDesc.Width              = bufferSize;
    bufferDesc.Height             = 1;
    bufferDesc.DepthOrArraySize   = 1;
    bufferDesc.MipLevels          = 1;
    bufferDesc.Format             = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc         = { 1, 0 };
    bufferDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        ThrowIfFailed(
            device_->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&readbackBuffers_[i])),
            "CreateCommittedResource (readback buffer)");
    }

    // Separate fence for readback synchronization so we do not interfere
    // with the main rendering fence.
    ThrowIfFailed(
        device_->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&readbackFence_)),
        "CreateFence (readback)");

    readbackEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!readbackEvent_) {
        throw std::runtime_error("CreateEvent failed for readback fence");
    }
}

// ===========================================================================
// Private -- GPU -> CPU readback of the current frame
// ===========================================================================

void DX12Renderer::performReadback() {
    // Signal the readback fence after the command list has executed.
    ++readbackFenceValue_;
    ThrowIfFailed(
        commandQueue_->Signal(readbackFence_.Get(), readbackFenceValue_),
        "CommandQueue::Signal (readback)");

    // Wait until the GPU has completed the copy into the readback buffer.
    if (readbackFence_->GetCompletedValue() < readbackFenceValue_) {
        ThrowIfFailed(
            readbackFence_->SetEventOnCompletion(readbackFenceValue_, readbackEvent_),
            "Fence::SetEventOnCompletion (readback)");

        WaitForSingleObjectEx(readbackEvent_, INFINITE, FALSE);
    }

    // Map the readback buffer and copy pixel data row-by-row, stripping
    // the alignment padding that D3D12 requires on each row.
    const uint32_t actualRowBytes = width_ * 4;
    const uint32_t alignedPitch   =
        (actualRowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    void* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(alignedPitch) * height_ };
    ThrowIfFailed(
        readbackBuffers_[frameIndex_]->Map(0, &readRange, &mappedData),
        "ReadbackBuffer::Map");

    // Capture timestamp before locking the mutex to keep the critical
    // section as short as possible.
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    const uint64_t currentMicros = static_cast<uint64_t>(
        now.QuadPart * 1'000'000 / freq.QuadPart);

    {
        std::lock_guard<std::mutex> lock(frameMutex_);

        latestFrame_.width       = width_;
        latestFrame_.height      = height_;
        latestFrame_.frameNumber = ++frameNumber_;
        latestFrame_.timestamp   = currentMicros - streamStartMicros_;

        latestFrame_.pixels.resize(
            static_cast<size_t>(actualRowBytes) * height_);

        const auto* src = static_cast<const uint8_t*>(mappedData);
        uint8_t*    dst = latestFrame_.pixels.data();

        for (uint32_t row = 0; row < height_; ++row) {
            std::memcpy(dst, src, actualRowBytes);
            src += alignedPitch;
            dst += actualRowBytes;
        }

        // Stamp the FPS counter into the top-left corner of the pixel buffer.
        fpsOverlay_.renderInto(latestFrame_.pixels.data(),
                               width_, height_, actualRowBytes);
    }

    // Indicate we are done reading from the readback buffer.
    D3D12_RANGE writeRange{ 0, 0 };
    readbackBuffers_[frameIndex_]->Unmap(0, &writeRange);
}

// ===========================================================================
// Private -- GPU synchronization
// ===========================================================================

void DX12Renderer::waitForGpu() {
    // Schedule a signal command in the queue.
    ThrowIfFailed(
        commandQueue_->Signal(fence_.Get(), fenceValues_[frameIndex_]),
        "CommandQueue::Signal (waitForGpu)");

    // Wait until the fence has been processed.
    ThrowIfFailed(
        fence_->SetEventOnCompletion(fenceValues_[frameIndex_], fenceEvent_),
        "Fence::SetEventOnCompletion (waitForGpu)");

    WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    ++fenceValues_[frameIndex_];
}

void DX12Renderer::moveToNextFrame() {
    // Schedule a signal command in the queue with the current frame's fence
    // value.  This lets us know when the GPU has finished this frame.
    const uint64_t currentFenceValue = fenceValues_[frameIndex_];
    ThrowIfFailed(
        commandQueue_->Signal(fence_.Get(), currentFenceValue),
        "CommandQueue::Signal (moveToNextFrame)");

    // Update the frame index to the next back buffer.
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    // If the next frame is not yet ready to be rendered, wait until it is.
    if (fence_->GetCompletedValue() < fenceValues_[frameIndex_]) {
        ThrowIfFailed(
            fence_->SetEventOnCompletion(fenceValues_[frameIndex_], fenceEvent_),
            "Fence::SetEventOnCompletion (moveToNextFrame)");

        WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    fenceValues_[frameIndex_] = currentFenceValue + 1;
}

// ===========================================================================
// Public -- editor scene loading
// ===========================================================================

size_t DX12Renderer::loadEditorScene(std::vector<EditorNode> nodes) {
    const size_t count = nodes.size();
    {
        std::lock_guard<std::mutex> lk(editorNodesMutex_);
        editorNodes_ = std::move(nodes);
    }
    editorMode_ = true;
    return count;
}

// ===========================================================================
// Private -- line-list PSO (for grid and wireframe rendering)
// ===========================================================================

void DX12Renderer::createLinePipelineState() {
    // Compile the same shaders used by the triangle PSO -- the colorMul
    // constant buffer is shared.
    UINT compileFlags = 0;
#ifdef UZENGINE_DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompile(
        kVertexShaderSrc,
        strlen(kVertexShaderSrc),
        "VertexShader",
        nullptr, nullptr,
        "VSMain", "vs_5_0",
        compileFlags, 0,
        &vertexShader, &errors);

    if (FAILED(hr)) {
        std::string msg = "Line VS compilation failed";
        if (errors) {
            msg += ": ";
            msg += static_cast<const char*>(errors->GetBufferPointer());
        }
        throw std::runtime_error(msg);
    }

    errors.Reset();

    hr = D3DCompile(
        kPixelShaderSrc,
        strlen(kPixelShaderSrc),
        "PixelShader",
        nullptr, nullptr,
        "PSMain", "ps_5_0",
        compileFlags, 0,
        &pixelShader, &errors);

    if (FAILED(hr)) {
        std::string msg = "Line PS compilation failed";
        if (errors) {
            msg += ": ";
            msg += static_cast<const char*>(errors->GetBufferPointer());
        }
        throw std::runtime_error(msg);
    }

    // Input layout identical to the triangle PSO.
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(XMFLOAT3),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.InputLayout    = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS             = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS             = { pixelShader->GetBufferPointer(),  pixelShader->GetBufferSize()  };

    // Wireframe rasterizer, no back-face culling (lines have no faces).
    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;
    psoDesc.RasterizerState.MultisampleEnable     = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount     = 0;
    psoDesc.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Opaque blend.
    psoDesc.BlendState.AlphaToCoverageEnable  = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth testing enabled for correct layering of grid and cubes.
    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable  = FALSE;
    psoDesc.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;

    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc            = { 1, 0 };

    ThrowIfFailed(
        device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&linePipelineState_)),
        "CreateGraphicsPipelineState (line)");
}

// ===========================================================================
// Private -- ground grid vertex buffer (XZ plane, Y=0)
// ===========================================================================

void DX12Renderer::createGridVertexBuffer() {
    // UE5-style viewport grid on the XZ plane (Y=0).
    //
    // - Minor lines every 1 unit:  subtle dark gray (blends into background)
    // - Major lines every 10 units: slightly brighter gray (structural cues)
    // - X-axis (i==0, Z-parallel): red   -- world "right" direction
    // - Z-axis (i==0, X-parallel): green -- world "forward" direction
    //
    // Grid extends from -100 to +100 on both axes.
    // 201 lines per axis x 2 axes = 402 lines = 804 vertices.
    constexpr int   kHalfExtent = 100;
    constexpr int   kLineCount  = 2 * kHalfExtent + 1;   // 201 per axis
    constexpr int   kTotalVerts = kLineCount * 2 * 2;     // 804

    Vertex gridVerts[kTotalVerts];
    uint32_t idx = 0;

    const float extent = static_cast<float>(kHalfExtent);

    // Color palette matching UE5 viewport appearance.
    const XMFLOAT3 kMinorColor = {0.12f, 0.12f, 0.12f};   // barely visible
    const XMFLOAT3 kMajorColor = {0.25f, 0.25f, 0.25f};   // every 10 units
    const XMFLOAT3 kAxisX      = {0.55f, 0.10f, 0.10f};   // red (X axis)
    const XMFLOAT3 kAxisZ      = {0.10f, 0.55f, 0.10f};   // green (Z axis, Y-up engine)

    // Lines parallel to the Z axis (varying X).
    for (int i = -kHalfExtent; i <= kHalfExtent; ++i) {
        const float x = static_cast<float>(i);

        // Pick color: axis > major > minor.
        XMFLOAT3 col;
        if (i == 0) {
            col = kAxisX;           // X-axis highlight (red)
        } else if (i % 10 == 0) {
            col = kMajorColor;      // every 10th line
        } else {
            col = kMinorColor;      // every 1-unit line
        }

        gridVerts[idx++] = { {x, 0.0f, -extent}, col };
        gridVerts[idx++] = { {x, 0.0f,  extent}, col };
    }

    // Lines parallel to the X axis (varying Z).
    for (int i = -kHalfExtent; i <= kHalfExtent; ++i) {
        const float z = static_cast<float>(i);

        XMFLOAT3 col;
        if (i == 0) {
            col = kAxisZ;           // Z-axis highlight (green)
        } else if (i % 10 == 0) {
            col = kMajorColor;
        } else {
            col = kMinorColor;
        }

        gridVerts[idx++] = { {-extent, 0.0f, z}, col };
        gridVerts[idx++] = { { extent, 0.0f, z}, col };
    }

    gridVertexCount_ = idx;

    const UINT bufferSize = sizeof(Vertex) * gridVertexCount_;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment          = 0;
    bufferDesc.Width              = bufferSize;
    bufferDesc.Height             = 1;
    bufferDesc.DepthOrArraySize   = 1;
    bufferDesc.MipLevels          = 1;
    bufferDesc.Format             = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc         = { 1, 0 };
    bufferDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(
        device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&gridVertexBuffer_)),
        "CreateCommittedResource (grid VB)");

    void* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    ThrowIfFailed(
        gridVertexBuffer_->Map(0, &readRange, &mappedData),
        "GridVertexBuffer::Map");

    std::memcpy(mappedData, gridVerts, bufferSize);
    gridVertexBuffer_->Unmap(0, nullptr);

    gridVertexBufferView_.BufferLocation = gridVertexBuffer_->GetGPUVirtualAddress();
    gridVertexBufferView_.StrideInBytes  = sizeof(Vertex);
    gridVertexBufferView_.SizeInBytes    = bufferSize;
}

// ===========================================================================
// Private -- unit wireframe cube vertex buffer (12 edges, LINE_LIST)
// ===========================================================================

void DX12Renderer::createCubeWireVertexBuffer() {
    // Unit cube centered at origin: corners from (-0.5,-0.5,-0.5) to (0.5,0.5,0.5).
    // 12 edges x 2 vertices per edge = 24 vertices.
    constexpr float h = 0.5f;
    const XMFLOAT3 white = {1.0f, 1.0f, 1.0f};

    // 8 corner positions.
    const XMFLOAT3 corners[8] = {
        {-h, -h, -h},  // 0: left-bottom-back
        { h, -h, -h},  // 1: right-bottom-back
        { h,  h, -h},  // 2: right-top-back
        {-h,  h, -h},  // 3: left-top-back
        {-h, -h,  h},  // 4: left-bottom-front
        { h, -h,  h},  // 5: right-bottom-front
        { h,  h,  h},  // 6: right-top-front
        {-h,  h,  h},  // 7: left-top-front
    };

    // 12 edges defined by pairs of corner indices.
    constexpr int edges[12][2] = {
        // Back face
        {0,1}, {1,2}, {2,3}, {3,0},
        // Front face
        {4,5}, {5,6}, {6,7}, {7,4},
        // Connecting edges
        {0,4}, {1,5}, {2,6}, {3,7},
    };

    constexpr uint32_t kVertCount = 24;
    Vertex cubeVerts[kVertCount];

    for (int e = 0; e < 12; ++e) {
        cubeVerts[e * 2 + 0] = { corners[edges[e][0]], white };
        cubeVerts[e * 2 + 1] = { corners[edges[e][1]], white };
    }

    cubeWireVertexCount_ = kVertCount;

    const UINT bufferSize = sizeof(Vertex) * kVertCount;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment          = 0;
    bufferDesc.Width              = bufferSize;
    bufferDesc.Height             = 1;
    bufferDesc.DepthOrArraySize   = 1;
    bufferDesc.MipLevels          = 1;
    bufferDesc.Format             = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc         = { 1, 0 };
    bufferDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(
        device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&cubeWireVertexBuffer_)),
        "CreateCommittedResource (cube wire VB)");

    void* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    ThrowIfFailed(
        cubeWireVertexBuffer_->Map(0, &readRange, &mappedData),
        "CubeWireVertexBuffer::Map");

    std::memcpy(mappedData, cubeVerts, bufferSize);
    cubeWireVertexBuffer_->Unmap(0, nullptr);

    cubeWireVertexBufferView_.BufferLocation = cubeWireVertexBuffer_->GetGPUVirtualAddress();
    cubeWireVertexBufferView_.StrideInBytes  = sizeof(Vertex);
    cubeWireVertexBufferView_.SizeInBytes    = bufferSize;
}

} // namespace uz
