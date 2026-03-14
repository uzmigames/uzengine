# DX11 vs DX12 Quick Reference — UzEngine RHI

## Constant Buffer Binding

| Aspect | DX12 | DX11 | UzEngine RHI |
|--------|------|------|--------------|
| **Binding method** | Root CBV (GPU VA) | VSSetConstantBuffers + scratch buffer | `SetRootCBV(slot, gpuVA)` or `SetRootCBV(slot, buffer, offset, cpuData, size)` |
| **Data location** | VRAM (persistent) | VRAM (ephemeral scratch) | Caller provides both |
| **CPU involvement** | None (GPU VA only) | Map/Unmap every frame | DX11 impl handles mapping |
| **Alignment** | 256-byte offset | 16-byte size | Enforced by RHI impl |
| **Buffer usage** | Constant/UploadHeap | DYNAMIC + WRITE_DISCARD | DX11 impl creates DYNAMIC |
| **Lifecycle** | Persistent (upload heap) | Per-frame (scratch) | Handled by backend |

**When to use which:**
- Use `SetRootCBV(gpuAddress)` if you have a persistent GPU buffer (DX12 efficient path, DX11 no-op)
- Use `SetRootCBV(buffer, offset, cpuData, size)` for material/per-draw data (supports both backends)

---

## Texture Binding

| Aspect | DX12 SM6.6 | DX12 SM5 | DX11 | UzEngine RHI |
|--------|-----------|----------|------|--------------|
| **Method** | Bindless heap indices | Descriptor tables | Slot binding | `SetDescriptorIndex()` / `SetDescriptorTable()` / `SetFragmentTexture()` |
| **Binding unit** | Push constant (uint32) | GPU descriptor table | Register slot (t0-t15) | Abstracted |
| **Shader access** | `ResourceDescriptorHeap[index]` | `textures[i]` array | `register(t0)`, `register(t1)`, etc | Per-shader |
| **Per-draw cost** | 1 push constant | 1 descriptor copy | 1 PSSetShaderResources | Abstracted |
| **Max textures** | Unlimited | 256+ (configurable) | 16 (D3D11 limit) | Backend-dependent |
| **Descriptor management** | Global heap | Staging block | None | DX11 uses pools |

**Decision tree:**
```
if (device->IsBindless()) {
    // SM6.6+ path
    uint32_t idx = device->GetTextureSRVIndex(texture);
    cmdList->SetDescriptorIndex(0, idx);
} else {
    // DX11 / SM5 path
    cmdList->SetFragmentTexture(slot, texture);  // Simpler, slot-based
}
```

---

## Sampler Binding

| Aspect | DX12 | DX11 |
|--------|------|------|
| **How to bind** | Static samplers in root signature | `PSSetSamplers()` per-draw |
| **Flexibility** | Fixed at PSO creation | Dynamic per-draw |
| **Count** | ~8-16 static | Can change all 16 slots dynamically |
| **Shader declares** | References root sig slot | `SamplerState s0 : register(s0);` |
| **Cost** | Zero (baked into PSO) | One API call per-draw |

**UzEngine approach:**
- Bind default samplers at `Begin()`: linear-wrap (s0), shadow comparison (s1)
- Additional samplers bound per-draw if needed
- Consider shader variants for different filter/address modes

---

## Resource Barriers

| Aspect | DX12 | DX11 |
|--------|------|------|
| **API** | `ResourceBarrier()` with explicit states | No API (implicit) |
| **Before/After states** | Explicit transitions | Driver infers |
| **Required** | Yes (validation layer enforces) | No (driver handles) |
| **Performance impact** | Can be severe if wrong | Auto-optimized by driver |
| **Debugging** | Validation layer errors | Must test on real hardware |

**UzEngine RHI:**
- DX12: Translates `RHIBarrierDesc` → `D3D12_RESOURCE_BARRIER`
- DX11: No-op (intentional), but can log warnings for unsafe patterns

---

## Command List Lifecycle

| Phase | DX12 | DX11 |
|-------|------|------|
| **Create** | Device owns allocators | Device owns immediate context |
| **Begin()** | Reset allocator + open list | Fetch immediate context pointer |
| **Record** | Calls to cmdList methods | Same, but immediate execution |
| **End()** | Close recording | No-op (already executing) |
| **Execute()** | Submit to queue + return | No-op (already on GPU) |
| **Sync** | Use fences for GPU-CPU sync | Fences still used for frame pacing |

**Key difference:** DX11 executes commands **immediately as recorded** (immediate context model), while DX12 records then submits.

---

## Scratch Constant Buffer Pattern (DX11 Only)

**Why needed:** DX11 can't use GPU virtual addresses like DX12. Must use Map/Unmap pattern.

**Pattern:**
```cpp
// Once per slot
ID3D11Buffer* scratchCB = CreateBuffer({
    .ByteWidth = alignedSize,
    .Usage = D3D11_USAGE_DYNAMIC,
    .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
});

// Per-draw
D3D11_MAPPED_SUBRESOURCE mapped;
ctx->Map(scratchCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
std::memcpy(mapped.pData, cpuData, dataSize);
ctx->Unmap(scratchCB, 0);
ctx->VSSetConstantBuffers(slot, 1, &scratchCB);
```

