# UE5 DX11 Constant Buffer & Deferred Context Analysis

**Critical Finding:** UE5's DX11 backend **DOES NOT USE DEFERRED CONTEXTS**. It uses the immediate context exclusively.

## Question 1: Does UE5 Use Deferred Contexts on DX11?

**Answer: NO. Not at all.**

- **File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11RHIPrivate.h:718`
- **Key Line**: `TRefCountPtr<FD3D11DeviceContext> Direct3DDeviceIMContext;`
- **Definition**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Public\D3D11Resources.h`
  ```cpp
  typedef ID3D11DeviceContext FD3D11DeviceContext;
  ```

There are **zero occurrences** of:
- `CreateDeferredContext`
- `FinishCommandList`
- `ExecuteCommandList`

in the entire D3D11RHI codebase.

**Rendering Model**: Direct immediate context submission. All calls go straight to the immediate context (`Direct3DDeviceIMContext`), no recording or deferred execution.

---

## Question 2: How Does UE5 Bind Constant Buffers on DX11?

**Answer: Via state cache with `VSSetConstantBuffers`/`PSSetConstantBuffers`.**

### Binding Flow

1. **State Cache Intercepts**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11StateCachePrivate.h:350-366`
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

2. **Dispatch to D3D11**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11StateCachePrivate.h:158-169`
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

3. **Command Emission**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11Commands.cpp:113`
   ```cpp
   StateCache.SetConstantBuffer<ShaderFrequency>(ConstantBuffer, BufferIndex);
   ```

**Pattern**: Cached state — avoid redundant `VSSetConstantBuffers` calls if the same buffer is already bound. Direct immediate context dispatch, no queuing.

---

## Question 3: How Does UE5 Create Constant Buffers on DX11?

**Answer: D3D11_USAGE_DYNAMIC with Map/Discard pattern (not UpdateSubresource).**

### Pooled Uniform Buffer Creation

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:113-168`

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
        // ... validation ...
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
        Desc.Usage = D3D11_USAGE_DYNAMIC;                         // ← KEY
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Desc.MiscFlags = 0;
        Desc.StructureByteStride = 0;

        VERIFYD3D11RESULT_EX(Device->CreateBuffer(&Desc, NULL, UniformBufferResource.GetInitReference()), Device);
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

### Key Decisions

| Aspect | Value | Rationale |
|--------|-------|-----------|
| **Usage** | `D3D11_USAGE_DYNAMIC` | Allows fast CPU updates via Map/Discard |
| **Upload Method** | `Map(..., D3D11_MAP_WRITE_DISCARD, ...)` + `Unmap()` | **NOT UpdateSubresource** |
| **Pooling** | Size-bucketed ring buffer | Avoid repeated allocation/deallocation |
| **Reuse Delay** | 3 frames (NumSafeFrames) | AMD driver workaround — prevents GPU still-in-use errors |
| **Non-Pooled Fallback** | `CreateBuffer()` with `D3D11_SUBRESOURCE_DATA` | Immediate data upload via GPU copy |

### Why NOT UpdateSubresource?

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:142-143` (comment)
> "This is method of updating is vastly superior to creating a new constant buffer each time with D3D11_USAGE_IMMUTABLE, **Since that inserts the data into the command buffer which causes GPU flushes**"

**UpdateSubresource is used ONLY for non-streaming textures and buffers** (e.g., when unlocking a static buffer):
- `D3D11Buffer.cpp:318` — staging buffer writes via UpdateSubresource
- `D3D11Texture.cpp:1235, 1267` — texture MIP updates

**NOT used for per-frame constant buffer updates.**

---

## Question 4: What's UE5's DX11 Rendering Submission Model?

**Answer: Immediate context, immediate submission. No command recording.**

### Key Characteristics

1. **No Command Lists**: All work submitted directly to `Direct3DDeviceIMContext`
2. **State Caching**: Avoids redundant state set calls via `FD3D11StateCache`
3. **Per-Frame Synchronization**:
   - `RHIEndFrame()` inserts completion queries
   - Frame boundary tracking via `D3D11_QUERY_EVENT`
   - GPU profiling via disjoint timestamp queries

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11RHI.cpp:26-100`

```cpp
void FD3D11DynamicRHI::RHIEndFrame(const FRHIEndFrameArgs& Args)
{
    // ... GPU profiler setup ...
    Direct3DDeviceIMContext->End(Profiler.Current.CompletionQuery);
    Profiler.Pending.Enqueue(MakeUnique<FProfiler::FFrame>(MoveTemp(Profiler.Current)));

    // Process historic results
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
        // ... process results ...
    }
}
```

