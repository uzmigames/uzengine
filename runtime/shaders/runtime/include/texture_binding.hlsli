/// texture_binding.hlsli — Unified texture access for bindless (SM6.6+) and register (SM5) paths.
///
/// Bindless mode (UZ_BINDLESS=1): textures accessed via ResourceDescriptorHeap[index].
/// Register mode (default):       textures bound to explicit register(tN) slots.
///
/// # Usage Pattern
/// Most UzEngine shaders already use per-shader #ifdef UZ_BINDLESS guards with
/// register-based fallback texture declarations in the #else block. This header
/// provides common macros for shaders that want a unified sampling interface.
///
/// For the register path, each shader declares its own Texture2D/TextureCube
/// at known register(tN) slots. The C++ backend (DX11 or non-bindless DX12)
/// binds textures at those slots to match.
///
/// # Macro Reference
/// UZ_SAMPLE_TEX2D(srvIndex, samp, uv)              — Sample Texture2D with default mip
/// UZ_SAMPLE_TEX2D_LOD(srvIndex, samp, uv, lod)     — SampleLevel on Texture2D
/// UZ_SAMPLE_TEX2D_BIAS(srvIndex, samp, uv, bias)   — SampleBias on Texture2D
/// UZ_SAMPLE_TEX2D_GRAD(srvIndex, samp, uv, ddx, ddy) — SampleGrad on Texture2D
/// UZ_SAMPLE_TEX2D_CMP(srvIndex, cmpSamp, uv, ref)  — SampleCmpLevelZero on Texture2D
/// UZ_LOAD_TEX2D(srvIndex, coord)                    — Load (integer coords) on Texture2D
/// UZ_SAMPLE_TEXCUBE(srvIndex, samp, dir)            — Sample TextureCube
/// UZ_SAMPLE_TEXCUBE_LOD(srvIndex, samp, dir, lod)   — SampleLevel on TextureCube
/// UZ_SAMPLE_TEX3D(srvIndex, samp, uvw)              — Sample Texture3D
/// UZ_SAMPLE_TEX3D_LOD(srvIndex, samp, uvw, lod)     — SampleLevel on Texture3D

#ifndef TEXTURE_BINDING_HLSLI
#define TEXTURE_BINDING_HLSLI

#ifdef UZ_BINDLESS

// ─────────────────────────────────────────────────────────────────────────────
// Bindless path (SM6.6+): dynamic indexing into the descriptor heap.
// The srvIndex is a uint index into ResourceDescriptorHeap[].
// ─────────────────────────────────────────────────────────────────────────────

#define UZ_SAMPLE_TEX2D(srvIndex, samp, uv) \
    ResourceDescriptorHeap[srvIndex].Sample(samp, uv)

#define UZ_SAMPLE_TEX2D_LOD(srvIndex, samp, uv, lod) \
    ResourceDescriptorHeap[srvIndex].SampleLevel(samp, uv, lod)

#define UZ_SAMPLE_TEX2D_BIAS(srvIndex, samp, uv, bias) \
    ResourceDescriptorHeap[srvIndex].SampleBias(samp, uv, bias)

#define UZ_SAMPLE_TEX2D_GRAD(srvIndex, samp, uv, ddx, ddy) \
    ResourceDescriptorHeap[srvIndex].SampleGrad(samp, uv, ddx, ddy)

#define UZ_SAMPLE_TEX2D_CMP(srvIndex, cmpSamp, uv, ref) \
    ResourceDescriptorHeap[srvIndex].SampleCmpLevelZero(cmpSamp, uv, ref)

#define UZ_LOAD_TEX2D(srvIndex, coord) \
    ResourceDescriptorHeap[srvIndex].Load(coord)

#define UZ_SAMPLE_TEXCUBE(srvIndex, samp, dir) \
    ResourceDescriptorHeap[srvIndex].Sample(samp, dir)

#define UZ_SAMPLE_TEXCUBE_LOD(srvIndex, samp, dir, lod) \
    ResourceDescriptorHeap[srvIndex].SampleLevel(samp, dir, lod)

#define UZ_SAMPLE_TEX3D(srvIndex, samp, uvw) \
    ResourceDescriptorHeap[srvIndex].Sample(samp, uvw)

#define UZ_SAMPLE_TEX3D_LOD(srvIndex, samp, uvw, lod) \
    ResourceDescriptorHeap[srvIndex].SampleLevel(samp, uvw, lod)

#else

// ─────────────────────────────────────────────────────────────────────────────
// Register path (SM5 / non-bindless): first argument is the texture object
// itself (e.g., a Texture2D declared at register(tN)), not a descriptor index.
// ─────────────────────────────────────────────────────────────────────────────

#define UZ_SAMPLE_TEX2D(tex, samp, uv) \
    (tex).Sample(samp, uv)

#define UZ_SAMPLE_TEX2D_LOD(tex, samp, uv, lod) \
    (tex).SampleLevel(samp, uv, lod)

#define UZ_SAMPLE_TEX2D_BIAS(tex, samp, uv, bias) \
    (tex).SampleBias(samp, uv, bias)

#define UZ_SAMPLE_TEX2D_GRAD(tex, samp, uv, ddx, ddy) \
    (tex).SampleGrad(samp, uv, ddx, ddy)

#define UZ_SAMPLE_TEX2D_CMP(tex, cmpSamp, uv, ref) \
    (tex).SampleCmpLevelZero(cmpSamp, uv, ref)

#define UZ_LOAD_TEX2D(tex, coord) \
    (tex).Load(coord)

#define UZ_SAMPLE_TEXCUBE(tex, samp, dir) \
    (tex).Sample(samp, dir)

#define UZ_SAMPLE_TEXCUBE_LOD(tex, samp, dir, lod) \
    (tex).SampleLevel(samp, dir, lod)

#define UZ_SAMPLE_TEX3D(tex, samp, uvw) \
    (tex).Sample(samp, uvw)

#define UZ_SAMPLE_TEX3D_LOD(tex, samp, uvw, lod) \
    (tex).SampleLevel(samp, uvw, lod)

#endif // UZ_BINDLESS

#endif // TEXTURE_BINDING_HLSLI
