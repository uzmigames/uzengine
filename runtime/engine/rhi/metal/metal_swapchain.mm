/// Metal Swapchain Implementation
///
/// CAMetalLayer-based presentation with triple buffering. Drawables are
/// transient — each frame acquires a new drawable via `nextDrawable` and
/// registers its texture in the device texture pool as a non-owning entry.
///
/// Unlike Vulkan/DX12 where swapchain images are persistent objects acquired
/// by index, Metal's drawable pool is managed internally by CAMetalLayer.
/// The layer handles buffer rotation, and the drawable's texture is only
/// valid for the duration of a single frame (acquire -> present).

#include "metal_swapchain.h"
#include "metal_device.h"
#include "metal_resources.h"

#include "log.h"
#include "profiler.h"

#if defined(__APPLE__)
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#if TARGET_OS_OSX
#import <AppKit/NSWindow.h>
#import <AppKit/NSView.h>
#endif
#endif

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MetalSwapChain::~MetalSwapChain() {
    UZ_PROFILE_FUNCTION();

    if (!m_device) return;

    DestroyBackBufferHandles();

#if defined(__APPLE__)
    // Release the current drawable if one is held
    if (m_currentDrawable) {
        id<CAMetalDrawable> drawable = (__bridge_transfer id<CAMetalDrawable>)m_currentDrawable;
        m_currentDrawable = nullptr;
        (void)drawable; // ARC releases on scope exit
    }

    // Release the retained CAMetalLayer
    if (m_layer) {
        CAMetalLayer* layer = (__bridge_transfer CAMetalLayer*)m_layer;
        m_layer = nullptr;
        (void)layer; // ARC releases on scope exit
    }
#endif

    UZ_LOG(MetalRHI, Info, "MetalSwapChain destroyed");
}