**No deferred context recording. Immediate execution.**

---

## Question 5: How Does UE5 Handle Per-Frame Constant Data on DX11?

**Answer: Pooled DYNAMIC buffers with Map/Discard, 3-frame reuse delay.**

### The Flow

1. **Frame Start**: `UniformBufferBeginFrame()` cycles SafeFrameIndex
2. **Per-Uniform-Buffer Create**:
   - Request size bucket (power-of-two)
   - Reuse from pool OR create new DYNAMIC buffer
   - Map with `D3D11_MAP_WRITE_DISCARD`
   - Memcpy user data
   - Unmap
3. **Frame End**:
   - Return freed buffers to SafeUniformBufferPools
   - Wait 3 frames before recycling (AMD driver safety)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:59-104`

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
                // ... release ...
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
        SafeUniformBufferPools[SafeFrameIndex][BucketIndex].Reset();
    }
}
```

### The Safety Window (NumSafeFrames = 3)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:25-30`

```cpp
/**
 * Number of frames that a uniform buffer will not be re-used for, after being freed.
 * This is done as a workaround for what appears to be an AMD driver bug with 11.10 drivers and a 6970 HD,
 * Where reusing a constant buffer with D3D11_MAP_WRITE_DISCARD still in use by the GPU will result in incorrect contents randomly.
 */
const int32 NumSafeFrames = 3;
```

**Critical**: AMD driver bug workaround. If you reuse a DYNAMIC buffer too quickly while GPU is still reading it, you get corruption.

---

## UzEngine's DX11 Problem: ROOT CAUSE ANALYSIS

### What UzEngine is Doing WRONG

Your black screen issue likely stems from one or more of these:

1. **Using deferred contexts when you shouldn't**
   - Deferred contexts have **no GPU synchronization semantics**
   - `ExecuteCommandList()` simply replays commands; there's no handshake
   - If you're clearing/drawing on a deferred context and expecting to see it, you're racing the GPU

2. **Updating constant buffers BEFORE binding**
   - You must:
     a. Create/update the buffer with current data
     b. Bind it with `VSSetConstantBuffers()`
     c. THEN draw
   - If you swap the order, the shader reads uninitialized data

3. **UpdateSubresource on the wrong context**
   - UpdateSubresource is synchronous on immediate context (blocking)
   - On deferred context, it's just recorded and might not flush before ExecuteCommandList

4. **Not flushing after ExecuteCommandList()**
   - Executing a deferred command list doesn't guarantee GPU execution
   - You might need `Direct3DDeviceIMContext->Flush()` before Present

5. **Constant buffer ownership issues**
   - Creating DEFAULT buffers instead of DYNAMIC
   - Not respecting the 3-frame delay before reuse

### What UE5 Does (The Correct Pattern)

```
Per-Frame:
  1. UniformBufferBeginFrame()  // Cycle pools
  2. For each uniform buffer:
     - Map(DISCARD) + Memcpy + Unmap on IMMEDIATE context
     - SetConstantBuffer() on state cache (immediate context)
  3. Draw calls on IMMEDIATE context
  4. RHIEndFrame() inserts completion query
  5. GPU executes immediately
```

---

## UzEngine Adaptation

### Step 1: ELIMINATE Deferred Contexts

Do NOT use:
- `CreateDeferredContext()`
- `FinishCommandList()`
- `ExecuteCommandList()`

Work directly on the immediate context.

### Step 2: Constant Buffer Pipeline

```cpp
// Frame start
UniformBufferBeginFrame();  // Cycle pool buckets

// Per draw call
{
    // 1. Create/update CB
    ID3D11Buffer* cbuffer = CreatePooledUniformBuffer(
        device,
        immediate_context,
        &cb_data,
        sizeof(cb_data)
    );

    // 2. Bind CB to shader
    StateCache.SetConstantBuffer<SF_Vertex>(cbuffer, 0);

    // 3. Set shaders, input layout, topology
    // ... (all on immediate context)

    // 4. Draw
    immediate_context->DrawIndexed(...);
}

// Frame end
RHIEndFrame();  // Insert completion query
```

### Step 3: Pooling Strategy

