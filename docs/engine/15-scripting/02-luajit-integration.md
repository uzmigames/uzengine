# Scripting — LuaJIT Integration

> **Status**: Active (Phase 1 Complete)
> **Source**: `engine/scripting/lua_vm.h/cpp`, `script_component.h/cpp`, `lua_*_api.cpp`, `lua_bindings.cpp`
> **Depends on**: Core (logging, memory, paths), subsystem registry (§9.4), ECS system (§10)
> **References**: LuaJIT 2.1 API, Lua 5.1 C interface, Godot GDScript embedding pattern

## Purpose

The Lua integration layer embeds LuaJIT 2.1 as a scripting VM. Each `World` has its own Lua state, enabling rapid game logic prototyping and modding. The integration handles VM creation, C function registration, FFI bridge setup, and memory management.

## LuaJIT Architecture

```
UzEngine Game Logic (C++)
        │
        ├─ ISubsystem interface
        ├─ Behaviors (C++)
        └─ Physics, Audio, Input (C++ APIs)
        │
        ▼
┌─────────────────────────────────────┐
│ LuaVM (per World)                   │
├─────────────────────────────────────┤
│ Lua State (lua_State*)              │
│  ├─ Global functions               │
│  ├─ Registered C functions          │
│  ├─ Metamethods (Vec3, Entity, etc) │
│  └─ Script instances (entities)     │
│                                     │
│ FFI Bridge (cdata pointers)         │
│  ├─ Directly access C++ objects     │
│  ├─ Call C++ functions from Lua     │
│  └─ Zero-copy data exchange         │
│                                     │
│ Memory Management                   │
│  ├─ Lua heap limit (default 64MB)   │
│  ├─ GC pauses (incremental)         │
│  └─ Allocation tracking             │
└─────────────────────────────────────┘
        │
        ▼
LuaJIT 2.1 (JIT compiler)
        │
        ▼
Native x64 Code (fast execution)
```

## LuaVM Creation

Each `World` creates a Lua VM on initialization:

```cpp
class World {
private:
    class LuaVM* lua_vm = nullptr;

public:
    bool Create() {
        lua_vm = LuaVM::Create({
            .heap_limit_mb = 64,
            .enable_jit = true,
            .gc_mode = GCMode::Incremental,  // Don't stall frames
        });

        if (!lua_vm) {
            UZ_LOG(Scripting, Error, "Failed to create Lua VM");
            return false;
        }

        UZ_LOG(Scripting, Info, "Lua VM created, heap_limit=64MB");
        return true;
    }
};
```

## LuaVM Interface

```cpp
class LuaVM {
public:
    /// Create a new Lua state
    static LuaVM* Create(const LuaVMConfig& config);

    /// Get underlying Lua state (for native C API calls)
    lua_State* GetState() const;

    /// Load and execute a Lua script file
    bool ExecuteScript(std::string_view path);

    /// Call a Lua function by name
    bool CallFunction(std::string_view function_name);

    /// Register a C function for Lua to call
    void RegisterFunction(std::string_view name, lua_CFunction fn);

    /// Register a C++ class as Lua metatable
    template <typename T>
    void RegisterClass(std::string_view name) {
        // Creates Lua metatable with methods
        // Allows Lua code: local vec = uz.Vec3.new(1, 2, 3)
    }

    /// Get last error message
    std::string GetLastError() const;

    /// Garbage collection
    void RunGarbageCollection();
    void SetHeapLimit(size_t limit_bytes);
    size_t GetHeapUsage() const;

    /// Shutdown and cleanup
    void Shutdown();

    ~LuaVM();

private:
    lua_State* L = nullptr;
    size_t heap_limit = 64 * 1024 * 1024;
    bool enable_jit = true;
};
```

## Lua State Creation

