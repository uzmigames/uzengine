/// UzEngine Root Parameter Slot Definitions
///
/// Named constants for root signature parameter slots used across all render
/// passes. Eliminates magic integers and documents the binding convention in
/// a single authoritative location.
///
/// # Root Signature Layout (Graphics)
/// | Slot | Register | Usage                                              |
/// |------|----------|----------------------------------------------------|
/// |  0   | b0       | Root constants (descriptor indices / debug mode)   |
/// |  1   | b1       | PerFrameCB (camera, time, lighting — per frame)    |
/// |  2   | b2       | PerObjectCB / LightingCB (per draw or fullscreen)  |
/// |  3   | b4       | ShadowDataCB / ShadowCB (per cascade or per frame) |
///
/// # Usage
/// ```cpp
/// cmd->SetRootConstants(kSlotRootConstants, indices, 4);
/// cmd->SetRootCBV(kSlotPerFrame, alloc.bufferHandle, ...);
/// cmd->SetRootCBV(kSlotPerObject, alloc.bufferHandle, ...);
/// cmd->SetRootCBV(kSlotShadow, alloc.bufferHandle, ...);
/// ```

#pragma once

#include <cstdint>

namespace uz {

/// Root parameter slot 0 — root constants (descriptor indices, debug mode selector).
static constexpr uint32_t kSlotRootConstants = 0;

/// Root parameter slot 1 — per-frame constant buffer (PerFrameCB at register b1).
static constexpr uint32_t kSlotPerFrame = 1;

/// Root parameter slot 2 — per-object constant buffer or lighting CB (register b2).
static constexpr uint32_t kSlotPerObject = 2;

/// Root parameter slot 3 — shadow data constant buffer (ShadowDataCB/ShadowCB at register b4).
static constexpr uint32_t kSlotShadow = 3;

} // namespace uz
