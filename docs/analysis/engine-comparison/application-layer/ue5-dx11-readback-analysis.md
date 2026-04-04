# UE5.6 DX11 Texture Readback Pattern Analysis

**Status:** Reference Implementation Study
**Date:** 2026-03-12
**Target:** UzEngine DX11 RHI screenshot/readback architecture

---

## Executive Summary

This document analyzes UE5.6's DX11 RHI implementation for GPU-to-CPU texture readback (screenshot capture, surface inspection). The pattern is straightforward but contains important sync and resource management details that UzEngine should adapt.

**Key insight:** UE5 avoids creating persistent staging textures. Instead, it creates temporary staging textures on-demand and destroys them after readback. This keeps GPU VRAM clean but adds CPU-side allocation overhead per readback.

---

## 1. Pattern: Staging Texture vs Staging Buffer

### UE5 Decision: STAGING TEXTURE (not buffer)

**Source:** `W:/UE_5.6_Source/Windows/D3D11RHI/Private/D3D11RenderTarget.cpp`, lines 296-369

UE5 uses a **D3D11_TEXTURE2D** with `D3D11_USAGE_STAGING`, not a buffer. The staging texture descriptor is:

```cpp
// Lines 326-338 from GetStagingTexture()
D3D11_TEXTURE2D_DESC Desc;
ZeroMemory(&Desc, sizeof(D3D11_TEXTURE2D_DESC));
Desc.Width = SizeX;                           // Exact rect width needed
Desc.Height = SizeY;                          // Exact rect height needed
Desc.MipLevels = 1;
Desc.ArraySize = 1;
Desc.Format = SourceDesc.Format;              // Match source format exactly
Desc.SampleDesc.Count = 1;                    // Always non-MSAA
Desc.SampleDesc.Quality = 0;
Desc.Usage = D3D11_USAGE_STAGING;             // CPU-readable, GPU-writable
Desc.BindFlags = 0;                           // No GPU pipeline binding
Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;  // CPU-side read permission
Desc.MiscFlags = 0;
```

**Why texture, not buffer?**
- D3D11 `CopySubresourceRegion` is optimized for texture-to-texture copies
- Format preservation is automatic (no need to calculate row pitch manually)
- Sub-region copying via `D3D11_BOX` is cleaner
- Driver optimizations for texture-specific paths

---

## 2. The Readback Flow: Non-MSAA Case

### Function Chain

```
RHIReadSurfaceData()
  ↓
ReadSurfaceDataNoMSAARaw()
  ↓
GetStagingTexture()
  ├─ Check: is source already USAGE_STAGING?
  │  └─ YES: return source directly (fast path)
  │  └─ NO: create temp staging texture
  ├─ CopySubresourceRegion() [GPU copy]
  └─ return staging texture
  ↓
Direct3DDeviceIMContext->Map()
  ↓
memcpy with RowPitch accounting
  ↓
Direct3DDeviceIMContext->Unmap()
```

### Detailed Implementation

**GetStagingTexture() (lines 296-369):**

