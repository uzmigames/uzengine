# UE5 DX11 Implementation — Exact Code Reference

This document contains exact code snippets from UE5 5.6 source, with file paths and line numbers. Use these as templates for UzEngine.

---

## 1. Constant Buffer Pool Structure

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp`

### Pool Bucket System (Lines 11-56)

```cpp
// Describes a uniform buffer in the free pool.
struct FPooledUniformBuffer
{
    TRefCountPtr<ID3D11Buffer> Buffer;
    uint32 CreatedSize;
    uint32 FrameFreed;
};

// Number of size buckets to use for the uniform buffer free pool.
// This needs to be enough to cover the valid uniform buffer size range combined with the heuristic used to map sizes to buckets.
const int32 NumPoolBuckets = 17;

// Number of frames that a uniform buffer will not be re-used for, after being freed.
// This is done as a workaround for what appears to be an AMD driver bug with 11.10 drivers and a 6970 HD,
// Where reusing a constant buffer with D3D11_MAP_WRITE_DISCARD still in use by the GPU will result in incorrect contents randomly.
const int32 NumSafeFrames = 3;

// Returns the size in bytes of the bucket that the given size fits into.
uint32 GetPoolBucketSize(uint32 NumBytes)
{
    return FMath::RoundUpToPowerOfTwo(NumBytes);
}

// Returns the index of the bucket that the given size fits into.
uint32 GetPoolBucketIndex(uint32 NumBytes)
{
    return FMath::CeilLogTwo(NumBytes);
}

// Pool of free uniform buffers, indexed by bucket for constant size search time.
TArray<FPooledUniformBuffer> UniformBufferPool[NumPoolBuckets];

// Uniform buffers that have been freed more recently than NumSafeFrames ago.
TArray<FPooledUniformBuffer> SafeUniformBufferPools[NumSafeFrames][NumPoolBuckets];
```

---

## 2. Per-Frame Pool Cycling

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:59-104`

This runs at the start of every frame.

```cpp
void UniformBufferBeginFrame()
{
    int32 NumCleaned = 0;

    SCOPE_CYCLE_COUNTER(STAT_D3D11CleanUniformBufferTime);

    // Clean a limited number of old entries to reduce hitching when leaving a large level
    for (int32 BucketIndex = 0; BucketIndex < NumPoolBuckets; BucketIndex++)
    {
        for (int32 EntryIndex = UniformBufferPool[BucketIndex].Num() - 1; EntryIndex >= 0 && NumCleaned < 10; EntryIndex--)
        {
            FPooledUniformBuffer& PoolEntry = UniformBufferPool[BucketIndex][EntryIndex];

            check(IsValidRef(PoolEntry.Buffer));

            // Clean entries that are unlikely to be reused
            if (GFrameNumberRenderThread - PoolEntry.FrameFreed > (uint32)GD3DUniformPoolRecycleDepth)
            {
                DEC_DWORD_STAT(STAT_D3D11NumFreeUniformBuffers);
                DEC_MEMORY_STAT_BY(STAT_D3D11FreeUniformBufferMemory, PoolEntry.CreatedSize);
                NumCleaned++;
                D3D11BufferStats::UpdateUniformBufferStats(PoolEntry.Buffer, PoolEntry.CreatedSize, false);
                PoolEntry.Buffer.SafeRelease();
                UniformBufferPool[BucketIndex].RemoveAtSwap(EntryIndex);
            }
        }
    }

    // Index of the bucket that is now old enough to be reused
    const int32 SafeFrameIndex = GFrameNumberRenderThread % NumSafeFrames;

    // Merge the bucket into the free pool array
    for (int32 BucketIndex = 0; BucketIndex < NumPoolBuckets; BucketIndex++)
    {
        int32 LastNum = UniformBufferPool[BucketIndex].Num();
        UniformBufferPool[BucketIndex].Append(SafeUniformBufferPools[SafeFrameIndex][BucketIndex]);
#if DO_CHECK
        while (LastNum < UniformBufferPool[BucketIndex].Num())
        {
            check(IsValidRef(UniformBufferPool[BucketIndex][LastNum].Buffer));
            LastNum++;
        }
#endif
        SafeUniformBufferPools[SafeFrameIndex][BucketIndex].Reset();
    }
}
```