bool MetalSwapChain::Init(MetalDevice* device, const RHISwapChainDesc& desc) {
    UZ_PROFILE_FUNCTION();

    if (!device) {
        UZ_LOG(MetalRHI, Error, "MetalSwapChain::Init -- null device");
        return false;
    }
    if (!desc.windowHandle) {
        UZ_LOG(MetalRHI, Error, "MetalSwapChain::Init -- null window handle");
        return false;
    }

    m_device = device;
    m_width = desc.width;
    m_height = desc.height;
    m_format = desc.format;

#if defined(__APPLE__) && TARGET_OS_OSX
    // The window handle is an NSWindow* on macOS
    NSWindow* window = (__bridge NSWindow*)desc.windowHandle;
    if (!window) {
        UZ_LOG(MetalRHI, Error, "MetalSwapChain::Init -- invalid NSWindow handle");
        return false;
    }

    NSView* contentView = [window contentView];
    if (!contentView) {
        UZ_LOG(MetalRHI, Error, "MetalSwapChain::Init -- NSWindow has no contentView");
        return false;
    }

    // Enable layer-backing on the content view
    [contentView setWantsLayer:YES];

    // Create and configure the CAMetalLayer
    CAMetalLayer* layer = [CAMetalLayer layer];
    if (!layer) {
        UZ_LOG(MetalRHI, Error, "MetalSwapChain::Init -- failed to create CAMetalLayer");
        return false;
    }

    // Bridge MTL::Device* to id<MTLDevice> for layer assignment
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device->GetMTLDevice();
    layer.device = mtlDevice;

    // Configure pixel format from RHI format.
    // CAMetalLayer only supports BGRA variants (BGRA8Unorm, BGRA8Unorm_sRGB,
    // RGBA16Float, BGR10A2Unorm). RGBA8Unorm is silently ignored and falls back
    // to BGRA8Unorm, causing a format mismatch with pipeline state objects.
    // Remap RGBA→BGRA here so the stored format matches the actual drawable texture.
    RHIFormat actualFormat = desc.format;
    if (actualFormat == RHIFormat::RGBA8Unorm) actualFormat = RHIFormat::BGRA8Unorm;
    if (actualFormat == RHIFormat::RGBA8Srgb)  actualFormat = RHIFormat::BGRA8Srgb;
    m_format = actualFormat;

    uint32_t mtlPixelFormat = ToMTLPixelFormat(actualFormat);
    layer.pixelFormat = static_cast<MTLPixelFormat>(mtlPixelFormat);

    // Set drawable dimensions
    layer.drawableSize = CGSizeMake(
        static_cast<CGFloat>(desc.width),
        static_cast<CGFloat>(desc.height));

    // Triple buffering — Metal supports 2 or 3 drawables
    uint32_t bufferCount = desc.bufferCount;
    if (bufferCount < 2) bufferCount = 2;
    if (bufferCount > 3) bufferCount = 3;
    layer.maximumDrawableCount = bufferCount;

    // VSync control
    layer.displaySyncEnabled = desc.vsync ? YES : NO;

    // Optimize for render target only (no CPU readback from drawable textures)
    layer.framebufferOnly = YES;

    // Attach the layer to the content view
    [contentView setLayer:layer];

    // Retain the layer via __bridge_retained so it outlives this scope
    m_layer = (__bridge_retained void*)layer;

    UZ_LOG(MetalRHI, Info,
           "MetalSwapChain created: {}x{}, format={}, buffers={}, vsync={}",
           m_width, m_height, static_cast<uint32_t>(m_format),
           bufferCount, desc.vsync);

    return true;
#else
    UZ_LOG(MetalRHI, Error,
           "MetalSwapChain::Init -- Metal swapchain not supported on this platform");
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain — Back Buffer Acquisition
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle MetalSwapChain::GetCurrentBackBuffer() {
    UZ_PROFILE_FUNCTION();

    if (!m_device || !m_layer) {
        UZ_LOG(MetalRHI, Error, "MetalSwapChain::GetCurrentBackBuffer -- not initialized");
        return RHITextureHandle::Invalid();
    }

#if defined(__APPLE__)
    CAMetalLayer* layer = (__bridge CAMetalLayer*)m_layer;

    // Release the previous drawable if one is still held.
    // This can happen if GetCurrentBackBuffer is called multiple times
    // without a Present in between (e.g., during resize).
    if (m_currentDrawable) {
        id<CAMetalDrawable> oldDrawable = (__bridge_transfer id<CAMetalDrawable>)m_currentDrawable;
        m_currentDrawable = nullptr;
        (void)oldDrawable;
    }

    // Acquire the next drawable from the layer's internal pool.
    // This may block if all drawables are in-flight (back pressure).
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) {
        UZ_LOG(MetalRHI, Error,
               "MetalSwapChain::GetCurrentBackBuffer -- nextDrawable returned nil "
               "(all drawables in-flight or timeout)");
        return RHITextureHandle::Invalid();
    }

    // Retain the drawable so it survives until Present()
    m_currentDrawable = (__bridge_retained void*)drawable;

    // Get the drawable's texture — this is a transient texture valid for one frame
    id<MTLTexture> drawableTexture = drawable.texture;
    if (!drawableTexture) {
        UZ_LOG(MetalRHI, Error,
               "MetalSwapChain::GetCurrentBackBuffer -- drawable has nil texture");
        return RHITextureHandle::Invalid();
    }

    // Free any existing handle at this back buffer index before reuse
    uint32_t idx = m_currentBackBufferIndex;
    if (m_backBufferHandles[idx].IsValid()) {
        auto* slot = m_device->GetTexturePool().Get(
            m_backBufferHandles[idx].index,
            m_backBufferHandles[idx].generation);
        if (slot) {
            slot->texture = nullptr;
        }
        m_device->GetTexturePool().Free(m_backBufferHandles[idx].index);
        m_backBufferHandles[idx] = RHITextureHandle::Invalid();
    }

    // Register the drawable's texture in the device texture pool.
    // ownsTexture = false because the drawable pool manages the texture lifetime.
    uint32_t generation = 0;
    uint32_t poolIndex = m_device->GetTexturePool().Allocate(generation);
    auto* slot = m_device->GetTexturePool().Get(poolIndex, generation);
    if (slot) {
        slot->texture = (__bridge MTL::Texture*)drawableTexture;
        slot->srvIndex = UINT32_MAX;       // Swap chain textures are not bindless SRVs
        slot->rtvIndex = UINT32_MAX;
        slot->dsvIndex = UINT32_MAX;
        slot->generation = generation;
        slot->ownsTexture = false;         // Drawable pool owns the texture
        slot->desc.width = m_width;
        slot->desc.height = m_height;
        slot->desc.format = m_format;
        slot->desc.usage = RHITextureUsage::RenderTarget;
        slot->desc.depthOrArraySize = 1;
        slot->desc.mipLevels = 1;
        slot->desc.debugName = "SwapChainBackBuffer";
        m_backBufferHandles[idx] = {poolIndex, generation};
    } else {
        UZ_LOG(MetalRHI, Error,
               "MetalSwapChain::GetCurrentBackBuffer -- failed to allocate texture pool slot");
        return RHITextureHandle::Invalid();
    }

    return m_backBufferHandles[idx];
#else
    return RHITextureHandle::Invalid();
#endif
}