```cpp
TRefCountPtr<ID3D11Texture2D> FD3D11DynamicRHI::GetStagingTexture(
    FRHITexture* TextureRHI,
    FIntRect InRect,
    FIntRect& StagingRectOUT,
    FReadSurfaceDataFlags InFlags)
{
    FD3D11Texture* Texture = ResourceCast(TextureRHI);

    D3D11_TEXTURE2D_DESC SourceDesc;
    Texture->GetD3D11Texture2D()->GetDesc(&SourceDesc);

    // FAST PATH: if source is already staging, reuse it
    bool bRequiresTempStagingTexture = SourceDesc.Usage != D3D11_USAGE_STAGING;
    if (bRequiresTempStagingTexture == false)
    {
        StagingRectOUT = InRect;
        return Texture->GetD3D11Texture2D();
    }

    // SLOW PATH: create temporary staging texture
    int32 SizeX = InRect.Width();
    int32 SizeY = InRect.Height();

    // Set up region box for CopySubresourceRegion
    D3D11_BOX Rect;
    Rect.left = InRect.Min.X;
    Rect.top = InRect.Min.Y;
    Rect.right = InRect.Max.X;
    Rect.bottom = InRect.Max.Y;
    Rect.front = 0;
    Rect.back = 1;

    // Create staging texture (see Desc above)
    // ...
    TRefCountPtr<ID3D11Texture2D> TempTexture2D;
    VERIFYD3D11RESULT_EX(
        Direct3DDevice->CreateTexture2D(&Desc, NULL, TempTexture2D.GetInitReference()),
        Direct3DDevice
    );

    // Compute subresource index (handles cubemaps, arrays, mips)
    uint32 Subresource = 0;
    if (SourceDesc.MiscFlags == D3D11_RESOURCE_MISC_TEXTURECUBE)
    {
        uint32 D3DFace = GetD3D11CubeFace(InFlags.GetCubeFace());
        Subresource = D3D11CalcSubresource(
            InFlags.GetMip(),
            InFlags.GetArrayIndex() * 6 + D3DFace,
            TextureRHI->GetNumMips()
        );
    }
    else
    {
        const bool bIsTextureArray = Texture->GetDesc().IsTextureArray();
        Subresource = D3D11CalcSubresource(
            InFlags.GetMip(),
            bIsTextureArray ? InFlags.GetArrayIndex() : 0,
            TextureRHI->GetNumMips()
        );
    }

    // Use D3D11_BOX only if reading partial region (driver prefers NULL for whole texture)
    D3D11_BOX* RectPtr = NULL;
    if (Rect.left != 0 || Rect.top != 0 ||
        Rect.right != SourceDesc.Width || Rect.bottom != SourceDesc.Height)
    {
        RectPtr = &Rect;
    }

    // CRITICAL: Copy from source to staging
    Direct3DDeviceIMContext->CopySubresourceRegion(
        TempTexture2D, 0,                      // Dest: staging texture, subresource 0
        0, 0, 0,                               // Dest offset (always 0,0,0 for temp)
        Texture->GetResource(),                // Source
        Subresource,                           // Source subresource (mip/array/face)
        RectPtr                                // Source region (NULL = whole texture)
    );

    // Return staging texture; caller will Map/Unmap it
    return TempTexture2D;
}
```

**ReadSurfaceDataNoMSAARaw() (lines 371-410):**

```cpp
void FD3D11DynamicRHI::ReadSurfaceDataNoMSAARaw(
    FRHITexture* TextureRHI,
    FIntRect InRect,
    TArray<uint8>& OutData,
    FReadSurfaceDataFlags InFlags)
{
    // ... validation ...

    FD3D11Texture* Texture = ResourceCast(TextureRHI);

    const uint32 SizeX = InRect.Width();
    const uint32 SizeY = InRect.Height();

    D3D11_TEXTURE2D_DESC TextureDesc;
    Texture->GetD3D11Texture2D()->GetDesc(&TextureDesc);

    uint32 BytesPerPixel = D3D11RT_ComputeBytesPerPixel(TextureDesc.Format);

    // Pre-allocate output buffer
    OutData.SetNumUninitialized(SizeX * SizeY * BytesPerPixel);

    // Get staging texture (may be temp or source itself)
    bool bIsUsingTempStagingTexture = TextureDesc.Usage != D3D11_USAGE_STAGING;
    FIntRect StagingRect;
    TRefCountPtr<ID3D11Texture2D> TempTexture2D =
        GetStagingTexture(TextureRHI, InRect, StagingRect, InFlags);

    // CRITICAL SYNC POINT: Map blocking call
    // This implicitly stalls CPU until GPU finishes outstanding commands
    uint32 MappedSubresource = bIsUsingTempStagingTexture ? 0 : InFlags.GetMip();
    D3D11_MAPPED_SUBRESOURCE LockedRect;
    VERIFYD3D11RESULT_EX(
        Direct3DDeviceIMContext->Map(
            TempTexture2D,
            MappedSubresource,
            D3D11_MAP_READ,       // Read-only access
            0,                    // Flags (0 = synchronous, block if needed)
            &LockedRect
        ),
        Direct3DDevice
    );

    // Copy from GPU-memory pointer to CPU-owned buffer
    uint32 BytesPerLine = BytesPerPixel * InRect.Width();
    uint8* DestPtr = OutData.GetData();
    uint8* SrcPtr = (uint8*)LockedRect.pData
                    + StagingRect.Min.X * BytesPerPixel
                    + StagingRect.Min.Y * LockedRect.RowPitch;

    for (uint32 Y = 0; Y < SizeY; Y++)
    {
        memcpy(DestPtr, SrcPtr, BytesPerLine);
        DestPtr += BytesPerLine;
        SrcPtr += LockedRect.RowPitch;  // RowPitch accounts for alignment padding
    }

    // Release GPU resource
    Direct3DDeviceIMContext->Unmap(TempTexture2D, MappedSubresource);
}
```

