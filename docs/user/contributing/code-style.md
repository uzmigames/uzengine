# Code Style

UzEngine follows Clean Code and SOLID principles. This page summarizes the
conventions enforced across the codebase.

## Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Types (class, struct, enum) | PascalCase | `DX12Device`, `RHIFormat`, `Vec3` |
| Functions / methods | PascalCase | `CreateBuffer()`, `GetWidth()` |
| Variables | camelCase | `frameIndex`, `deviceDesc` |
| Constants | kPascalCase | `kMaxDescriptors`, `kFramesInFlight` |
| Enum values | PascalCase | `RHIFormat::RGBA8Unorm` |
| Macros | UPPER_SNAKE | `UZ_LOG`, `UZ_CHECK` |
| Member variables | m_camelCase | `m_device`, `m_bufferPool` |
| Files | snake_case | `platform_window.cpp`, `dx12_device.h` |

## Documentation

Triple-slash (`///`) for all public types, methods, and functions:

```cpp
/// Creates a GPU buffer with the given descriptor.
/// Returns an invalid handle on failure.
RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc);
```

Section separators for logical groupings:

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// Buffer Operations
// ─────────────────────────────────────────────────────────────────────────────
```

Inline comments only for **why**, never for **what**:

```cpp
x += 1;  // Skip sentinel value at index 0 ← good
x += 1;  // increment x ← bad (obvious)
```

## File Organization

- **Max 1500 lines per file** — split proactively at ~1000
- **One responsibility per file** — `dx12_device.cpp` does device init, not resource creation
- **Directory by topic** — `engine/logging/`, `engine/rhi/dx12/`, `platform/windows/`
- **Tests mirror source** — `tests/engine/test_logging.cpp`, `tests/engine/rhi/test_rhi_config.cpp`

## Functions

- Each function does **one thing** — max ~40 lines
- `Get*()` methods must not modify state
- Validate inputs at boundaries, use `UZ_CHECK` / `UZ_ENSURE` for invariants
- No magic numbers — use `constexpr` named constants

## SOLID

| Principle | Application |
|-----------|-------------|
| **Single Responsibility** | One class = one reason to change |
| **Open/Closed** | New backends extend via `IRHIDevice`, not by modifying existing code |
| **Liskov Substitution** | Any RHI backend is interchangeable through the abstract interface |
| **Interface Segregation** | `IRHIDevice`, `IRHICommandList`, `IRHIFence` are separate interfaces |
| **Dependency Inversion** | Renderer depends on `rhi.h` abstractions, never on `dx12_device.h` |

## Formatting

Enforced by `.clang-format`. No manual overrides. Key settings:
- 4-space indentation
- 120-character column limit
- Braces on same line for functions/control flow
- No trailing whitespace
