<!-- doc-meta -->
title: CVars and Runtime Controls
date: 2026-04-07
status: approved
<!-- end-doc-meta -->

# 5. CVars and Controls

> Implementation note: the shipped tracer uses DXR-inline pixel shaders, not this compute/analytic design; [08 reconciliation](08-implementation-reconciliation.md) is authoritative.

Registered in `path_tracer_pass.cpp` as static locals, following the existing
pattern used by `rt_manager.cpp:14-40` and `gtao_pass.cpp`.

| CVar | Type | Default | Range | Purpose |
|------|------|---------|-------|---------|
| `r.PathTracer.Enable` | bool | 0 | 0/1 | Master toggle. When on, bypasses deferred lighting and writes path-traced result directly to `hdrSceneColor`. |
| `r.PathTracer.MaxBounces` | int | 4 | 1-16 | Bounce limit. Reference shadertoy uses 8. |
| `r.PathTracer.SamplesPerFrame` | int | 1 | 1-16 | Samples integrated per frame before accumulation. Raise for faster convergence at lower FPS. |
| `r.PathTracer.RussianRoulette` | bool | 1 | 0/1 | Enable throughput-based early termination. |
| `r.PathTracer.RussianRouletteMinBounce` | int | 2 | 0-8 | Don't apply RR before this bounce to avoid dark corners. |
| `r.PathTracer.EnvIntensity` | float | 1.0 | 0-10 | Environment/sky multiplier (matches reference `c_skyboxBrightnessMultiplier`). |
| `r.PathTracer.ResetAccum` | exec | — | — | Manual accumulation reset. |
| `r.PathTracer.Accumulate` | bool | 1 | 0/1 | When 0, every frame is single-sample (preview mode). |
| `r.PathTracer.ClampFirefly` | float | 10.0 | 0-1e6 | Max luminance clamp per sample to kill fireflies. 0 = off. |
| `r.PathTracer.JitterAA` | bool | 1 | 0/1 | Subpixel jitter for free AA. |

## Registration snippet (pattern only — do not copy-paste)

```cpp
static auto* cv_PtEnable      = UZ_CVAR_BOOL ("r.PathTracer.Enable", false,
                                              "Enable compute path tracer");
static auto* cv_PtMaxBounces  = UZ_CVAR_INT  ("r.PathTracer.MaxBounces", 4,
                                              "Max ray bounces [1..16]");
static auto* cv_PtSppFrame    = UZ_CVAR_INT  ("r.PathTracer.SamplesPerFrame", 1, "");
static auto* cv_PtRR          = UZ_CVAR_BOOL ("r.PathTracer.RussianRoulette", true, "");
static auto* cv_PtEnvInt      = UZ_CVAR_FLOAT("r.PathTracer.EnvIntensity", 1.0f, "");
static auto* cv_PtResetAccum  = UZ_CVAR_EXEC ("r.PathTracer.ResetAccum",
                                              []{ s_pass->ResetAccumulation(); },
                                              "Reset path tracer history");
```

All accessed via `cv_*->value` each frame — no hot-reload plumbing needed.

## Interaction with existing CVars

- `r.TAA` — MUST be disabled when path tracer is on (path tracer does its own
  accumulation; TAA would blend on top of already-accumulated history and
  cause drifting ghosts).
- `r.Tonemap.*` — untouched. Path tracer writes HDR linear to `hdrSceneColor`
  and the existing tonemap stack consumes it normally.
- `r.Exposure` — untouched. Auto-exposure should work on path-traced HDR.
- `r.Bloom.*` — untouched.
- `r.RayTracing.*` — independent. These control the (unrelated) DXR path and
  should be forced off when path tracer is on to save TLAS build cost.

The enable/disable side-effects are handled in `PathTracerPass::OnEnableChanged`
which sets the dependent CVars via the CVar system's programmatic API.

## Lua API (per `.claude/rules/lua-parity.md`)

Every C++ feature ships a Lua binding. Minimum surface:

```lua
uz.pathtracer.enable(true)
uz.pathtracer.set_max_bounces(8)
uz.pathtracer.reset_accum()
uz.pathtracer.is_enabled()  -- returns bool
uz.pathtracer.samples_accumulated()  -- returns int
```

Binding lives in `runtime/engine/scripting/api/lua_pathtracer.cpp`, registered
from `LuaVM::Init()` alongside the other `uz.*` tables.

---

Next: See 06-testing-plan.md
