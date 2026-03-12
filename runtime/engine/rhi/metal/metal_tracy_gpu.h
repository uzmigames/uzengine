#pragma once

/// Tracy Metal GPU Profiling — C++ Interface
///
/// Provides a C-style function interface to isolate the ARC requirement of
/// `TracyMetal.hmm` behind a single `.mm` translation unit. All other Metal
/// backend files compile without ARC (`-fno-objc-arc`), while this module's
/// implementation (`metal_tracy_gpu.mm`) compiles with `-fobjc-arc`.
///
/// # Architecture
/// Tracy's `MetalZoneScope` is an RAII object that must live between the
/// render pass descriptor configuration and the encoder's end. Since we
/// cannot use RAII across function boundaries (Begin/End are separate calls),
/// we heap-allocate the zone scope and manage its lifetime manually.
///
/// # Usage
/// ```cpp
/// // In MetalDevice::Init()
/// void* ctx = MetalTracyCreate(device);
///
/// // In MetalCommandList::BeginRenderPass()
/// MetalTracyZoneBegin(ctx, rpDesc, "GBuffer");
///
/// // In MetalCommandList::EndRenderPass()
/// MetalTracyZoneEnd(ctx);
///
/// // In MetalCommandList::Execute() or once per frame
/// MetalTracyCollect(ctx);
///
/// // In MetalDevice::~MetalDevice()
/// MetalTracyDestroy(ctx);
/// ```

#include <cstdint>

namespace uz {

/// Creates a Tracy Metal GPU profiling context.
///
/// Initializes `tracy::MetalCtx` with the given Metal device. Returns nullptr if:
/// - Tracy is not enabled (`TRACY_ENABLE` not defined).
/// - The device does not support counter sampling.
/// - TracyMetal.hmm is not available at compile time.
///
/// # Arguments
/// * `mtlDevice` — `id<MTLDevice>` cast to `void*` via `__bridge`. Must not be null.
///
/// # Returns
/// Opaque pointer to the Tracy Metal context, or nullptr on failure/disabled.
///
/// # Thread Safety
/// Must be called from the main thread during device initialization.
void* MetalTracyCreate(void* mtlDevice);

/// Destroys a Tracy Metal GPU profiling context.
///
/// Collects any remaining pending timestamps before destruction. Safe to call
/// with nullptr (no-op).
///
/// # Arguments
/// * `ctx` — Context returned by `MetalTracyCreate()`, or nullptr.
void MetalTracyDestroy(void* ctx);

/// Collects resolved GPU timestamps and sends them to the Tracy server.
///
/// Must be called once per frame (typically in `Execute()` or at frame end).
/// Internally calls `tracy::MetalCtx::Collect()`. No-op if ctx is nullptr.
///
/// # Arguments
/// * `ctx` — Context returned by `MetalTracyCreate()`, or nullptr.
void MetalTracyCollect(void* ctx);

/// Begins a Tracy GPU zone by attaching counter queries to a render pass descriptor.
///
/// Creates a heap-allocated `tracy::MetalZoneScope` that configures
/// `sampleBufferAttachments[0]` on the descriptor. The zone remains open until
/// `MetalTracyZoneEnd()` is called.
///
/// # Arguments
/// * `ctx` — Context returned by `MetalTracyCreate()`. Must not be null.
/// * `rpDesc` — `MTLRenderPassDescriptor*` cast to `void*` via `__bridge`. Must not be null.
/// * `name` — Zone name displayed in Tracy UI (string literal, not copied by this function).
///
/// # Preconditions
/// - No zone is currently active (i.e., `MetalTracyZoneEnd()` was called for the previous zone).
/// - Must be called BEFORE creating the render command encoder from the descriptor.
void MetalTracyZoneBegin(void* ctx, void* rpDesc, const char* name);

/// Ends the currently active Tracy GPU zone.
///
/// Destroys the heap-allocated `MetalZoneScope`, which submits the end-of-zone
/// GPU timestamp to Tracy. No-op if no zone is active or ctx is nullptr.
///
/// # Arguments
/// * `ctx` — Context returned by `MetalTracyCreate()`. Must not be null.
void MetalTracyZoneEnd(void* ctx);

} // namespace uz
