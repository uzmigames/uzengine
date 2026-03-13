# Rendering — Vertex Layout & Format System

> **Status**: Planned
> **Source**: `runtime/engine/renderer/`, `runtime/engine/rhi/`
> **Depends on**: RHI (§03), Mesh Pipeline (§08-06)

## Purpose

The Vertex Layout system provides a single source of truth for vertex structure across all rendering code. Instead of hardcoding `RHIInputElementDesc[]` arrays in each demo and render pass, the vertex format is derived automatically from a simple flags bitmask. This eliminates stride mismatches, duplication, and manual synchronization between CPU and GPU definitions.

## Current State: Hardcoded Duplication

Today, every render pass and demo repeats vertex layout:

```cpp
// In pbr_test_demo.cpp
const RHIInputElementDesc elements[] = {
    {"POSITION", 0, RHIFormat::R32G32B32Float, 0, 0, RHIInputClassification::PerVertex},
    {"NORMAL",   0, RHIFormat::R32G32B32Float, 0, 12, RHIInputClassification::PerVertex},
    {"TANGENT",  0, RHIFormat::R32G32B32Float, 0, 24, RHIInputClassification::PerVertex},
    {"TEXCOORD", 0, RHIFormat::R32G32Float,    0, 36, RHIInputClassification::PerVertex},
};
// Stride = 44 bytes

// In deferred_test_demo.cpp (identical, but maintainers don't know it's identical)
const RHIInputElementDesc elements[] = {
    {"POSITION", 0, RHIFormat::R32G32B32Float, 0, 0, RHIInputClassification::PerVertex},
    {"NORMAL",   0, RHIFormat::R32G32B32Float, 0, 12, RHIInputClassification::PerVertex},
    {"TANGENT",  0, RHIFormat::R32G32B32Float, 0, 24, RHIInputClassification::PerVertex},
    {"TEXCOORD", 0, RHIFormat::R32G32Float,    0, 36, RHIInputClassification::PerVertex},
};

// In forward_pbr_pass.cpp (same again)
// ... repeated 4 times across the codebase
```

**Problems**:
- Stride manually calculated and duplicated (48 bytes, 44 bytes, 52 bytes — inconsistency)
- Adding a new channel (e.g., UV1) requires editing 4+ files
- Offset calculation by hand is error-prone (12, 24, 36... what if someone forgets?)
- No enum to validate supported formats

## Planned API

### VertexFormatFlags Bitmask

```cpp
/// Bitmask of supported vertex attributes.
enum class VertexFormatFlags : uint32_t {
    HasPosition  = 1u << 0,   // Vec3 position (required, always present)
    HasNormal    = 1u << 1,   // Vec3 normal (world-space)
    HasTangent   = 1u << 2,   // Vec3 tangent (for normal maps)
    HasBitangent = 1u << 3,   // Vec3 bitangent (derived from normal + tangent if needed)
    HasUV0       = 1u << 4,   // Vec2 texture coordinate 0
    HasUV1       = 1u << 5,   // Vec2 texture coordinate 1 (lightmap, detail)
    HasUV2       = 1u << 6,   // Vec2 texture coordinate 2
    HasColor     = 1u << 7,   // Vec4 color (RGBA, vertex painting)
};
```

### VertexLayout Class

```cpp
/// Single source of truth for vertex structure: attributes, stride, input elements.
/// Automatically computes offsets and validates channel compatibility.
class VertexLayout {
public:
    /// Standard PBR vertex: position + normal + tangent + uv0.
    /// Stride: 48 bytes (12 + 12 + 12 + 8 + 4 padding)
    static VertexLayout Standard();

    /// Construct layout from flags bitmask.
    /// Automatically computes stride and input element descriptors.
    static VertexLayout FromFlags(VertexFormatFlags flags);

    /// Returns the computed stride in bytes (e.g., 48 for Standard()).
    uint32_t GetStride() const;

    /// Returns the computed input element descriptors for RHI.
    /// Use this to build the input layout in D3D12.
    const std::vector<RHIInputElementDesc>& GetElements() const;

    /// Returns the flags used to construct this layout.
    VertexFormatFlags GetFlags() const;

    /// Returns the byte offset of a given channel (or SIZE_MAX if not present).
    /// Useful for introspection: "Where does my normal data start?"
    uint32_t GetOffset(VertexFormatFlags channel) const;
};
```

### Helper Functions

```cpp
/// Computes the byte size of a single attribute.
/// position/normal/tangent = 12, uv0/uv1/uv2 = 8, color = 16
uint32_t GetAttributeSize(VertexFormatFlags channel);

/// Computes stride from flags (sum of all attribute sizes, 4-byte aligned).
uint32_t GetVertexStride(VertexFormatFlags flags);

/// Validates that flags represent a valid vertex layout.
/// Returns reason on failure (e.g., "position is required").
bool ValidateVertexFormat(VertexFormatFlags flags, std::string* error = nullptr);
```