**Key Pattern**: 3-frame rotational buffers. SafeFrameIndex rotates 0→1→2→0→... as frames advance. Buffers freed in frame N go to SafeUniformBufferPools[N % 3], and only move to the main pool 3 frames later.

---

## 3. Pooled Buffer Creation with Map/Discard

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:113-168`

This is the CORE function that creates or reuses a constant buffer.

```cpp
static TRefCountPtr<ID3D11Buffer> CreateAndUpdatePooledUniformBuffer(
    FD3D11Device* Device,
    FD3D11DeviceContext* Context,
    const void* Contents,
    uint32 NumBytes)
{
    TRefCountPtr<ID3D11Buffer> UniformBufferResource;

    // Find the appropriate bucket based on size
    const uint32 BucketIndex = GetPoolBucketIndex(NumBytes);
    TArray<FPooledUniformBuffer>& PoolBucket = UniformBufferPool[BucketIndex];

    if (PoolBucket.Num() > 0)
    {
        // Reuse the last entry in this size bucket
        FPooledUniformBuffer FreeBufferEntry = PoolBucket.Pop();
        check(IsValidRef(FreeBufferEntry.Buffer));
        checkf(FreeBufferEntry.CreatedSize >= NumBytes, TEXT("%u %u %u %u"), NumBytes, BucketIndex, FreeBufferEntry.CreatedSize, GetPoolBucketSize(NumBytes));
        DEC_DWORD_STAT(STAT_D3D11NumFreeUniformBuffers);
        DEC_MEMORY_STAT_BY(STAT_D3D11FreeUniformBufferMemory, FreeBufferEntry.CreatedSize);
        UniformBufferResource = FreeBufferEntry.Buffer;
    }

    if (!IsValidRef(UniformBufferResource))
    {
        D3D11_BUFFER_DESC Desc;
        // Allocate based on the bucket size, since this uniform buffer will be reused later
        Desc.ByteWidth = GetPoolBucketSize(NumBytes);
        // Use D3D11_USAGE_DYNAMIC, which allows multiple CPU writes for pool reuses
        // This is method of updating is vastly superior to creating a new constant buffer each time with D3D11_USAGE_IMMUTABLE,
        // Since that inserts the data into the command buffer which causes GPU flushes
        Desc.Usage = D3D11_USAGE_DYNAMIC;
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Desc.MiscFlags = 0;
        Desc.StructureByteStride = 0;

        VERIFYD3D11RESULT_EX(Device->CreateBuffer(&Desc, NULL, UniformBufferResource.GetInitReference()), Device);

        D3D11BufferStats::UpdateUniformBufferStats(UniformBufferResource, Desc.ByteWidth, true);
    }

    check(IsValidRef(UniformBufferResource));

    if (Contents)
    {
        D3D11_MAPPED_SUBRESOURCE MappedSubresource;
        // Discard previous results since we always do a full update
        VERIFYD3D11RESULT_EX(Context->Map(UniformBufferResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubresource), Device);
        check(MappedSubresource.RowPitch >= NumBytes);
        FMemory::Memcpy(MappedSubresource.pData, Contents, NumBytes);
        Context->Unmap(UniformBufferResource, 0);
    }

    return UniformBufferResource;
}
```

**Critical Pattern**:
1. Check pool for reusable buffer of this size
2. If empty, create new with `D3D11_USAGE_DYNAMIC` and no initial data
3. Map with `D3D11_MAP_WRITE_DISCARD` (tells GPU: "I'm overwriting the whole thing, GPU can discard old contents")
4. Memcpy the user data
5. Unmap (GPU now owns the buffer again)

---

## 4. State Cache Constant Buffer Binding

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11StateCachePrivate.h:350-366`