Implement size-bucketed pooling:
```cpp
// Create: power-of-two bucket sizes
uint32 GetPoolBucketSize(uint32 NumBytes) {
    return (1u << CeilLog2(NumBytes));
}

// Reuse delay: 3 frames
const int SafeFrames = 3;
SafeUniformBufferPools[SafeFrameIndex][BucketIndex]  // Freed this-cycle-safe-soon
UniformBufferPool[BucketIndex]                       // Freed > 3 frames ago, reusable now
```

### Step 4: Map Flags

**ALWAYS use D3D11_MAP_WRITE_DISCARD for per-frame data:**

```cpp
D3D11_MAPPED_SUBRESOURCE mapped;
Context->Map(
    buffer,           // DYNAMIC usage buffer
    0,                // subresource
    D3D11_MAP_WRITE_DISCARD,  // ← KEY: tells GPU "I'm overwriting entire buffer"
    0,                // flags
    &mapped
);
memcpy(mapped.pData, data, size);
Context->Unmap(buffer, 0);
```

NOT `Map()` without DISCARD, NOT `UpdateSubresource()`.

### Step 5: State Cache

Implement caching to avoid redundant state calls:
```cpp
struct CB_State {
    ID3D11Buffer* Buffer;
    uint32 FirstConstant;
    uint32 NumConstants;
};

void SetConstantBuffer(EShaderFrequency Stage, uint32 Slot, ID3D11Buffer* CB) {
    CB_State& Current = CachedCBs[Stage][Slot];
    if (Current.Buffer != CB) {
        Current.Buffer = CB;
        // Dispatch based on stage
        switch(Stage) {
            case SF_Vertex: immediate_context->VSSetConstantBuffers(Slot, 1, &CB); break;
            case SF_Pixel:  immediate_context->PSSetConstantBuffers(Slot, 1, &CB); break;
            // ...
        }
    }
}
```

---

## Summary Table

| Aspect | UE5 Pattern | UzEngine Should Do |
|--------|-------------|-------------------|
| **Context** | Immediate only | Eliminate deferred contexts |
| **CB Creation** | `D3D11_USAGE_DYNAMIC`, no initial data | Same |
| **CB Upload** | `Map(DISCARD) + Unmap` | Same |
| **CB Binding** | Via state cache on immediate context | Same |
| **Rendering** | Direct immediate context calls | Same |
| **Submission** | Per-frame queries, no flush needed | Same |
| **Pooling** | Size-bucketed, 3-frame reuse delay | Implement this |
| **UpdateSubresource** | Only for staging/non-streaming | Not for per-frame CBs |

---

## File Reference Map

| File | Key Lines | Concept |
|------|-----------|---------|
| `D3D11RHIPrivate.h` | 718 | Immediate context member |
| `D3D11Resources.h` | - | Typedef ID3D11DeviceContext → FD3D11DeviceContext |
| `D3D11UniformBuffer.cpp` | 113-168 | Pooled buffer creation + Map/Discard pattern |
| `D3D11UniformBuffer.cpp` | 25-30, 59-104 | Pool cycling, safety delay |
| `D3D11UniformBuffer.cpp` | 279-294 | UpdateUniformBufferContents (Map/Unmap) |
| `D3D11StateCachePrivate.h` | 350-366 | CB binding state cache |
| `D3D11StateCachePrivate.h` | 158-169 | Dispatch to VSSetConstantBuffers etc. |
| `D3D11Commands.cpp` | 113 | Binding during draw |
| `D3D11RHI.cpp` | 26-100 | RHIEndFrame frame synchronization |
| `D3D11Buffer.cpp` | 318 | UpdateSubresource (staging only, not CBs) |

---

## Why You're Getting Black Screen

**Most Likely Causes**:

1. **Deferred context invalidation**: Commands recorded on deferred context don't execute if you don't call `ExecuteCommandList()` correctly
2. **Uninitialized constant buffers**: Creating DEFAULT-usage CBs or never uploading data
3. **Binding before uploading**: Binding an empty CB, then uploading after
4. **No implicit flush**: Deferred contexts don't auto-flush; if you render on deferred and execute without proper sync, GPU might not see the data

**Test Checklist**:
- [ ] Remove all `CreateDeferredContext()` calls
- [ ] Verify CBs are created with `D3D11_USAGE_DYNAMIC`
- [ ] Verify upload happens BEFORE binding (Map/Discard/Unmap, then SetConstantBuffer)
- [ ] Verify binding happens BEFORE draw
- [ ] Use immediate context for all state and draw calls
- [ ] Add debug breakpoint after `Unmap()` to confirm data is written
- [ ] Try a simple constant buffer with a single float and output it to SV_Position to see if it's wired

Good luck!