---

## 3. Command List Execution & Synchronization

### NO Explicit Flush

**Critical observation:** UE5 does NOT call `Flush()` before `Map()`.

```cpp
// From GetStagingTexture(), line 366:
Direct3DDeviceIMContext->CopySubresourceRegion(...);

// Later, from ReadSurfaceDataNoMSAARaw(), line 397:
Direct3DDeviceIMContext->Map(TempTexture2D, ..., D3D11_MAP_READ, 0, ...);
// No Flush() in between!
```

**Why?** D3D11 `Map()` with flag `0` (synchronous mode) implicitly:
1. Flushes the immediate command buffer
2. Stalls the CPU until the GPU finishes all pending work on that resource
3. Returns a CPU-accessible pointer

This is a **blocking synchronization point**.

### Deferred Context: Not Used

UE5.6 D3D11 RHI uses only the **immediate context**. No deferred contexts are employed:

- `Direct3DDeviceIMContext` is type `FD3D11DeviceContext` (wraps `ID3D11DeviceContext`)
- All GPU operations go directly to immediate context
- No `ExecuteCommandList()` calls in readback code path

---

## 4. Copy Method: CopySubresourceRegion

UE5 uses **CopySubresourceRegion**, not `CopyResource` or `ResolveSubresource`:

```cpp
Direct3DDeviceIMContext->CopySubresourceRegion(
    TempTexture2D,              // Destination
    0,                          // Dest subresource (0 = first mip, array index 0)
    0, 0, 0,                    // Dest offset (always 0,0,0 for a new temp texture)
    Texture->GetResource(),     // Source
    Subresource,                // Source subresource (calculated to handle mips, arrays)
    RectPtr                     // Source region (D3D11_BOX or NULL for whole texture)
);
```

**Why CopySubresourceRegion?**
- Supports partial region via `D3D11_BOX`
- Handles format conversions automatically (if within D3D11 rules)
- Destination offset parameter allows flexible positioning
- More flexible than `CopyResource` (which copies entire resource)
- More flexible than `ResolveSubresource` (which is for MSAA resolve only)

---

## 5. Map/Unmap Pattern: Synchronous Read

```cpp
D3D11_MAPPED_SUBRESOURCE LockedRect;

// Synchronous map: blocks until GPU finishes
HRESULT hr = Direct3DDeviceIMContext->Map(
    TempTexture2D,
    MappedSubresource,
    D3D11_MAP_READ,    // Read-only flag
    0,                 // Flags: 0 = synchronous (block if needed)
    &LockedRect
);

// Use LockedRect.pData as CPU pointer, LockedRect.RowPitch as stride
uint8* SrcPtr = (uint8*)LockedRect.pData;
// ...memcpy with RowPitch accounting...

Direct3DDeviceIMContext->Unmap(TempTexture2D, MappedSubresource);
```

**Key LockedRect fields:**
- `pData`: Pointer to GPU-allocated memory (CPU-readable after Map succeeds)
- `RowPitch`: Stride in bytes between rows (accounts for alignment padding)
- `DepthPitch`: Stride in bytes between 3D slices (used for 3D textures)

**Synchronization:**
- `Map(..., D3D11_MAP_READ, 0, ...)` blocks until GPU finishes
- Driver may cache the result if resource is already in system memory
- `Unmap()` completes the operation; data is no longer guaranteed accessible

---

## 6. Special Case: MSAA Readback

For multi-sample anti-aliased textures (line 465-592):

