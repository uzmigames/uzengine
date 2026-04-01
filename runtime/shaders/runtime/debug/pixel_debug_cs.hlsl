/// PixelDebug compute post-pass -- reads GBuffer + HDR at a specific pixel
/// and writes debug values to the records buffer.
///
/// @ref NVIDIA Falcor PixelDebug -- compute shader readback pattern.
///
/// Dispatched as (1,1,1) -- a single thread. No conditional writes needed,
/// so DXC cannot dead-code-eliminate the UAV stores. This replaces the inline
/// PDEBUG writes in the pixel shader which DXC aggressively optimizes away
/// because the per-pixel `if (pixel == debugPixel)` branch is provably false
/// for almost all pixels.
///
/// Root constants at b0 (10 uint32 slots):
///   [0] albedoSrvIdx    -- GBuffer RT0: rgb=albedo, a=ao
///   [1] normalSrvIdx    -- GBuffer RT1: rg=oct-normal, b=metallic, a=roughness
///   [2] depthSrvIdx     -- GBuffer depth (D32Float)
///   [3] hdrSrvIdx       -- HDR scene color after lighting
///   [4] counterUavIdx   -- PixelDebug counter UAV (RWByteAddressBuffer)
///   [5] recordsUavIdx   -- PixelDebug records UAV (RWByteAddressBuffer)
///   [6] debugPixelX     -- Target pixel X coordinate
///   [7] debugPixelY     -- Target pixel Y coordinate
///   [8] maxRecords      -- Maximum record count (overflow protection)
///   [9] emissiveSrvIdx  -- GBuffer RT3: rgb=emissive, a=packed specular+smID

cbuffer PixelDebugConstants : register(b0) {
    uint albedoSrvIdx;
    uint normalSrvIdx;
    uint depthSrvIdx;
    uint hdrSrvIdx;
    uint counterUavIdx;
    uint recordsUavIdx;
    uint debugPixelX;
    uint debugPixelY;
    uint maxRecords;
    uint emissiveSrvIdx;
};

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    int2 px = int2(debugPixelX, debugPixelY);

    // ── Read GBuffer textures at the debug pixel ────────────────────────
    Texture2D<float4> albedoTex   = ResourceDescriptorHeap[albedoSrvIdx];
    Texture2D<float4> normalTex   = ResourceDescriptorHeap[normalSrvIdx];
    Texture2D<float>  depthTex    = ResourceDescriptorHeap[depthSrvIdx];
    Texture2D<float4> hdrTex      = ResourceDescriptorHeap[hdrSrvIdx];
    Texture2D<float4> emissiveTex = ResourceDescriptorHeap[emissiveSrvIdx];

    float4 albedoRaw   = albedoTex.Load(int3(px, 0));   // RT0: rgb=albedo, a=ao
    float4 normalRaw   = normalTex.Load(int3(px, 0));    // RT1: rg=oct-normal, b=metallic, a=roughness
    float  depth       = depthTex.Load(int3(px, 0));
    float4 hdrColor    = hdrTex.Load(int3(px, 0));
    float4 emissiveRaw = emissiveTex.Load(int3(px, 0));  // RT3: rgb=emissive, a=packed

    // ── Decode GBuffer values ───────────────────────────────────────────
    float3 albedo    = albedoRaw.rgb;
    float  ao        = albedoRaw.a;
    float  metallic  = normalRaw.b;
    float  roughness = normalRaw.a;
    float3 emissive  = emissiveRaw.rgb;

    // ── Write to records buffer ─────────────────────────────────────────
    // Each record is 16 bytes: [messageId:u32, value:f32, pixelX:u32, pixelY:u32]
    // Reserve 13 slots atomically to avoid interleaving with other writers.
    RWByteAddressBuffer counter = ResourceDescriptorHeap[counterUavIdx];
    RWByteAddressBuffer records = ResourceDescriptorHeap[recordsUavIdx];

    uint baseSlot;
    counter.InterlockedAdd(0, 13u, baseSlot);

    uint dpX = debugPixelX;
    uint dpY = debugPixelY;

    // id=0: depth
    if (baseSlot + 0 < maxRecords) { uint o = (baseSlot + 0) * 16; records.Store(o, 0u); records.Store(o + 4, asuint(depth));        records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    // id=1: ambient occlusion
    if (baseSlot + 1 < maxRecords) { uint o = (baseSlot + 1) * 16; records.Store(o, 1u); records.Store(o + 4, asuint(ao));           records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    // id=2,3,4: albedo RGB
    if (baseSlot + 2 < maxRecords) { uint o = (baseSlot + 2) * 16; records.Store(o, 2u); records.Store(o + 4, asuint(albedo.x));     records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    if (baseSlot + 3 < maxRecords) { uint o = (baseSlot + 3) * 16; records.Store(o, 3u); records.Store(o + 4, asuint(albedo.y));     records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    if (baseSlot + 4 < maxRecords) { uint o = (baseSlot + 4) * 16; records.Store(o, 4u); records.Store(o + 4, asuint(albedo.z));     records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    // id=5,6,7: HDR color RGB
    if (baseSlot + 5 < maxRecords) { uint o = (baseSlot + 5) * 16; records.Store(o, 5u); records.Store(o + 4, asuint(hdrColor.x));   records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    if (baseSlot + 6 < maxRecords) { uint o = (baseSlot + 6) * 16; records.Store(o, 6u); records.Store(o + 4, asuint(hdrColor.y));   records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    if (baseSlot + 7 < maxRecords) { uint o = (baseSlot + 7) * 16; records.Store(o, 7u); records.Store(o + 4, asuint(hdrColor.z));   records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    // id=8: roughness
    if (baseSlot + 8 < maxRecords) { uint o = (baseSlot + 8) * 16; records.Store(o, 8u); records.Store(o + 4, asuint(roughness));    records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    // id=9: metallic
    if (baseSlot + 9 < maxRecords) { uint o = (baseSlot + 9) * 16; records.Store(o, 9u); records.Store(o + 4, asuint(metallic));     records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    // id=10,11,12: emissive RGB
    if (baseSlot + 10 < maxRecords) { uint o = (baseSlot + 10) * 16; records.Store(o, 10u); records.Store(o + 4, asuint(emissive.x)); records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    if (baseSlot + 11 < maxRecords) { uint o = (baseSlot + 11) * 16; records.Store(o, 11u); records.Store(o + 4, asuint(emissive.y)); records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
    if (baseSlot + 12 < maxRecords) { uint o = (baseSlot + 12) * 16; records.Store(o, 12u); records.Store(o + 4, asuint(emissive.z)); records.Store(o + 8, dpX); records.Store(o + 12, dpY); }
}