```cpp
template <EShaderFrequency ShaderFrequency>
D3D11_STATE_CACHE_INLINE void SetConstantBuffer(ID3D11Buffer* ConstantBuffer, uint32 SlotIndex)
{
#if D3D11_ALLOW_STATE_CACHE
    D3D11_STATE_CACHE_VERIFY_PRE();
    FD3D11ConstantBufferState& Current = CurrentConstantBuffers[ShaderFrequency][SlotIndex];
    if ((Current.Buffer != ConstantBuffer || Current.FirstConstant != 0 || Current.NumConstants != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT) || GD3D11SkipStateCaching)
    {
        Current.Buffer = ConstantBuffer;
        Current.FirstConstant = 0;
        Current.NumConstants = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;
        InternalSetSetConstantBuffer<ShaderFrequency>(SlotIndex, ConstantBuffer);
    }
    D3D11_STATE_CACHE_VERIFY_POST();
#else
    InternalSetSetConstantBuffer<ShaderFrequency>(SlotIndex, ConstantBuffer);
#endif
}
```

**State Cache Dispatch** (Lines 158-169):

```cpp
template <EShaderFrequency ShaderFrequency>
D3D11_STATE_CACHE_INLINE void InternalSetSetConstantBuffer(uint32 SlotIndex, ID3D11Buffer*& ConstantBuffer)
{
    CA_SUPPRESS(6326);
    switch (ShaderFrequency)
    {
    case SF_Vertex:     Direct3DDeviceIMContext->VSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
    case SF_Geometry:   Direct3DDeviceIMContext->GSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
    case SF_Pixel:      Direct3DDeviceIMContext->PSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
    case SF_Compute:    Direct3DDeviceIMContext->CSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
    }
}
```

**Pattern**: Template dispatch on shader stage, switch to call the right D3D11 method.

---

