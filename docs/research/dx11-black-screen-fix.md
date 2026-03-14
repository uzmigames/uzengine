# DX11 Black Screen Fix — Quick Reference

## TL;DR: Your Problem

You're using deferred contexts. **UE5 doesn't**. That's your bug.

**UE5 Pattern**: Immediate context only, DYNAMIC CBs, Map/Discard, 3-frame pooling.

**UzEngine Current**: Deferred contexts, likely DEFAULT CBs, UpdateSubresource, no pooling.

---

## The Fix (Quick Steps)

### 1. Kill Deferred Contexts

```cpp
// DELETE THIS:
ID3D11DeviceContext* DeferredCtx = nullptr;
Device->CreateDeferredContext(0, &DeferredCtx);
ID3D11CommandList* CmdList = nullptr;
DeferredCtx->FinishCommandList(FALSE, &CmdList);
ImmediateCtx->ExecuteCommandList(CmdList, FALSE);

// USE THIS INSTEAD:
// Everything goes to ImmediateCtx directly
```

### 2. Constant Buffer Creation

```cpp
// WRONG (yours):
D3D11_BUFFER_DESC Desc = { ... };
Desc.Usage = D3D11_USAGE_DEFAULT;
D3D11_SUBRESOURCE_DATA Data = { YourData, ... };
Device->CreateBuffer(&Desc, &Data, &CB);

// RIGHT (UE5):
D3D11_BUFFER_DESC Desc = { ... };
Desc.Usage = D3D11_USAGE_DYNAMIC;                    // ← KEY
Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;       // ← KEY
Device->CreateBuffer(&Desc, nullptr, &CB);          // ← NULL initial data
```

### 3. Constant Buffer Upload

```cpp
// WRONG (yours, probably):
ImmediateCtx->UpdateSubresource(CB, 0, nullptr, Data, 0, 0);

// RIGHT (UE5):
D3D11_MAPPED_SUBRESOURCE Mapped;
ImmediateCtx->Map(CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);  // ← D3D11_MAP_WRITE_DISCARD
memcpy(Mapped.pData, YourData, Size);
ImmediateCtx->Unmap(CB, 0);                          // ← Unmap releases lock

// Then:
ImmediateCtx->VSSetConstantBuffers(Slot, 1, &CB);   // ← AFTER upload
```

### 4. Pooling (Optional But Recommended)

```cpp
// Per-frame, call at frame start:
UniformBufferBeginFrame();

// When you need a CB:
ID3D11Buffer* CB = CreateAndUpdatePooledUniformBuffer(Device, ImmediateCtx, Data, Size);

// CB is returned from pool or created new
// Map/Discard happens inside
// Bind and draw
// At frame end, return CB to SafeUniformBufferPools[current_frame_index]
// 3 frames later, it moves to main pool for reuse
```

---

## Why It's Black Screen

### Scenario A: Deferred Context Recording

```cpp
// Thread 1: Recording
DeferredCtx->ClearRenderTargetView(RTV, ClearColor);  // Recorded, not executed yet
DeferredCtx->VSSetConstantBuffers(0, 1, &CB);         // Recorded
DeferredCtx->DrawIndexed(...);                        // Recorded

CmdList = DeferredCtx->FinishCommandList();

// Thread 2: Executing
ImmediateCtx->ExecuteCommandList(CmdList, FALSE);     // Now GPU executes
                                                       // But GPU might race with your CB upload!
Present();                                             // Too early, GPU didn't finish
```

**Problem**: `ExecuteCommandList()` doesn't stall for GPU completion. If your CB data is still being uploaded on the immediate context while the deferred commands execute, you get garbage or uninitialized data.

### Scenario B: Uploading After Binding

```cpp
ImmediateCtx->VSSetConstantBuffers(0, 1, &CB);       // CB is empty!
ImmediateCtx->DrawIndexed(...);                       // Shader reads garbage from CB
                                                       // Result: black screen or random colors

D3D11_MAPPED_SUBRESOURCE Mapped;
ImmediateCtx->Map(CB, ...);                           // Too late, draw already submitted
memcpy(Mapped.pData, Data, Size);
ImmediateCtx->Unmap(CB);
```

### Scenario C: DEFAULT Buffer with Map

```cpp
D3D11_BUFFER_DESC Desc = { ... };
Desc.Usage = D3D11_USAGE_DEFAULT;
Device->CreateBuffer(&Desc, nullptr, &CB);

D3D11_MAPPED_SUBRESOURCE Mapped;
ImmediateCtx->Map(CB, 0, D3D11_MAP_WRITE, 0, &Mapped);  // ERROR! Can't map DEFAULT
```

DEFAULT buffers can't be mapped CPU-side. You need DYNAMIC for per-frame updates.

