# RHI Pattern Rationale: Why UE5 Designs DX11/DX12 Abstraction This Way

## Fundamental Insight: Pipeline Stalls vs Hazards

The core difference between DX11 and DX12 abstraction isn't about API features — it's about **when** you find out about conflicts.

### DX11: Implicit Synchronization (Forgiving)
```
GPU Timeline:
│ 0ns: GPU reads Texture as SRV
│       │ 200ns
│       └─ Still reading...
│           │ 400ns
│           └─ Still reading...
│               │ 600ns
│               └─ Done reading

CPU calls: Context->PSSetRenderTargets(Texture)  ← DX11 WAITS here
```

DX11 automatically stalls the CPU at binding time if GPU still has the resource. **Synchronization is implicit and invisible.**

### DX12: Explicit Synchronization (Dangerous)
```
GPU Timeline:
│ 0ns: GPU reads Texture as SRV
│       │ 200ns
│       └─ Command List processes...

CPU calls: CommandList->SetRenderTarget(Texture)  ← DX12 does NOTHING here
           CommandList->DrawIndexedPrimitive()    ← GPU tries to write to RTV

If GPU still reading as SRV at 200ns → **DEVICE REMOVAL (crash/hang)**
```

DX12 assumes the programmer knows what they're doing. **Synchronization is explicit and required.**

---

## Pattern 1: Batched Parameters (FRHIBatchedShaderParameters)

### Why This Pattern Exists

**Problem:**
- You want to bind 10 textures, 4 samplers, and 64 KB of constants
- DX11: Must iterate and call `PSSetShaderResources()` 10 times, `PSSetSamplers()` 4 times, and copy constant data
- DX12: Must update descriptor heap, call `SetDescriptorTable()` once

**Solution: Separate Collection from Execution**

```cpp
// Collection phase (backend-neutral)
FRHIBatchedShaderParameters Params;
for (int i = 0; i < 10; ++i) {
    Params.SetShaderTexture(i, Textures[i]);
}
for (int i = 0; i < 4; ++i) {
    Params.SetShaderSampler(i, Samplers[i]);
}
Params.SetShaderParameter(0, ConstantData, 64 * 1024);
// Params now contains WHAT to bind, not HOW

// Execution phase (backend-specific)
RHI.SetBatchedShaderParameters(Shader, Params);
// DX11: Iterates Params and calls PSSetShaderResources()
// DX12: Updates descriptor heap and calls SetDescriptorTable()
```

**Why This Works:**
1. Caller doesn't know or care about backend
2. Backend can optimize (e.g., DX12 can batch descriptor updates)
3. Validation happens in collection phase (catch errors early)
4. No temporary allocations during execution phase

### Why NOT Use a Callback/Visitor Pattern?

**Bad approach:**
```cpp
RHI.SetShaderParameters(Shader, [](FRHIParameterSetter& Setter) {
    for (int i = 0; i < 10; ++i) {
        Setter.SetTexture(i, Textures[i]);
    }
});
```

**Why this fails:**
- Lambda capture is expensive
- No way to pre-validate bindings
- Callback overhead on every draw
- Can't re-use parameters across multiple shaders

**Batched approach fixes all of these:**
- Parameters are pre-allocated in arena
- Validation happens once during collection
- Parameters can be reused 100 times with no cost
- No callback overhead (simple struct pass)

---

## Pattern 2: Ring Buffer Constant Buffer (Versioning)

### Why This Pattern Exists

**Problem:**
- You update a constant buffer every frame
- DX11: `Map(DISCARD)` → write → `Unmap()`
- DX12: Ring buffer required to avoid GPU hazards

**DX11 Map() Behavior:**
```
Frame N:
  CPU calls Map(DISCARD) ← "Discard old contents, give me new memory"
           ↓
         DX11 gives you mapped pointer to same underlying buffer
         (Ring buffer under the hood, but you don't see it)
           ↓
  CPU writes to [Ptr, Ptr + Size)
           ↓
  CPU calls Unmap()
           ↓
  GPU can now read from this memory

Frame N+1:
  CPU calls Map(DISCARD) again
           ↓
         DX11 gives you mapped pointer to DIFFERENT internal buffer
         (Rotates the ring buffer)
           ↓
  Previous buffer is GPU-safe because we got a new one
```

**DX12 Requirement:**
```
DX12 doesn't have Map(DISCARD) semantic. You must:

1. Pre-allocate N buffers (typically 3)
2. Rotate to next buffer each frame
3. GPU can safely read buffer N while you write buffer N+1

Frame N:
  Your code: "Give me space to write 256 bytes"
  Ring.Allocate(256)
           ↓
         Returns pointer to Buffer[0], offset 0
           ↓
  You write to this memory
           ↓
  GPU reads from Buffer[0] after you're done

Frame N+1:
  Your code: "Give me space to write 256 bytes"
  Ring.Allocate(256)
           ↓
         Returns pointer to Buffer[1], offset 0
         (Rotated to next buffer)
           ↓
  GPU still reading Buffer[0] from frame N
  You writing to Buffer[1] from frame N+1
  ← NO HAZARD
```

