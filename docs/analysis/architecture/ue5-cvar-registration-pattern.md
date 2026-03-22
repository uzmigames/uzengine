<!-- doc-meta
title: UE5 CVar Registration Architecture Pattern
description: How UE5 registers console variables independently without a central coordinator
created: 2026-03-21
status: reference
tags: [ue5-architecture, cvars, console-variables, pattern-analysis]
-->

# UE5 CVar Registration Architecture

## Pattern Overview

UE5 CVars are registered **statically and independently** by each subsystem. There is no central `RegisterAll()` function. Instead, the `IConsoleManager` singleton collects CVars through **global object initialization** at module load time.

## Two Registration Mechanisms

### 1. TAutoConsoleVariable<T> — Owned Static Variables

```cpp
// From DirectionalLightComponent.cpp (line 70-74)
static TAutoConsoleVariable<int32> CVarMaxNumFarShadowCascades(
    TEXT("r.Shadow.MaxNumFarShadowCascades"),
    10,                                      // default value
    TEXT("Description..."),
    ECVF_RenderThreadSafe | ECVF_Scalability
);
```

**How it works:**
- Global static object created at module load
- Constructor automatically registers itself with `IConsoleManager::Get()`
- Variable ownership is explicit (static member, cpp file)
- **Used when:** subsystem owns and manages the CVar value

### 2. FAutoConsoleVariableRef — External Global Reference

```cpp
// From DirectionalLightComponent.cpp (line 79-84)
ENGINE_API int32 GFarShadowStaticMeshLODBias = 0;
FAutoConsoleVariableRef CVarFarShadowStaticMeshLODBias(
    TEXT("r.Shadow.FarShadowStaticMeshLODBias"),
    GFarShadowStaticMeshLODBias,             // reference to external var
    TEXT("Description..."),
    ECVF_Scalability | ECVF_RenderThreadSafe
);
```

**How it works:**
- External global variable (GFarShadowStaticMeshLODBias) exists separately
- `FAutoConsoleVariableRef` wraps reference to it
- Changes via console update the external global
- **Used when:** variable already exists and is used throughout codebase

## Key Registration Details

### Declaration Location (Always .cpp)

CVars are **NEVER in .h files**. They're declared as `static` in `.cpp` files:

```cpp
// DirectionalLightComponent.cpp — correct
static TAutoConsoleVariable<float> CVarFarShadowDistanceOverride(...);

// DirectionalLightComponent.h — WRONG
// static TAutoConsoleVariable<float> CVarFarShadowDistanceOverride(...);  // Never here
```

**Why:** Each `.cpp` can have its own static global objects. If declared in `.h`, multiple `.cpp` files including that header would create duplicate globals.

### Global IConsoleManager Singleton

```cpp
// Simplified: IConsoleManager is a global singleton
class IConsoleManager {
    static IConsoleManager* Get();

    void RegisterConsoleObject(const TCHAR* name, IConsoleObject* obj, ...);
    // ...
};
```

Each CVar auto-registers in its destructor at shutdown, and at construction via static object initialization.

### No Central RegisterAll() Function

The magic: **C++ static object initialization order at module load time.**

1. Module loads (`.dll` / `.so`)
2. All global statics are initialized in **top-to-bottom order**
3. Each `TAutoConsoleVariable<T>` constructor calls `IConsoleManager::Get()->Register(...)`
4. All CVars are registered **before** any code runs

```cpp
// Pseudo-code: how TAutoConsoleVariable works
template<typename T>
class TAutoConsoleVariable {
public:
    TAutoConsoleVariable(const TCHAR* name, const T& def, const TCHAR* help, uint32 flags)
        : Value(def), Name(name), Help(help), Flags(flags) {
        // Automatically register with IConsoleManager
        IConsoleManager::Get()->RegisterConsoleObject(Name, this, Help, Flags);
    }

    // Console can read/write Value through virtual interface
    T Value;
    // ...
};
```

## Naming Convention

| Prefix | Subsystem | Example |
|--------|-----------|---------|
| `r.` | Renderer | `r.Shadow.MaxNumFarShadowCascades` |
| `p.` | Physics | `p.DefaultGravityZ` |
| `a.` | Audio | `a.Master.Volume` |
| `n.` | Network | `n.TickRate` |
| `l.` | Lua/Scripting | `l.ScriptDebugEnable` |
| `ed.` | Editor | `ed.ViewportAAType` |
| `s.` | Streaming | `s.StreamingPoolSize` |

