# Security & Memory Safety

> **Status**: Reference
> **Overview**: UzEngine uses strict patterns for resource lifetime management, memory safety, and secure compilation. This section documents the conventions that prevent crashes, leaks, and security issues.

## Contents

### [01 — RAII Ownership Conventions](./01-raii-ownership.md)

GPU and CPU resource lifetime management using RAII patterns. Covers:
- Smart pointers for temporary objects (`ComPtr<T>`, `Own<T>`)
- Explicit destroy pattern for GPU resources in C-style structs
- Descriptor heap allocation and bounds checking
- Double-free and use-after-free prevention
- Fence overflow safety and memory barriers

**Key rule**: GPU resources in C-style structs (DX12Buffer, DX12Texture, DX12Pipeline) use raw pointers for ABI compatibility. Their lifetimes are managed via explicit `Destroy()` calls, not destructors.

### [02 — Shader Compilation & Bytecode Safety](./02-shader-security.md)

Safe shader compilation and bytecode validation. Covers:
- Include path security and symlink traversal prevention
- Bytecode size validation (max 64 MB)
- Debug layer filtering to prevent message queue exhaustion
- Error message handling and end-user privacy (don't expose file paths)
- Compilation failure fallback strategies

**Key rule**: Shader include paths are hardcoded from build configuration, never user-supplied. Compilation errors are logged in full (with absolute paths) but never exposed to players.

## Design Principles

1. **Deterministic Lifetime Management** — No garbage collector. Every resource has a clear owner and destruction point.
2. **Explicit Over Implicit** — Raw pointers in C-style structs make it explicit that you must call `Destroy()`. Implicit cleanup in destructors can be unsafe for GPU resources.
3. **Fail Safe** — Resource allocation failures are checked and handled gracefully. Out-of-memory doesn't crash the engine.
4. **Defensive Coding** — Bounds checks, null checks, and precondition validation catch bugs before they cause corruption.
5. **Developer Debugging > End-User Privacy** — Logs can contain absolute paths and source structure for developer debugging, but this information is never exposed to players.

## Patterns Used Throughout

### Smart Pointer Pattern (CPU Objects)

```cpp
ComPtr<ID3DBlob> blob;  // Temporary COM object — auto-release
Own<Mesh> mesh = MakeOwn<Mesh>(...);  // Heap object — auto-delete
```

### Explicit Destroy Pattern (GPU Resources)

```cpp
DX12Buffer buf = device->CreateBuffer(desc);  // Returns struct with raw pointer
// ... use buffer ...
device->DestroyBuffer(buf);  // Explicit cleanup
```

### Validation Before Use

```cpp
UZ_CHECKF(handle.IsValid(), "Invalid buffer handle");
UZ_ENSURE(bytecode.size() <= kMaxSize, "Bytecode too large");
```

## Relationship to Other Sections

- **[Core — Memory Management](../01-core/05-memory.md)** — `Own<T>`, `Ref<T>`, allocator patterns
- **[RHI — Device & Resources](../03-rhi/03-device-resources.md)** — detailed resource creation/deletion
- **[Shader Pipeline — Compilation](../05-shader-pipeline/03-compilation.md)** — shader compilation process
- **[Observability — Log System](../04-observability/01-log-system.md)** — logging and what to expose/hide