**The Key Insight: DX11 is Doing Ring Buffering Under the Hood**

When you call `Map(DISCARD)`, DX11 internally:
1. Marks old buffer as "being used by GPU"
2. Allocates/returns next buffer
3. When GPU finishes with old buffer, recycles it

**UE5's solution: Make the Ring Buffer Explicit**

```cpp
struct FD3D12ConstantBuffer {
    FD3D12FastConstantAllocator& Allocator;  // Manages the ring

    bool Version(FD3D12ResourceLocation& BufferOut) {
        // Request next buffer from ring
        void* Data = Allocator.Allocate(TotalUpdateSize, BufferOut);

        // Copy constants to new buffer
        FMemory::Memcpy(Data, ShadowData.GetData(), TotalUpdateSize);

        return true;
    }
};
```

**Why Explicit is Better:**
1. **DX11 backend** can implement `Allocator.Allocate()` as:
   ```cpp
   void* Allocate(uint32 Size, FD3D12ResourceLocation& Out) {
       D3D11_MAPPED_SUBRESOURCE Mapped;
       Context->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
       Out.Resource = Buffer;  // DX11 handles ring buffer
       return Mapped.pData;
   }
   ```

2. **DX12 backend** can implement it as:
   ```cpp
   void* Allocate(uint32 Size, FD3D12ResourceLocation& Out) {
       FD3D12ResourceLocation Result = RingBuffer.Allocate(Size);
       Out = Result;
       return RingBuffer.GetMappedPointer(Result);
   }
   ```

Both backends work correctly because the **semantic is unified**.

### Why NOT Use a Global Upload Buffer?

**Bad approach:**
```cpp
// One global buffer for all uploads
FRHIBuffer* GlobalUploadBuffer;

void UpdateConstant(const void* Data, uint32 Size) {
    void* Ptr = GlobalUploadBuffer->Map();
    memcpy(Ptr, Data, Size);
    GlobalUploadBuffer->Unmap();  // ← EVERY DRAW CALL
    BindConstantBuffer(GlobalUploadBuffer);
}
```

**Why this fails:**
- Every draw call triggers Map/Unmap overhead
- GPU might still be reading previous frame's data
- No frame-to-frame isolation
- Causes stalls and cache misses

**Ring buffer fixes this:**
- Allocate once per frame (or when buffer overflows)
- Different GPU and CPU buffers each frame
- Zero synchronization overhead
- GPU can pipeline 3 frames efficiently

---

## Pattern 3: Descriptor Cache (Deferred Binding)

### Why This Pattern Exists

**Problem:**
- You want to bind 16 textures
- DX11: Call `PSSetShaderResources(0, 16, SRVArray)`
- DX12: Update descriptor table, call `SetDescriptorTable()`

**Solution: Cache the Last State**

```cpp
class FDescriptorCache {
    TArray<ID3D11ShaderResourceView*> CachedSRVs;

    void SetShaderResources(const TArray<ID3D11ShaderResourceView*>& NewSRVs) {
        // Only call API if changed
        if (CachedSRVs != NewSRVs) {
            Context->PSSetShaderResources(0, NewSRVs.Num(), NewSRVs.GetData());
            CachedSRVs = NewSRVs;  // Update cache
        }
    }
};
```

**Why This Works:**
- Avoids redundant API calls
- Most draws use same textures frame-to-frame
- Descriptor cache hits save huge overhead

### But Why Defer to a Cache?

**Problem with Direct Calls:**
```cpp
// In render pass:
RHI.SetFragmentTexture(0, Texture0);
RHI.SetFragmentTexture(1, Texture1);
RHI.SetFragmentTexture(2, Texture2);
RHI.DrawCall();  // ← Actual binding happens here

// DX11: 3 API calls just before draw
// DX12: Update descriptor table just before draw

// But what if we're doing 1000 draw calls per frame?
// Each SetFragmentTexture() is an API call or heap update!
```

**Deferred Approach:**
```cpp
RHI.SetFragmentTexture(0, Texture0);  // ← Just mark as dirty
RHI.SetFragmentTexture(1, Texture1);  // ← Just mark as dirty
RHI.SetFragmentTexture(2, Texture2);  // ← Just mark as dirty
RHI.DrawCall();
    ↓
    // On first draw with new textures:
    // 1. Check descriptor cache
    // 2. If changed, update GPU state
    // 3. Issue draw call
    ↓
RHI.SetFragmentTexture(0, Texture0);  // ← Same texture, mark dirty
RHI.DrawCall();
    ↓
    // No state change, draw with cached state
```