uint32_t MetalSwapChain::GetCurrentBackBufferIndex() const {
    return m_currentBackBufferIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain — Presentation
// ─────────────────────────────────────────────────────────────────────────────

void MetalSwapChain::Present(bool vsync) {
    UZ_PROFILE_FUNCTION();

    if (!m_device || !m_layer) return;

#if defined(__APPLE__)
    // Update VSync on-the-fly — Metal allows toggling displaySyncEnabled per frame
    CAMetalLayer* layer = (__bridge CAMetalLayer*)m_layer;
    layer.displaySyncEnabled = vsync ? YES : NO;

    // Present the current drawable.
    // Note: On macOS, the Window Server compositor controls presentation timing.
    // Even with displaySyncEnabled=NO, nextDrawable blocks when all drawables
    // are in-flight, throttling to the display refresh rate (60 Hz / 120 Hz).
    // This is a macOS platform limitation, not a bug.
    if (m_currentDrawable) {
        id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)m_currentDrawable;
        [drawable present];

        // Release our retained reference — the presentation engine takes ownership
        id<CAMetalDrawable> released = (__bridge_transfer id<CAMetalDrawable>)m_currentDrawable;
        m_currentDrawable = nullptr;
        (void)released;
    } else {
        UZ_LOG(MetalRHI, Warning,
               "MetalSwapChain::Present -- no drawable acquired (GetCurrentBackBuffer not called?)");
    }

    // Advance the back buffer index for the next frame
    m_currentBackBufferIndex = (m_currentBackBufferIndex + 1) % kMaxBackBuffers;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain — Resize
// ─────────────────────────────────────────────────────────────────────────────

void MetalSwapChain::Resize(uint32_t width, uint32_t height) {
    UZ_PROFILE_FUNCTION();

    // Skip resize for minimized windows (zero dimensions)
    if (width == 0 || height == 0) return;

    if (!m_device || !m_layer) {
        UZ_LOG(MetalRHI, Error, "MetalSwapChain::Resize -- not initialized");
        return;
    }

#if defined(__APPLE__)
    // Free existing back buffer handles — drawable textures from previous
    // size are no longer valid after resize
    DestroyBackBufferHandles();

    // Release any in-flight drawable — it was for the old size
    if (m_currentDrawable) {
        id<CAMetalDrawable> drawable = (__bridge_transfer id<CAMetalDrawable>)m_currentDrawable;
        m_currentDrawable = nullptr;
        (void)drawable;
    }

    // Update the layer's drawable size — CAMetalLayer handles the rest
    CAMetalLayer* layer = (__bridge CAMetalLayer*)m_layer;
    layer.drawableSize = CGSizeMake(
        static_cast<CGFloat>(width),
        static_cast<CGFloat>(height));

    m_width = width;
    m_height = height;
    m_currentBackBufferIndex = 0;

    UZ_LOG(MetalRHI, Info, "MetalSwapChain resized to {}x{}", m_width, m_height);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain — Getters
// ─────────────────────────────────────────────────────────────────────────────

uint32_t MetalSwapChain::GetWidth() const {
    return m_width;
}

uint32_t MetalSwapChain::GetHeight() const {
    return m_height;
}

RHIFormat MetalSwapChain::GetFormat() const {
    return m_format;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Releases all back buffer texture pool entries.
///
/// Called during Resize() and destruction to clean up stale texture pool
/// slots. The actual Metal textures are owned by the drawable pool and
/// are not released here.
void MetalSwapChain::DestroyBackBufferHandles() {
    for (uint32_t i = 0; i < kMaxBackBuffers; ++i) {
        if (m_backBufferHandles[i].IsValid()) {
            auto* slot = m_device->GetTexturePool().Get(
                m_backBufferHandles[i].index,
                m_backBufferHandles[i].generation);
            if (slot) {
                slot->texture = nullptr;
            }
            m_device->GetTexturePool().Free(m_backBufferHandles[i].index);
            m_backBufferHandles[i] = RHITextureHandle::Invalid();
        }
    }
}

} // namespace uz