```cpp
void FD3D11DynamicRHI::ReadSurfaceDataMSAARaw(...)
{
    // Step 1: Create non-MSAA temp render target (USAGE_DEFAULT, BIND_RENDER_TARGET)
    D3D11_TEXTURE2D_DESC NonMSAADesc;
    NonMSAADesc.SampleDesc.Count = 1;
    NonMSAADesc.Usage = D3D11_USAGE_DEFAULT;
    NonMSAADesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    // ...create NonMSAATexture2D...

    // Step 2: Create staging texture (USAGE_STAGING, CPU_ACCESS_READ)
    // Same pattern as non-MSAA case
    // ...create StagingTexture2D...

    // Step 3: For EACH sample, resolve and copy
    for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // Resolve individual sample to non-MSAA target via shader
        ResolveTextureUsingShader<FResolveSingleSamplePS>(
            this,
            Texture,
            NULL,
            NonMSAARTV,
            NULL,
            NonMSAADesc,
            FResolveRect(InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y),
            FResolveRect(0, 0, SizeX, SizeY),
            SampleIndex
        );

        // Copy resolved sample to staging
        Direct3DDeviceIMContext->CopySubresourceRegion(
            StagingTexture2D, 0, 0, 0, 0,
            NonMSAATexture2D, Subresource,
            &Rect
        );

        // Map and read one sample
        D3D11_MAPPED_SUBRESOURCE LockedRect;
        Direct3DDeviceIMContext->Map(StagingTexture2D, 0, D3D11_MAP_READ, 0, &LockedRect);

        // Copy sample data (interleaved per-pixel)
        // ...memcpy with sample index offset...

        Direct3DDeviceIMContext->Unmap(StagingTexture2D, 0);
    }
}
```

**MSAA cost:**
- Creates 2 temporary textures (non-MSAA RT + staging)
- Per-sample: shader resolve + copy + map/unmap
- Results in interleaved sample data: `[Sample0_Pixel0, Sample1_Pixel0, ..., Sample0_Pixel1, Sample1_Pixel1, ...]`

---

## 7. Memory Management: On-Demand Temporary Textures

UE5 philosophy:
- **Do NOT pre-allocate persistent staging textures**
- Create temporary staging textures when needed
- Rely on `TRefCountPtr` to auto-release when out of scope
- Minimizes GPU VRAM waste but adds allocation CPU overhead

```cpp
TRefCountPtr<ID3D11Texture2D> TempTexture2D;
VERIFYD3D11RESULT_EX(
    Direct3DDevice->CreateTexture2D(&Desc, NULL, TempTexture2D.GetInitReference()),
    Direct3DDevice
);
// ...use TempTexture2D...
// ~TRefCountPtr() automatically releases when function returns
```

**Implications for UzEngine:**
- If screenshots are frequent, consider **caching staging textures** (UE5 does not)
- Cache key: width × height × format
- Reuse for multiple readbacks before destruction
- This trades memory for fewer allocations

---

## 8. Sub-Region Readback Optimization

UE5 minimizes PCIe bandwidth by reading only the requested region:

```cpp
FIntRect InRect = {X, Y, X+Width, Y+Height};

// Staging texture created at exact size needed
int32 SizeX = InRect.Width();    // Only request width
int32 SizeY = InRect.Height();   // Only request height

D3D11_TEXTURE2D_DESC Desc;
Desc.Width = SizeX;
Desc.Height = SizeY;
// ...

// CopySubresourceRegion copies only the region
D3D11_BOX Rect = {InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y, 0, 1};
Direct3DDeviceIMContext->CopySubresourceRegion(
    TempTexture2D, 0, 0, 0, 0,
    Texture->GetResource(), Subresource,
    &Rect  // Only copy the box, not whole texture
);
```

**Benefit:** For a 4K readback of a 2x2 region, you transfer ~32 bytes, not ~32 MB.

---

## Adaptation for UzEngine DX11 Backend

### Recommended Pattern

