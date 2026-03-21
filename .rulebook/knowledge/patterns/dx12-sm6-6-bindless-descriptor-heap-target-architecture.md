# DX12 SM6.6 Bindless Descriptor Heap (Target Architecture)

**Category**: architecture
**Tags**: dx12, bindless, descriptors, sm6.6, rendering, target

## Description

Target architecture for descriptor management: single global CBV_SRV_UAV descriptor heap (1M slots). Resources get persistent descriptor index on creation. Shaders access via ResourceDescriptorHeap[index] — no per-draw descriptor table swaps. This eliminates the biggest DX12 CPU overhead and resolves the current root signature saturation (64/64 DWORDs). Currently blocked behind task 3.41.0 migration.

## Example

// SM6.6 shader — bindless access\nTexture2D tex = ResourceDescriptorHeap[materialData.albedoIndex];\nfloat4 color = tex.Sample(SamplerDescriptorHeap[samplerIndex], uv);\n\n// CPU side — resource gets persistent index at creation\nuint32_t index = descriptorHeap.Allocate();\ndevice->CreateSRV(resource, &srvDesc, descriptorHeap.GetCPUHandle(index));\nmaterialData.albedoIndex = index; // stored in material CB

## When to Use

When migrating from the current root signature model to bindless. When implementing new material/texture features that need more descriptor slots. When designing SM6.6 shader code.

## When NOT to Use

On SM5.0 fallback path (no ResourceDescriptorHeap support). For compute-only passes that use separate root signature with sufficient space.
