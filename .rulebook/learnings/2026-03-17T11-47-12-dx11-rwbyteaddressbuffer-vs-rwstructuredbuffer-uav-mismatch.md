# DX11 RWByteAddressBuffer vs RWStructuredBuffer UAV mismatch
**Source**: manual
**Date**: 2026-03-17
**Related Task**: phase0-0.8.0-uniform-buffer-system
**Tags**: dx11, uav, raw-buffer, structured-buffer, auto-exposure, hlsl
DX11 D3D11 Debug Layer reports "UAV not created with D3D11_BUFFER_UAV_FLAG_RAW flag, but shader expects RAW Buffer" when buffer is created as StructuredBuffer (D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) but HLSL declares RWByteAddressBuffer. Fix: either (a) change HLSL to RWStructuredBuffer<uint>/RWStructuredBuffer<float> to match the C++ buffer, or (b) change C++ to create as RAW buffer (D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS + DXGI_FORMAT_R32_TYPELESS + D3D11_BUFFER_UAV_FLAG_RAW). Option (a) is simpler and more portable. Applied to auto-exposure histogram (RWStructuredBuffer<uint>) and exposure (RWStructuredBuffer<float>) buffers.