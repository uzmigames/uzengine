# Your First Window

This guide walks through how the UzEngine runtime creates a DX12-backed window
with an animated render loop. By the end, you'll understand the initialization
sequence and how all the pieces fit together.

> **Prerequisites**: [Building from Source](building.md) — ensure `zig build run` works.

## The Runtime Entry Point

The standalone runtime (`tools/uzengine_runtime.cpp`) follows this sequence:

```
1. Initialize logging
2. Load config (project file + CLI overrides)
3. Resolve RHI settings (backend, shader model, validation)
4. Create window (unless headless)
5. Create RHI device
6. Create swapchain, command list, fences
7. Enter render loop
8. Cleanup in reverse order
```

## Configuration Cascade

RHI settings are resolved from multiple sources, highest priority first:

```
CLI flags  >  .uzproject file  >  platform auto-detect
```

| Flag | Example | Description |
|------|---------|-------------|
| `--rhi` | `--rhi=dx12` | Rendering backend |
| `--shader-model` | `--shader-model=sm6.6` | Target shader model |
| `--gpu-validation` | (boolean) | Enable GPU-based validation layer |
| `--gpu-breadcrumbs` | (boolean) | Enable DRED breadcrumbs for crash analysis |
| `--adapter` | `--adapter=1` | GPU adapter index |
| `--headless` | (boolean) | Device-only mode, no window |
| `--width` | `--width=1920` | Window width (default: 1280) |
| `--height` | `--height=1080` | Window height (default: 720) |
| `--frames` | `--frames=100` | Frame limit (-1 = infinite) |
| `--scene` | `--scene=level.uzscene` | Scene file to load (not yet implemented) |

## Device Creation

The runtime resolves an `RHIConfig` and converts it to an `RHIDeviceDesc`:

```cpp
const RHIConfig rhiCfg = ResolveRHIConfig(config);
const RHIDeviceDesc deviceDesc = ToDeviceDesc(rhiCfg);
auto* device = CreateRHIDevice(deviceDesc);
```

On DX12, `CreateRHIDevice` initializes the full stack:

1. **Debug layer** — D3D12 validation + optional GPU validation
2. **DXGI factory** — Adapter enumeration
3. **Adapter selection** — GPU device (respects `--adapter`)
4. **D3D12 Device** — Feature level auto-detect (12_2 → 12_0)
5. **Shader model negotiation** — Probes SM6.7 → SM6.6 → SM5.0, adapts root signature
6. **D3D12MA allocator** — GPU memory management
7. **Descriptor heaps** — 1M CBV/SRV/UAV (SM6.6+ bindless), 2048 samplers, 256 RTV, 64 DSV
8. **Command queues** — Direct (graphics) + Copy

> **Agility SDK**: The runtime exports `D3D12SDKVersion` and `D3D12SDKPath` to activate
> the bundled Agility SDK, enabling Feature Level 12.2 and SM6.7 on supported hardware.

## Swapchain and Frame Pacing

Triple-buffered with fence synchronization:

```
Frame N:    Record → Execute → Present → Signal fence
Frame N+1:  Record → Execute → Present → Signal fence
Frame N+2:  Record → Execute → Present → Signal fence
Frame N+3:  Wait fence[0] → Record → Execute → Present → Signal fence
```

Each frame waits for its fence (N-2) before reusing the command allocator.
This keeps 3 frames in flight for maximum GPU utilization.

## The Render Loop

Currently renders an animated sine-wave RGB clear color:

```cpp
while (window.PumpMessages() && window.IsOpen()) {
    // Wait for frame N-2
    frameFences[frameIndex % 3]->WaitCPU(fenceValues[frameIndex % 3]);

    // Handle resize
    if (window.WasResized()) {
        device->WaitIdle();
        swapChain->Resize(window.GetWidth(), window.GetHeight());
    }

    // Record and submit
    cmdList->Begin();
    cmdList->TransitionBarrier(backBuffer, PRESENT, RENDER_TARGET);
    cmdList->ClearRTV(rtv, clearColor);
    cmdList->TransitionBarrier(backBuffer, RENDER_TARGET, PRESENT);
    cmdList->End();
    cmdList->Execute();

    // Present and signal
    swapChain->Present(false);
    queue->Signal(fence, ++globalFenceValue);
}
```

## Headless Mode

For CI/testing, pass `--headless` to skip window creation:

```bash
zig build run -- --headless
```

This creates the device, logs diagnostics, and exits. Useful for validating
GPU initialization on build machines.

## What's Next

- [RHI Overview](../guides/rhi/overview.md) — Deep dive into the hardware abstraction
- [Architecture](../guides/architecture.md) — Full system design
- [API Reference: IRHIDevice](../reference/rhi/index.md) — Complete interface reference
