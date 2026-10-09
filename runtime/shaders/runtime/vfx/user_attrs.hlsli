// UzEngine — User-declared per-particle attribute helpers
//
// A VFX emitter can declare up to 4 StructuredBuffer<float> channels and
// 2 StructuredBuffer<float4> channels of user-defined per-particle data
// (Niagara "Parameters" parity). Each declared channel lives in the
// ping-pong bank as BOTH an SRV (read) and a UAV (write). Slot indices
// are assigned by the C++ runtime and handed to the shader through the
// spawn/update constant buffers.
//
// The helpers below wrap the bindless indirection so modules can read /
// write a user channel in a single line.
//
// All functions assume the bindless `ResourceDescriptorHeap[]` SM6.6
// semantics already exposed by the rest of the VFX shaders.

#ifndef UZ_VFX_USER_ATTRS_HLSLI
#define UZ_VFX_USER_ATTRS_HLSLI

// ---------------------------------------------------------------------------
// Bindless index tables
// ---------------------------------------------------------------------------
//
// Callers push a small fixed-size bindless table into the update CB (one
// uint per slot, UINT32_MAX meaning "unbound"). The helpers here take that
// table plus the slot index and produce the final Read/Write result.
// Keeping the table passed explicitly (rather than via a global) means a
// Spawn CS and an Update CS can share the same .hlsli but bind different
// register indices.

// ---------------------------------------------------------------------------
// Float channel helpers
// ---------------------------------------------------------------------------

/// Reads a per-particle float user attribute. `srvIdx` is typically
/// `cb.userFloatSrvIdx[slot]`. Returns 0.0 if the slot is unbound.
float UserAttr_ReadFloat(uint srvIdx, uint particleID) {
    if (srvIdx == 0xFFFFFFFFu) return 0.0f;
    StructuredBuffer<float> src = ResourceDescriptorHeap[srvIdx];
    return src[particleID];
}

/// Writes a per-particle float user attribute. `uavIdx` is typically
/// `cb.userFloatUavIdx[slot]`. No-op when the slot is unbound.
void UserAttr_WriteFloat(uint uavIdx, uint particleID, float value) {
    if (uavIdx == 0xFFFFFFFFu) return;
    RWStructuredBuffer<float> dst = ResourceDescriptorHeap[uavIdx];
    dst[particleID] = value;
}

// ---------------------------------------------------------------------------
// Vec4 channel helpers
// ---------------------------------------------------------------------------

float4 UserAttr_ReadVec4(uint srvIdx, uint particleID) {
    if (srvIdx == 0xFFFFFFFFu) return float4(0, 0, 0, 0);
    StructuredBuffer<float4> src = ResourceDescriptorHeap[srvIdx];
    return src[particleID];
}

void UserAttr_WriteVec4(uint uavIdx, uint particleID, float4 value) {
    if (uavIdx == 0xFFFFFFFFu) return;
    RWStructuredBuffer<float4> dst = ResourceDescriptorHeap[uavIdx];
    dst[particleID] = value;
}

#endif // UZ_VFX_USER_ATTRS_HLSLI
