<!-- doc-meta
title: VFX Demo Authoring Guide
category: guides
theme: vfx
created: 2026-03-26
-->

# VFX Demo Authoring Guide

How to create a new VFX demo that follows the engine's data-driven pattern.

## Quick Start

1. Create demo directory: `runtime/demos/<name>/`
2. Create `assets/<name>.uzfx` with emitter configuration
3. Create `main.cpp` that loads the `.uzfx` and runs simulation
4. Create `build.zig` using `build_common.zig`

## Demo Structure

```
runtime/demos/my_effect/
├── assets/
│   └── my_effect.uzfx      # Emitter configuration
├── build.zig                # Build script (uses common)
├── build.zig.zon            # Package manifest
└── main.cpp                 # Demo entry point
```

## Step 1: Create .uzfx Asset

```json
{
  "name": "My Effect",
  "version": 1,
  "emitter": {
    "max_particles": 10000,
    "emission_rate": 200.0,
    "lifetime": 2.0,
    "looping": true
  },
  "modules": {
    "spawn": {
      "type": "spawn",
      "velocity": [0.0, 0.0, 200.0],
      "spread_angle": 30.0
    },
    "update": [
      {"type": "update", "gravity": [0.0, 0.0, -981.0], "drag": 0.98},
      {"type": "decay"}
    ],
    "render": {
      "type": "sprite",
      "width": 0.5,
      "height": 0.5
    }
  }
}
```

See [.uzfx format spec](../specs/uzfx-format.md) for all module types and parameters.

## Step 2: Create main.cpp

```cpp
#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/asset/uzfx_loader.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    auto json = ReadFile("assets/my_effect.uzfx");
    if (json.empty()) {
        std::fprintf(stderr, "Cannot read assets/my_effect.uzfx\n");
        return 1;
    }

    auto desc = uz::UzfxLoader::Load(json);

    uz::DemoMockDevice device;
    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, 500000);

    uz::ParticleEmitter emitter;
    if (!uz::UzfxLoader::Instantiate(desc, emitter, &device, vfxSystem.GetBudget())) {
        std::fprintf(stderr, "Failed to instantiate emitter\n");
        vfxSystem.Shutdown(&device);
        return 1;
    }
    vfxSystem.RegisterEmitter(&emitter);

    // Simulate
    constexpr float kDt = 1.0f / 60.0f;
    constexpr int kFrames = 180;
    for (int f = 0; f < kFrames; ++f) {
        vfxSystem.Tick(kDt);
    }

    std::printf("[OK] %s: %u particles, %d frames\n",
                desc.name.c_str(), emitter.allocatedFromBudget, kFrames);

    // Cleanup
    vfxSystem.UnregisterEmitter(&emitter);
    emitter.Shutdown(&device, vfxSystem.GetBudget());
    vfxSystem.Shutdown(&device);
    return 0;
}
```

## Step 3: Create build.zig

```zig
const std = @import("std");
const common = @import("../common/build_common.zig");

pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "my_effect",
        .sources = &.{"main.cpp"},
        .use_demo_application = false,
    });
    demo.finalize();
}
```

## Step 4: Build and Run

```bash
cd runtime/demos/my_effect
zig build
./zig-out/bin/my_effect
```

## Module Types Reference

| Type | Purpose | Key Params |
|------|---------|------------|
| `spawn` | Initial particle emission | velocity, spread_angle, lifespan |
| `update` | Physics (gravity, drag) | gravity, drag |
| `turbulence` | Noise-based displacement | strength, frequency, octaves |
| `decay` | Alpha fade over lifetime | (none) |
| `attractor` | Point force | position, strength, radius |
| `orbit` | Orbital motion | center, speed, radius, axis |
| `collision` | Depth-buffer bounce | bounce_factor |
| `vector_field` | 3D flow field | intensity, tightness, bounds |
| `sprite` | Billboard render | width, height |
| `mesh` | Instanced mesh render | mesh_path, scale |
| `light` | Per-particle lights | intensity, radius |
