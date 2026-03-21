# DX12/Vulkan/Metal Types Above RHI Boundary

**Category**: architecture
**Tags**: rhi, dx12, vulkan, metal, abstraction-leak, legacy

## Description

Using graphics API types (ID3D12Device*, VkBuffer, MTLTexture) anywhere above the RHI backend files (rhi_dx12.c, rhi_vulkan.c, rhi_metal.c). This creates tight coupling to a specific backend and makes multi-backend support impossible. The legacy src/renderer/ directory is a major violator with 20+ ComPtr<ID3D12*> members in DX12Renderer class.

## Example

// WRONG — DX12 types in renderer\nclass DX12Renderer {\n    ComPtr<ID3D12Device> device_; // FORBIDDEN above rhi.h\n};\n\n// WRONG — gizmo interface takes DX12 types\nvirtual void Init(ID3D12Device* device); // Blocks Vulkan/Metal\n\n// CORRECT — use RHI handles\nclass Renderer {\n    RhiDevice* device_; // Opaque, backend-agnostic\n};

## When to Use

When reviewing any code that interacts with GPU resources. When adding new render passes or systems.