**Key**: `WRITE_DISCARD` flag tells driver to give you a fresh GPU buffer (ring-buffer pattern), avoiding stall.

**UzEngine impl:** `DX11CommandList::GetOrCreateScratchCB(slot, size)` handles this.

---

## Pool Lookup Pattern (DX11 Specific)

**Texture pool lookup (SetDescriptorTable):**
```cpp
ID3D11ShaderResourceView* srv = device->FindTextureSRVByIndex(srvIndex);
if (srv) {
    ctx->PSSetShaderResources(0, 1, &srv);  // Bind to t0
}
```

**Requirement:**
- Texture pool must be **non-empty** before first draw
- Every texture must have a valid SRV if created with `ShaderResource` usage
- SRV indices must be assigned sequentially starting at 0

**Validation:**
```cpp
if (srvIndex >= device->m_nextSrvIndex) {
    // Index out of range — texture not created yet?
    UZ_LOG(Error, "Invalid SRV index {}", srvIndex);
    return;
}
```

---

## Comparison: UE5 vs Unity vs UzEngine

### Constant Buffer Binding

| Engine | DX12 | DX11 |
|--------|------|------|
| **UE5** | Root CBV with offset | Shadow buffer + UpdateSubresource (per-frame copy) |
| **Unity** | Material CB (persistent upload) | Material CB (CPU-side mirror, per-frame copy) |
| **UzEngine** | SetRootCBV(gpuVA) | SetRootCBV(cpuData, size) → scratch CB |

**Winner:** UzEngine's API is **most explicit** about the DX11 scratch pattern.

### Texture Binding

| Engine | Approach |
|--------|----------|
| **UE5** | Switch on backend, use raw API directly (DX11: IASetVertexBuffers, DX12: SetGraphicsRoot) |
| **Unity** | Material property system + SetTexture API (hides backend) |
| **UzEngine** | RHI abstraction with per-backend overrides (explicit) |

**Winner:** UzEngine **avoids switch-on-type**, Unity **avoids exposure of API differences**.

### Validation

| Engine | Approach |
|--------|----------|
| **UE5** | Sparse, relies on driver validation layer |
| **Unity** | Comprehensive (SerializationCallbackReceiver pattern) |
| **UzEngine** | Missing pre-flight validation (opportunity for improvement) |

**Recommendation:** Add pre-flight checks to render_graph_pass execution.

---

## DX11 Crash Diagnosis Flowchart

```
Segfault in DX11 first frame?

├─ Is it in SetRootCBV?
│  ├─ Check: Is m_ctx == nullptr? → Init failed, check GetImmediateContext
│  ├─ Check: Is cpuData == nullptr? → Render graph passing bad data
│  └─ Check: Did GetOrCreateScratchCB return nullptr? → Device->CreateBuffer failed

├─ Is it in SetDescriptorTable?
│  ├─ Check: Is m_ctx == nullptr? → Same as above
│  ├─ Check: Is descriptorIndex >= m_nextSrvIndex? → Index out of range
│  └─ Check: Did FindTextureSRVByIndex return nullptr? → Texture has no SRV

├─ Is it in SetFragmentTexture?
│  ├─ Check: Is m_ctx == nullptr? → Same as above
│  └─ Check: Does texture have valid SRV? → Check DX11Texture::srv != nullptr

└─ Is it somewhere else in Begin()/Draw()?
   └─ Check: All resource lookups (texture pool, pipeline pool, sampler pool)
```

**Most likely:** Null immediate context or invalid pool lookup. Add validation and logging.

---

## Checklist for DX11 Correctness

Before shipping DX11 support:

- [ ] Device Init creates and caches immediate context
- [ ] All resource pools (buffer, texture, shader, sampler, pipeline) pre-allocated or have null checks
- [ ] SetRootCBV validates cpuData and dataSize before creating scratch CB
- [ ] GetOrCreateScratchCB returns non-nullptr (or logs error and skips draw)
- [ ] SetDescriptorTable validates descriptorIndex < m_nextSrvIndex
- [ ] FindTextureSRVByIndex validates returned SRV is not nullptr
- [ ] Begin() validates m_ctx is not nullptr
- [ ] Render graph pre-validates all texture handles before first draw
- [ ] Sampler state is bound once at Begin() for s0 (default) and s1 (shadow)
- [ ] ResourceBarrier is a no-op (intentional, not a bug)
- [ ] All Map calls check HRESULT and log on failure
- [ ] Scratch CBs are only created DYNAMIC (not IMMUTABLE)
- [ ] Frame pacing uses fences (same as DX12)

---

## References

**Files in UzEngine:**
- `runtime/engine/rhi/rhi.h` — RHI interface contract
- `runtime/engine/rhi/dx11/dx11_command_list.cpp` — DX11 impl
- `runtime/engine/rhi/dx11/dx11_device.cpp` — DX11 device & resource pools
- `runtime/engine/rhi/dx12/dx12_command_list.cpp` — DX12 impl (for comparison)

**External References:**
- [D3D11 Constant Buffers](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-buffers-constant-how-to)
- [D3D11 Shader Reflection](https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nn-d3d11shader-id3d11shaderreflection)
- [D3D12 Root Signatures](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures)
- [DX11 vs DX12 Optimization Guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/notes-on-various-apis)