---

## Symptom Checklist

Check if you have these issues:

- [ ] Black backbuffer after clear (but before any draw)
  → CB binding broken, or buffer not uploaded

- [ ] Black backbuffer only with certain demos
  → Some demos use CBs, others don't (like clear-only or triangle without CB)

- [ ] Debug output shows constant buffer upload but no data
  → Likely uploading to wrong buffer or to DEFAULT usage buffer

- [ ] Crash on Map() call
  → Trying to map DEFAULT buffer

- [ ] Flickering/random colors
  → Race condition: GPU reading CB while you're uploading, or uploading AFTER bind

- [ ] Clear works but triangle with CB doesn't
  → CB binding or upload broken, draw submits before data ready

---

## Test: Verify the Fix

### Test 1: Simple CB Value

```hlsl
// Simple VS that outputs CB value to screen
cbuffer TestCB : register(b0)
{
    float4 Value;
};

float4 main(uint VertexID : SV_VertexID) : SV_POSITION
{
    // If Value.x is positive, render in specific location
    // This proves CB was uploaded
    return Value;
};
```

```cpp
// C++
float Value[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

// Upload
D3D11_MAPPED_SUBRESOURCE Mapped;
Ctx->Map(CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
memcpy(Mapped.pData, Value, sizeof(Value));
Ctx->Unmap(CB, 0);

// Bind
Ctx->VSSetConstantBuffers(0, 1, &CB);

// Draw
Ctx->DrawIndexed(3, 0, 0);

// Present and check: if backbuffer is NOT black, CB upload worked
```

### Test 2: Pool Recycling

```cpp
// Frame 0
ID3D11Buffer* CB0 = CreateAndUpdatePooledUniformBuffer(Device, Ctx, Data0, Size);
Ctx->VSSetConstantBuffers(0, 1, &CB0);
Ctx->DrawIndexed(3, 0, 0);
// CB0 freed, goes to SafeUniformBufferPools[0]

// Frame 1
ID3D11Buffer* CB1 = CreateAndUpdatePooledUniformBuffer(Device, Ctx, Data1, Size);
// CB1 should be new (pool[0] still waiting)

// Frame 2
ID3D11Buffer* CB2 = CreateAndUpdatePooledUniformBuffer(Device, Ctx, Data2, Size);
// CB2 should be new

// Frame 3
ID3D11Buffer* CB3 = CreateAndUpdatePooledUniformBuffer(Device, Ctx, Data3, Size);
// CB3 should be new

// Frame 4 (after 3 frames)
ID3D11Buffer* CB4 = CreateAndUpdatePooledUniformBuffer(Device, Ctx, Data4, Size);
// CB4 might be CB0 (recycled from pool[0] now available)

// Check: CB4 address should match CB0 if recycling worked
assert(CB4 == CB0);  // Pool recycles!
```

---

## Reference: UE5 Source Files

All analysis from actual UE5 5.6 code:

1. **Pooling Logic**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:113-168`
   - `CreateAndUpdatePooledUniformBuffer()` — pool lookup, create, Map/Discard, upload

2. **State Cache Binding**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11StateCachePrivate.h:350-366`
   - `SetConstantBuffer()` — cached binding dispatch

3. **Pool Cycling**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp:59-104`
   - `UniformBufferBeginFrame()` — frame-start pool rotation

4. **Immediate Context Only**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11RHIPrivate.h:718`
   - `TRefCountPtr<FD3D11DeviceContext> Direct3DDeviceIMContext;` — no deferred

5. **RHI End Frame**: `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11RHI.cpp:26-100`
   - `RHIEndFrame()` — completion query, no explicit Flush needed

---

## Implementation Order

1. **Remove deferred contexts first** (highest impact)
   - Everything to immediate context
   - Delete ExecuteCommandList calls

2. **Fix CB creation** (next highest impact)
   - DYNAMIC + CPU_ACCESS_WRITE
   - NULL initial data

3. **Fix CB upload** (critical for correctness)
   - Map(DISCARD) instead of UpdateSubresource
   - Always BEFORE binding

4. **Add state cache** (optional optimization)
   - Avoids redundant D3D11 calls

5. **Implement pooling** (performance optimization)
   - Can add later, doesn't affect correctness

**If you fix #1 + #2 + #3, black screen should disappear.**

---

## Final Checklist

- [ ] No deferred contexts in code
- [ ] All work on immediate context
- [ ] All CBs created with D3D11_USAGE_DYNAMIC
- [ ] All CBs uploaded with Map(DISCARD)/Unmap
- [ ] CB binding happens AFTER upload
- [ ] Pool cycle happens at frame start
- [ ] Pool reuse respects 3-frame delay
- [ ] Test: simple CB value renders correctly

Done!