```cpp
// uz_readback.h
struct UzReadbackRequest {
    FRHITexture* SourceTexture;
    FIntRect Region;  // {0,0} to {width,height}
    EPixelFormat Format;
};

// uz_readback.cpp
bool UzDX11_ReadBackTexture(
    const UzReadbackRequest& Request,
    TArray<uint8>& OutData)
{
    // 1. Get or create staging texture (USAGE_STAGING, CPU_ACCESS_READ)
    ID3D11Texture2D* StagingTexture = GetOrCreateStagingTexture(
        Request.SourceTexture,
        Request.Region.Width(),
        Request.Region.Height(),
        Request.Format
    );

    // 2. CopySubresourceRegion from source to staging
    D3D11_BOX Box = {
        Request.Region.Min.X, Request.Region.Min.Y,
        Request.Region.Max.X, Request.Region.Max.Y,
        0, 1
    };
    DeviceContext->CopySubresourceRegion(
        StagingTexture, 0, 0, 0, 0,
        SourceResource, 0,
        &Box
    );

    // 3. Map (BLOCKING SYNC POINT)
    D3D11_MAPPED_SUBRESOURCE Mapped;
    HR(DeviceContext->Map(
        StagingTexture, 0,
        D3D11_MAP_READ, 0,
        &Mapped
    ));

    // 4. Copy with RowPitch accounting
    uint8* SrcPtr = (uint8*)Mapped.pData;
    uint32 BytesPerPixel = GetBytesPerPixel(Request.Format);
    uint32 BytesPerLine = Request.Region.Width() * BytesPerPixel;

    OutData.SetNumUninitialized(Request.Region.Width() * Request.Region.Height() * BytesPerPixel);
    uint8* DestPtr = OutData.GetData();

    for (uint32 Y = 0; Y < Request.Region.Height(); Y++)
    {
        memcpy(DestPtr, SrcPtr, BytesPerLine);
        DestPtr += BytesPerLine;
        SrcPtr += Mapped.RowPitch;
    }

    // 5. Unmap
    DeviceContext->Unmap(StagingTexture, 0);

    return true;
}

// Cache staging textures to avoid allocation overhead
std::unordered_map<uint64, ID3D11Texture2D*> StagingTextureCache;

ID3D11Texture2D* GetOrCreateStagingTexture(
    ID3D11Texture2D* SourceTexture,
    uint32 Width, uint32 Height, DXGI_FORMAT Format)
{
    uint64 Key = HashKey(Width, Height, Format);

    auto It = StagingTextureCache.find(Key);
    if (It != StagingTextureCache.end())
    {
        return It->second;
    }

    // Create and cache
    D3D11_TEXTURE2D_DESC Desc = {
        Width, Height, 1, 1,
        Format,
        {1, 0},
        D3D11_USAGE_STAGING,
        0,
        D3D11_CPU_ACCESS_READ,
        0
    };

    ID3D11Texture2D* StagingTexture;
    Device->CreateTexture2D(&Desc, nullptr, &StagingTexture);
    StagingTextureCache[Key] = StagingTexture;

    return StagingTexture;
}
```

### Key Decisions for UzEngine

1. **Use staging TEXTURE (not buffer)** — matches UE5, simpler format handling
2. **Cache staging textures** — UE5 doesn't, but makes sense for frequent screenshots
3. **NO explicit Flush()** — Map() is synchronous, handles sync automatically
4. **Synchronous Map()** — simpler code, acceptable for screenshots (not real-time critical)
5. **Support sub-region readback** — enables efficient region captures
6. **No deferred contexts** — keep it simple, use immediate context only

---

## References

- `W:/UE_5.6_Source/Windows/D3D11RHI/Private/D3D11RenderTarget.cpp`
  - `GetStagingTexture()` — lines 296-369
  - `ReadSurfaceDataNoMSAARaw()` — lines 371-410
  - `ReadSurfaceDataMSAARaw()` — lines 465-592
  - `RHIReadSurfaceData()` (FColor) — lines 412-463
  - `RHIReadSurfaceData()` (FLinearColor) — lines 696+

- `W:/UE_5.6_Source/Windows/D3D11RHI/Private/D3D11RHIPrivate.h`
  - Class `FD3D11DynamicRHI` — method declarations
  - Member `Direct3DDeviceIMContext` — line 718

---

## Anti-Patterns to Avoid

1. ❌ **Creating a persistent staging texture** — UE5 avoids this; adds complexity
2. ❌ **Using CopyResource for partial region** — CopyResource copies entire resource; use CopySubresourceRegion with D3D11_BOX
3. ❌ **Calling Flush() explicitly** — Map() does it automatically; redundant
4. ❌ **Using D3D11_MAP_WRITE or D3D11_MAP_READ_WRITE** — readback is read-only; use D3D11_MAP_READ
5. ❌ **Mapping directly from render target (USAGE_DEFAULT)** — must copy to staging first
6. ❌ **Ignoring RowPitch** — GPU alignment padding differs from format byte size; always account for RowPitch
7. ❌ **Reading MSAA textures as-is** — must resolve per-sample via shader, then read

---

**End of Analysis**