Names use **dot notation for hierarchy**, not underscores: `r.Shadow.MaxCascades` (NOT `r_shadow_max_cascades`).

## Flags (ECVF_*)

CVars have flags that control behavior:

```cpp
enum EConsoleVariableFlags : uint32 {
    ECVF_Default           = 0x0,
    ECVF_Scalability       = 0x1,      // Can be driven by scalability settings
    ECVF_RenderThreadSafe  = 0x2,      // Safe to access from render thread
    ECVF_Cheat             = 0x4,      // Cheat-protected (offline/preview only)
    ECVF_ReadOnly          = 0x8,      // Cannot be changed
    // ... more flags
};
```

## UzEngine Adaptation Strategy

### Recommended Pattern for UzEngine

```cpp
// File: src/renderer/shadows/shadow_renderer.cpp (NOT .h)

namespace uz::renderer {

// Pattern 1: Owned static variable
static uz::dev::CVar<int32> CVarMaxFarShadowCascades(
    "r.Shadow.MaxNumFarShadowCascades",
    10,
    "Max number of far shadow cascades"
);

// Pattern 2: External global reference
int32 GFarShadowStaticMeshLODBias = 0;
static uz::dev::CVarRef<int32> CVarFarShadowStaticMeshLODBias(
    "r.Shadow.FarShadowStaticMeshLODBias",
    GFarShadowStaticMeshLODBias,
    "LOD bias for static meshes in far shadows"
);

void RenderShadows(const Scene& scene) {
    int32 max_cascades = CVarMaxFarShadowCascades.Get();
    // ... use max_cascades
}

}  // namespace uz::renderer
```

### CVarManager Implementation

```cpp
// File: src/engine/developer/cvar_manager.h

namespace uz::dev {

class CVarManager {
public:
    static CVarManager& Get();

    // Called by CVar<T> and CVarRef<T> constructors
    void Register(StringView name, IConsoleVariable* var);

    // Console access
    std::optional<std::string> Get(StringView name) const;
    bool Set(StringView name, StringView value);

    std::vector<CVarInfo> GetAll(StringView prefix = "") const;
};

}  // namespace uz::dev
```

### CVar Base Class

```cpp
// File: src/engine/developer/cvar.h

namespace uz::dev {

class IConsoleVariable {
public:
    virtual ~IConsoleVariable() = default;
    virtual std::string GetValue() const = 0;
    virtual bool SetValue(StringView value) = 0;
    StringView name;
    StringView help;
};

template<typename T>
class CVar : public IConsoleVariable {
public:
    CVar(StringView name_arg, const T& default_val, StringView help_arg)
        : value_(default_val) {
        // Static initialization → automatic registration
        name = name_arg;
        help = help_arg;
        CVarManager::Get().Register(name, this);
    }

    T Get() const { return value_; }
    void Set(const T& new_value) { value_ = new_value; }

    // For console access (string-based)
    std::string GetValue() const override;
    bool SetValue(StringView value) override;

private:
    T value_;
};

}  // namespace uz::dev
```

## Key Insights for UzEngine

1. **Declaration Location**: Always `.cpp` files, never `.h`
   - Static at module/file scope
   - Prevents duplicate registration across multiple `.cpp` includes

2. **Registration Trigger**: Static object construction
   - No explicit `RegisterAll()` needed
   - Happens automatically when shared library loads
   - Order: top-to-bottom within file

3. **No Central Coordinator**
   - IConsoleManager is a singleton (passive registry)
   - Each subsystem independently self-registers
   - Decoupled: renderer doesn't know about physics, but both register

4. **Name Hierarchy**
   - Use dotted names: `r.Shadow.MaxCascades`
   - Subsystem.Category.Property convention
   - Enables CLI filtering: `cvarlist r.*` → all renderer CVars

5. **Two Ownership Patterns**
   - `TAutoConsoleVariable<T>`: Owns the value (subsystem state)
   - `FAutoConsoleVariableRef`: References external global (legacy compat)