```cpp
LuaVM* LuaVM::Create(const LuaVMConfig& config) {
    LuaVM* vm = new LuaVM();

    // 1. Create Lua state
    vm->L = luaL_newstate();
    if (!vm->L) {
        UZ_LOG(Scripting, Error, "luaL_newstate failed");
        delete vm;
        return nullptr;
    }

    // 2. Open standard libraries (safe subset)
    // Note: os, io, loadfile, dofile stripped in sandbox phase
    luaL_openlibs(vm->L);

    // 3. Configure memory allocator
    // Use engine allocator for Lua heap
    lua_setallocf(vm->L, LuaAllocator, vm);
    vm->SetHeapLimit(config.heap_limit_mb * 1024 * 1024);

    // 4. Register engine bindings
    RegisterEngineBindings(vm);

    // 5. Set up FFI bridge for C++ interop
    SetupFFIBridge(vm);

    // 6. Enable JIT if requested
    if (config.enable_jit) {
        luaJIT_setmode(vm->L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
        UZ_LOG(Scripting, Info, "LuaJIT enabled");
    }

    UZ_LOG(Scripting, Info, "Lua VM created: heap={} MB",
           config.heap_limit_mb);

    return vm;
}
```

## Custom Memory Allocator

```cpp
static void* LuaAllocator(void* ud, void* ptr, size_t osize, size_t nsize) {
    LuaVM* vm = static_cast<LuaVM*>(ud);

    // Block allocations that would exceed heap limit
    if (nsize > 0 && (vm->current_usage + nsize) > vm->heap_limit) {
        UZ_LOG(Scripting, Warning, "Lua heap limit exceeded: {} > {}",
               vm->current_usage + nsize, vm->heap_limit);
        return nullptr;  // Allocation denied, Lua will error
    }

    if (nsize == 0) {
        // Deallocation
        if (ptr) {
            free(ptr);  // or engine allocator
            vm->current_usage -= osize;
        }
        return nullptr;
    } else if (ptr == nullptr) {
        // Allocation
        void* new_ptr = malloc(nsize);  // or engine allocator
        if (new_ptr) {
            vm->current_usage += nsize;
        }
        return new_ptr;
    } else {
        // Reallocation
        void* new_ptr = realloc(ptr, nsize);  // or engine allocator
        if (new_ptr) {
            vm->current_usage = vm->current_usage - osize + nsize;
        }
        return new_ptr;
    }
}
```

## FFI Bridge Setup

FFI (Foreign Function Interface) allows Lua to directly call C++ functions and access C++ objects:

```cpp
void SetupFFIBridge(LuaVM* vm) {
    lua_State* L = vm->GetState();

    // Register metamethods for Vec3
    luaL_newmetatable(L, "Vec3");
    lua_pushfunction(L, Vec3_Add);
    lua_setfield(L, -2, "__add");
    lua_pushfunction(L, Vec3_Mul);
    lua_setfield(L, -2, "__mul");
    lua_pushfunction(L, Vec3_Index);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);  // Remove metatable from stack

    // Register constructor function
    lua_pushfunction(L, Vec3_New);
    lua_setglobal(L, "Vec3");
}

// Lua: vec = Vec3.new(1, 2, 3)
static int Vec3_New(lua_State* L) {
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float z = luaL_checknumber(L, 3);

    Vec3* pv = (Vec3*)lua_newuserdata(L, sizeof(Vec3));
    *pv = {x, y, z};

    luaL_setmetatable(L, "Vec3");
    return 1;  // Return userdata
}

// Lua: result = vec1 + vec2
static int Vec3_Add(lua_State* L) {
    Vec3* v1 = (Vec3*)luaL_checkudata(L, 1, "Vec3");
    Vec3* v2 = (Vec3*)luaL_checkudata(L, 2, "Vec3");

    Vec3 result = *v1 + *v2;

    Vec3* pr = (Vec3*)lua_newuserdata(L, sizeof(Vec3));
    *pr = result;
    luaL_setmetatable(L, "Vec3");
    return 1;
}
```

