---
name: demo
description: Create, build, or run standalone demo applications under demos/. Each demo has its own build.zig. Use when creating new demos, testing rendering features, or running existing demos.
---

# Demo Management

Create and manage standalone demo applications under `runtime/demos/`.

## Usage

- `/demo new <name>` — Create a new demo from template
- `/demo build <name>` — Build a specific demo
- `/demo list` — List all available demos
- `/demo run <name>` — Build and run a demo

## Existing Demos

```
demos/clear_screen/   — Basic DX12 clear + present (6069 FPS benchmark)
demos/triangle/       — Vertex buffer + PSO + draw call
demos/textured_quad/  — Texture loading + sampling
demos/camera_test/    — Camera system + input
demos/mesh_test/      — .uzmesh loading + rendering
```

## New Demo Template

Each demo MUST be standalone with its own `build.zig`:

```bash
mkdir -p runtime/demos/<name>
# Create build.zig, main.cpp
```

## Build Commands

```bash
# Build specific demo
cd runtime/demos/<name> && zig build 2>&1

# Run
cd runtime/demos/<name> && zig-out/bin/<name>.exe
```

## CRITICAL Rules

- **NEVER register demos in `runtime/build.zig`** — each demo is standalone
- Each demo gets its own `build.zig` that links the engine library
- Demos go in `runtime/demos/<name>/`
- Use `common/` for shared demo utilities (window creation, etc.)