**Savings:**
- 999 out of 1000 draws skip redundant state changes
- Descriptor cache validates binding layout
- Can batch descriptor heap updates (DX12)

---

## Pattern 4: Explicit Transitions (Resource Barriers)

### Why This Pattern Exists

**Problem:**
- Texture starts as SRV (being read)
- You want to render to it (RTV)
- DX11: Automatic (driver figures it out)
- DX12: You must tell driver explicitly

**Solution: Unified Transition API**

```cpp
// Caller code (backend-neutral)
RHI.Transition(Texture, ERHIAccess::SRVGraphics, ERHIAccess::RTV);
RHI.SetRenderTarget(Texture);
RHI.DrawCall();

// DX11 implementation (validation)
void Transition(FRHITexture* Texture, ERHIAccess Before, ERHIAccess After) {
    // Just track state, no API call
    TextureStates[Texture] = After;

    #if DO_CHECK
        // Validate (fail if incoherent)
        check(IsValidTransition(Before, After));
    #endif
}

// DX12 implementation (barrier)
void Transition(FRHITexture* Texture, ERHIAccess Before, ERHIAccess After) {
    D3D12_RESOURCE_STATES State1 = ConvertToD3D12State(Before);
    D3D12_RESOURCE_STATES State2 = ConvertToD3D12State(After);

    if (State1 != State2) {
        D3D12_RESOURCE_BARRIER Barrier = {};
        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = GetD3D12Resource(Texture);
        Barrier.Transition.StateBefore = State1;
        Barrier.Transition.StateAfter = State2;
        CommandList->ResourceBarrier(1, &Barrier);
    }
}
```

### Why Explicit Transitions Matter

**Without explicit transitions (Bad):**
```cpp
// GPU is reading Texture as SRV
RenderPass1(Texture);  // ← In pixel shader: Texture is SRV

// Now you want to write to Texture
SetRenderTarget(Texture);
RenderPass2();  // ← In pixel shader: Trying to write to RTV

// DX11: Works (driver auto-syncs)
// DX12: DEVICE REMOVED ERROR (undefined behavior)
```

**With explicit transitions (Good):**
```cpp
// GPU is reading Texture as SRV
RenderPass1(Texture);

// Explicitly transition
RHI.Transition(Texture, ERHIAccess::SRVGraphics, ERHIAccess::RTV);
// ← DX12 emits barrier, DX11 validates

// Now you can safely write
SetRenderTarget(Texture);
RenderPass2();

// DX11: Works (state was validated)
// DX12: Works (barrier was issued)
```

### Why This Pattern Prevents Segfaults in DX12

The segfault in UzEngine DX11 backend is because:
1. Render graph assumes transitions are happening
2. UzEngine hasn't implemented transition API
3. GPU resource state is incoherent
4. DX12 sees invalid state → DEVICE REMOVAL → crash

---

## Pattern 5: State Cache Flush Points

### Why Deferred Execution

**Principle: Decouple Collection from Execution**

```cpp
// Bad: Immediate execution
void SetFragmentTexture(uint32 Index, FRHITexture* Texture) {
    Context->PSSetShaderResources(Index, 1, &SRV);  // ← Executes immediately
}

// Good: Deferred execution
void SetFragmentTexture(uint32 Index, FRHITexture* Texture) {
    BoundTextures[Index] = Texture;  // ← Just record
    DirtyMask |= (1 << Index);       // ← Mark as needing update
}

void FlushState() {
    if (DirtyMask == 0) return;  // ← Nothing changed

    // Issue all state changes at once
    Context->PSSetShaderResources(0, 16, BoundTextures);
    DirtyMask = 0;
}
```

**Why This Works:**
1. **Batching**: 10 `SetFragmentTexture()` calls = 1 API call
2. **Validation**: Can check bindings for consistency before execution
3. **Optimization**: Can skip calls if nothing changed
4. **Coordination**: Ensures barriers happen before shader execution

**Flush Points:**
- Before draw call
- Before compute dispatch
- Before render pass transition
- End of frame

---

## Summary: The Core Philosophy

UE5's RHI abstraction is built on these core principles:

1. **Separate Concerns**: Collection (WHAT) vs Execution (HOW)
2. **Deferred Execution**: Mark dirty, flush at sync points
3. **Explicit Synchronization**: Make GPU dependencies visible
4. **Caching**: Avoid redundant API calls through state tracking
5. **Ring Buffers**: Eliminate Map/Discard stalls through rotation

These patterns work because they address the fundamental differences between DX11 (implicit, forgiving) and DX12 (explicit, dangerous) while maintaining a single code path for the application.