## 5. Constant Buffer State Cache Definition

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11StateCachePrivate.h:119-126`

```cpp
// Constant Buffer State
struct FD3D11ConstantBufferState
{
    ID3D11Buffer* Buffer;
    uint32 FirstConstant;
    uint32 NumConstants;
} CurrentConstantBuffers[SF_NumStandardFrequencies][D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
```

This is a 2D array:
- First dimension: shader frequency (VS, GS, PS, CS)
- Second dimension: CB slot index (0–13 typically)

---

## 6. How Constant Buffers Are Actually Used in Commands

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11Commands.cpp:108-117`

```cpp
template<EShaderFrequency ShaderFrequency>
void FD3D11DynamicRHI::BindUniformBuffer(uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
    check(BufferRHI && BufferRHI->GetLayout().GetHash());

    FD3D11UniformBuffer* Buffer = ResourceCast(BufferRHI);

    ID3D11Buffer* ConstantBuffer = Buffer ? Buffer->Resource.GetReference() : nullptr;
    StateCache.SetConstantBuffer<ShaderFrequency>(ConstantBuffer, BufferIndex);

    BoundUniformBuffers[ShaderFrequency][BufferIndex] = BufferRHI;
    DirtyUniformBuffers[ShaderFrequency] |= (1 << BufferIndex);
}
```

**Flow**: RHI wrapper calls `StateCache.SetConstantBuffer()`, which checks cache and only calls `VSSetConstantBuffers()` if changed.

---

## 7. Global Constant Buffer Commit (Per-Frame)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11Commands.cpp:989+`

```cpp
void FD3D11DynamicRHI::CommitNonComputeShaderConstants()
{
    FD3D11BoundShaderState* CurrentBoundShaderState = (FD3D11BoundShaderState*)BoundShaderStateHistory.GetLast();
    check(CurrentBoundShaderState);

    // Only set the constant buffer if this shader needs the global constant buffer bound
    // Otherwise we will overwrite a different constant buffer
    if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Vertex])
    {
        // Commit and bind vertex shader constants
        FD3DRHIUtil::CommitConstants<SF_Vertex>(VSConstantBuffer, StateCache, bDiscardSharedConstants);
    }

    if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Geometry])
    {
        // Commit and bind geometry shader constants
        FD3DRHIUtil::CommitConstants<SF_Geometry>(GSConstantBuffer, StateCache, bDiscardSharedConstants);
    }

    if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Pixel])
    {
        // Commit and bind pixel shader constants
        FD3DRHIUtil::CommitConstants<SF_Pixel>(PSConstantBuffer, StateCache, bDiscardSharedConstants);
    }
}
```

**Template Specialization** (`FD3DRHIUtil::CommitConstants`):

File shows this pattern:
```cpp
template <EShaderFrequency ShaderFrequencyT>
struct FCommitConstantsHelper
{
    static FORCEINLINE void Commit(FD3D11ConstantBuffer* InConstantBuffer, FD3D11StateCache& StateCache, bool bDiscardSharedConstants)
    {
        FWinD3D11ConstantBuffer* ConstantBuffer = static_cast<FWinD3D11ConstantBuffer*>(InConstantBuffer);
        // Array may contain NULL entries to pad out to proper
        if (ConstantBuffer && ConstantBuffer->CommitConstantsToDevice(bDiscardSharedConstants))
        {
            ID3D11Buffer* DeviceBuffer = ConstantBuffer->GetConstantBuffer();
            StateCache.SetConstantBuffer<ShaderFrequencyT>(DeviceBuffer, GLOBAL_CONSTANT_BUFFER_INDEX);
        }
    }
};
```

---

## 8. End-of-Frame GPU Synchronization

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11RHI.cpp:26-100`

```cpp
void FD3D11DynamicRHI::RHIEndFrame(const FRHIEndFrameArgs& Args)
{
    // End Frame
#if RHI_NEW_GPU_PROFILER
    {
        // End GPU work
        auto& EndWork = EmplaceProfilerEvent<UE::RHI::GPUProfiler::FEvent::FEndWork>();
        InsertProfilerTimestamp(&EndWork.GPUTimestampBOP);

        uint64 Timestamp = FPlatformTime::Cycles64();

        // Insert frame boundary
        EmplaceProfilerEvent<UE::RHI::GPUProfiler::FEvent::FFrameBoundary>(Timestamp, Args.FrameNumber
    #if WITH_RHI_BREADCRUMBS
            , Args.GPUBreadcrumbs[ERHIPipeline::Graphics]
    #endif
    #if STATS
            , Args.StatsFrame
    #endif
        );

        // Issue a completion query so we know when to readback these profiler results.
        {
            if (Profiler.EventPool.IsEmpty())
            {
                D3D11_QUERY_DESC QueryDesc {};
                QueryDesc.Query = D3D11_QUERY_EVENT;
                VERIFYD3D11RESULT(Direct3DDevice->CreateQuery(&QueryDesc, Profiler.Current.CompletionQuery.GetInitReference()));
            }
            else
            {
                Profiler.Current.CompletionQuery = Profiler.EventPool.Pop();
            }

            Direct3DDeviceIMContext->End(Profiler.Current.CompletionQuery);
            Profiler.Pending.Enqueue(MakeUnique<FProfiler::FFrame>(MoveTemp(Profiler.Current)));
        }

        // Attempt to process historic results
        while (TUniquePtr<FProfiler::FFrame>* PreviousFramePtr = Profiler.Pending.Peek())
        {
            TUniquePtr<FProfiler::FFrame>& PreviousFrame = *PreviousFramePtr;

            BOOL EventComplete = false;
            VERIFYD3D11RESULT(Direct3DDeviceIMContext->GetData(PreviousFrame->CompletionQuery, &EventComplete, sizeof(EventComplete), 0));
            if (!EventComplete)
            {
                // Frame not yet finished on the GPU
                break;
            }

            // Ensure we have the latest timestamp data
            PollQueryResults();

            // Previous frame has completed and the data is available. Publish the profiler events.
            UE::RHI::GPUProfiler::ProcessEvents(MakeArrayView(&PreviousFrame->EventStream, 1));

            Profiler.EventPool.Push(MoveTemp(PreviousFrame->CompletionQuery));
            Profiler.Pending.Pop();
        }

        // Start the next frame's GPU work
        auto& BeginWork = EmplaceProfilerEvent<UE::RHI::GPUProfiler::FEvent::FBeginWork>(Timestamp);
        InsertProfilerTimestamp(&BeginWork.GPUTimestampTOP);
    }

#else
    GPUProfilingData.EndFrame();
#endif

    UpdateMemoryStats();
    CurrentComputeShader = nullptr;

    // Begin Frame
    UniformBufferBeginFrame();
}
```

**Key**: Uses `D3D11_QUERY_EVENT` to track frame completion. No explicit flush needed — queries handle synchronization.

---

## 9. Immediate Context Declaration

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11RHIPrivate.h:717-721`

```cpp
// The global D3D device's immediate context
TRefCountPtr<FD3D11DeviceContext> Direct3DDeviceIMContext;

#if WITH_DX_PERF
    TRefCountPtr<ID3DUserDefinedAnnotation> Direct3DDeviceIMAnnotation;
#endif
```

**Typedef** from `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Public\D3D11Resources.h`:

```cpp
typedef ID3D11DeviceContext FD3D11DeviceContext;
```

**NO deferred contexts. NO separate thread for recording. Just the immediate context.**

---

## 10. When UpdateSubresource IS Used

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11Buffer.cpp:315-322`

This is for unlocking a STATIC buffer (not per-frame CBs):

```cpp
else
{
    // Copy the contents of the temporary memory buffer allocated for writing into the buffer.
    Direct3DDeviceIMContext->UpdateSubresource(Buffer->Resource, 0, NULL, LockedData.GetData(), LockedData.Pitch, 0);

    // Free the temporary memory buffer.
    LockedData.FreeData();
}
```

**Why this and not Map/Discard?** Because this buffer is DEFAULT usage (GPU-writable), not DYNAMIC. You can't Map a DEFAULT buffer.

**For CBs: NEVER use UpdateSubresource. Always Map/Discard.**

---

## Implementation Checklist for UzEngine

### Phase 1: Remove Deferred Contexts

- [ ] Delete all `CreateDeferredContext()` calls
- [ ] Delete all `FinishCommandList()` / `ExecuteCommandList()` calls
- [ ] Verify all work goes to immediate context only
- [ ] Remove any deferred context wrapper code

### Phase 2: Implement Pooling

- [ ] Define `PoolBucketSize()` (power-of-two)
- [ ] Define `PoolBucketIndex()` (CeilLog2)
- [ ] Allocate `UniformBufferPool[NumBuckets]`
- [ ] Allocate `SafeUniformBufferPools[NumSafeFrames][NumBuckets]`
- [ ] Implement `UniformBufferBeginFrame()` (pool cycle)
- [ ] Implement `CreateAndUpdatePooledUniformBuffer()`

### Phase 3: Fix CB Upload

- [ ] Create CBs with `D3D11_USAGE_DYNAMIC` and NULL initial data
- [ ] Always use `Map(D3D11_MAP_WRITE_DISCARD)` for upload
- [ ] Never use `UpdateSubresource()` on CBs
- [ ] Unmap immediately after Memcpy

### Phase 4: State Cache

- [ ] Cache last-bound CB per (stage, slot)
- [ ] Only call `VSSetConstantBuffers()` if cache miss
- [ ] Use template dispatch on shader frequency
- [ ] Avoid redundant API calls

### Phase 5: Test

- [ ] Simple CB with single float value
- [ ] Output CB value to SV_Position (should be visible in backbuffer)
- [ ] Verify no black screen
- [ ] Measure perf: should be fast (no stalls)

---

## Quick Reference: D3D11 Buffer Descriptor for CBs

```cpp
D3D11_BUFFER_DESC Desc = {};
Desc.ByteWidth = AlignedSize;                          // Must be 16-byte aligned
Desc.Usage = D3D11_USAGE_DYNAMIC;                       // Per-frame upload
Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
Desc.MiscFlags = 0;
Desc.StructureByteStride = 0;

ID3D11Buffer* Buffer = nullptr;
device->CreateBuffer(&Desc, nullptr, &Buffer);         // No initial data!

// Later, per-frame:
D3D11_MAPPED_SUBRESOURCE Mapped;
Context->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
memcpy(Mapped.pData, YourData, size);
Context->Unmap(Buffer, 0);

// Then bind:
Context->VSSetConstantBuffers(slot, 1, &Buffer);
```

Done!