## Usage Examples

### Before (Hardcoded)

```cpp
// In pbr_test_demo.cpp — 10 lines of manual labor
const RHIInputElementDesc elements[] = {
    {"POSITION", 0, RHIFormat::R32G32B32Float, 0, 0, RHIInputClassification::PerVertex},
    {"NORMAL",   0, RHIFormat::R32G32B32Float, 0, 12, RHIInputClassification::PerVertex},
    {"TANGENT",  0, RHIFormat::R32G32B32Float, 0, 24, RHIInputClassification::PerVertex},
    {"TEXCOORD", 0, RHIFormat::R32G32Float,    0, 36, RHIInputClassification::PerVertex},
};
const uint32_t stride = 48;  // Manual calculation, error-prone
```

### After (VertexLayout)

```cpp
// In pbr_test_demo.cpp — 2 lines, automatic
VertexLayout layout = VertexLayout::Standard();
const auto& elements = layout.GetElements();
const uint32_t stride = layout.GetStride();  // Automatically 48

// Create input layout in D3D12
D3D12_INPUT_LAYOUT_DESC input_layout = {
    .pInputElementDescs = elements.data(),
    .NumElements = (uint32_t)elements.size(),
};
m_device->CreateInputLayout(&input_layout);
```

### Custom Vertex Format

```cpp
// Use flags to define custom layouts on the fly
VertexFormatFlags flags = VertexFormatFlags::HasPosition
                        | VertexFormatFlags::HasNormal
                        | VertexFormatFlags::HasUV0
                        | VertexFormatFlags::HasUV1;  // Add lightmap UV

VertexLayout layout = VertexLayout::FromFlags(flags);
uint32_t stride = layout.GetStride();  // 12 + 12 + 8 + 8 = 40

// Validate before using (safety check)
if (layout.GetOffset(VertexFormatFlags::HasUV1) == SIZE_MAX) {
    UZ_LOG(Renderer, Error, "Layout does not have UV1 channel");
    return false;
}
```

## Implementation Plan

```cpp
// runtime/engine/renderer/vertex_layout.h
#ifndef UZ_VERTEX_LAYOUT_H
#define UZ_VERTEX_LAYOUT_H

enum class VertexFormatFlags : uint32_t {
    HasPosition  = 1u << 0,
    HasNormal    = 1u << 1,
    HasTangent   = 1u << 2,
    HasBitangent = 1u << 3,
    HasUV0       = 1u << 4,
    HasUV1       = 1u << 5,
    HasUV2       = 1u << 6,
    HasColor     = 1u << 7,
};

class VertexLayout {
private:
    VertexFormatFlags flags_;
    std::vector<RHIInputElementDesc> elements_;
    uint32_t stride_;
    std::unordered_map<uint32_t, uint32_t> channel_offsets_;

    /// Internal: compute stride and offsets from flags
    void ComputeLayout();

public:
    static VertexLayout Standard();
    static VertexLayout FromFlags(VertexFormatFlags flags);

    uint32_t GetStride() const { return stride_; }
    const std::vector<RHIInputElementDesc>& GetElements() const { return elements_; }
    VertexFormatFlags GetFlags() const { return flags_; }
    uint32_t GetOffset(VertexFormatFlags channel) const;
};

uint32_t GetAttributeSize(VertexFormatFlags channel);
uint32_t GetVertexStride(VertexFormatFlags flags);
bool ValidateVertexFormat(VertexFormatFlags flags, std::string* error = nullptr);

#endif // UZ_VERTEX_LAYOUT_H
```

## Vertex Struct Definition

The CPU vertex struct mirrors the GPU input layout:

```cpp
struct Vertex {
    Vec3 position;   // 12 bytes, offset 0
    Vec3 normal;     // 12 bytes, offset 12
    Vec3 tangent;    // 12 bytes, offset 24
    Vec2 uv0;        // 8 bytes,  offset 36
    // Total: 44 bytes (requires 4-byte alignment padding)
};
// static_assert(sizeof(Vertex) == 48);
```

## Rules

1. **NEVER hardcode `RHIInputElementDesc[]` in demos or passes** — ALWAYS use `VertexLayout::Standard()` or `VertexLayout::FromFlags()`
2. **NEVER calculate stride by hand** — ALWAYS call `layout.GetStride()`
3. **ALWAYS validate custom formats** — call `ValidateVertexFormat()` before using flags
4. **ALWAYS keep CPU `Vertex` struct in sync with VertexLayout** — use `static_assert(sizeof(Vertex) == VertexLayout::Standard().GetStride())`

## Phase Roadmap

| Phase | What Ships |
|-------|-----------|
| **1.5** | VertexLayout class, Standard() + FromFlags(), input layout generation for D3D12 |
| **2.0** | GPU buffer binding with VertexLayout stride, vertex instancing support |
| **2.5** | Dynamic vertex format detection from mesh import, runtime format negotiation |