## Script Execution

```cpp
bool LuaVM::ExecuteScript(std::string_view path) {
    lua_State* L = this->L;

    // Resolve path relative to VFS
    std::string resolved_path = PathSystem::ResolvePath(path);

    // Load and execute
    if (luaL_dofile(L, resolved_path.c_str()) != 0) {
        const char* error = lua_tostring(L, -1);
        UZ_LOG(Scripting, Error, "Lua script error: {}", error);
        lua_pop(L, 1);  // Pop error
        return false;
    }

    UZ_LOG(Scripting, Debug, "Lua script executed: {}", path);
    return true;
}

bool LuaVM::CallFunction(std::string_view function_name) {
    lua_State* L = this->L;

    // Get function from global table
    lua_getglobal(L, function_name.data());

    if (!lua_isfunction(L, -1)) {
        UZ_LOG(Scripting, Warning, "Lua function not found: {}", function_name);
        lua_pop(L, 1);
        return false;
    }

    // Call with no arguments, no return value
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* error = lua_tostring(L, -1);
        UZ_LOG(Scripting, Error, "Lua function error: {}", error);
        lua_pop(L, 1);
        return false;
    }

    return true;
}
```

## Safe Execution (Error Handling)

```cpp
/// Execute Lua code with exception-free error handling
bool SafeLuaCall(lua_State* L, int nargs, int nresults,
                 std::string& out_error) {
    int status = lua_pcall(L, nargs, nresults, 0);

    if (status != LUA_OK) {
        if (lua_isstring(L, -1)) {
            out_error = lua_tostring(L, -1);
        } else {
            out_error = "(unknown error)";
        }
        lua_pop(L, 1);

        UZ_LOG(Scripting, Error, "Lua error: {}", out_error);
        return false;
    }

    return true;
}
```

## Memory Management Best Practices

```cpp
// Good: Incremental GC, no stalls
vm->SetGCMode(GCMode::Incremental);

// Check heap usage during development
if (vm->GetHeapUsage() > 48 * 1024 * 1024) {
    UZ_LOG(Scripting, Warning,
           "Lua heap usage high: {} MB", vm->GetHeapUsage() / 1024 / 1024);
}

// Manually trigger GC after heavy allocations
vm->RunGarbageCollection();
```

## Lua Module Search Path

Configure where Lua looks for `require()` modules:

```cpp
void ConfigureLuaSearchPath(LuaVM* vm) {
    lua_State* L = vm->GetState();

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    std::string cur_path = lua_tostring(L, -1);

    // Add engine script directory
    std::string engine_scripts = PathSystem::ResolvePath("scripts/?. lua");
    std::string new_path = cur_path + ";" + engine_scripts;

    lua_pop(L, 1);
    lua_pushstring(L, new_path.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
}
```

## Engine Comparison

| Aspect | UE5 | Godot 4 | Unity | **UzEngine** |
|--------|-----|---------|-------|------------|
| Scripting VM | None (Blueprints or C++) | GDScript (Python-like) | C# + Roslyn | **LuaJIT 2.1** |
| Embedding | N/A | GDExtension | IL2CPP | **Lua C API + FFI** |
| JIT compilation | N/A | N/A | IL2CPP AOT | **LuaJIT on-demand** |
| Memory limit | N/A | Configurable | GC heap | **Configurable (default 64MB)** |
| Performance | N/A | Interpreted | JIT compiled | **JIT compiled (fast)** |

## Rules

1. **ALWAYS use `lua_pcall` for Lua calls** — never `lua_call` (unhandled exceptions crash engine)
2. **NEVER directly access Lua stack without proper type checking** — use `luaL_check*` macros
3. **ALWAYS set heap limit before loading large scripts** — prevent runaway allocations
4. **NEVER register unbounded iteration in Lua** — can cause stalls; add safety limits
5. **ALWAYS check return values from Lua C API calls** — silent failures are hard to debug
