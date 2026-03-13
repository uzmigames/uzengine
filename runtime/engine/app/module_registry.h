#pragma once

/// UzEngine Module Registry
///
/// Provides static self-registration of engine modules via the `UZ_REGISTER_MODULE` macro.
/// Each module registers a callback that receives an `EngineLoop&` during initialization,
/// allowing it to register subsystems before the lifecycle begins.
///
/// # Design
/// Uses the static-initializer pattern: each translation unit with `UZ_REGISTER_MODULE`
/// adds its callback to a global list before `main()` runs. The engine loop then invokes
/// all registered callbacks during `Initialize()`.
///
/// # Thread Safety
/// Registration happens during static initialization (single-threaded by the C++ standard).
/// `RunModuleRegistrars` is called from the main thread during `EngineLoop::Initialize()`.

namespace uz {

class EngineLoop;

namespace detail {

/// Function pointer type for module registration callbacks.
///
/// Each callback receives a mutable reference to the engine loop and is expected
/// to call `loop.RegisterSubsystem(...)` to add its subsystems.
using ModuleRegistrarFn = void(*)(EngineLoop&);

/// Adds a module registrar function to the global list.
///
/// Called automatically by the `UZ_REGISTER_MODULE` macro during static initialization.
/// Must not be called after `RunModuleRegistrars` has executed.
///
/// # Arguments
/// * `fn` - Non-null function pointer to the registration callback.
void AddModuleRegistrar(ModuleRegistrarFn fn);

/// Invokes all registered module registrar callbacks.
///
/// Called once by `EngineLoop::Initialize()`. Each callback registers its subsystems
/// with the engine loop. After this call, no further registrars should be added.
///
/// # Arguments
/// * `loop` - The engine loop instance to register subsystems with.
void RunModuleRegistrars(EngineLoop& loop);

} // namespace detail
} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// UZ_REGISTER_MODULE — static self-registration macro
// ─────────────────────────────────────────────────────────────────────────────

/// Registers a module's subsystems with the engine loop via static initialization.
///
/// Place this macro at file scope in a `.cpp` file. The provided function will be
/// called during `EngineLoop::Initialize()` with a reference to the loop, allowing
/// the module to register its subsystems.
///
/// # Arguments
/// * `Name` - A unique identifier for this module (used to generate internal type names).
/// * `Func` - A function with signature `void(uz::EngineLoop&)`.
///
/// # Example
/// ```cpp
/// static void RegisterAudioModule(uz::EngineLoop& loop) {
///     loop.RegisterSubsystem(std::make_unique<AudioSubsystem>());
/// }
/// UZ_REGISTER_MODULE(Audio, RegisterAudioModule);
/// ```
#define UZ_REGISTER_MODULE(Name, Func)                                \
    namespace {                                                        \
    struct UzModule_##Name {                                           \
        UzModule_##Name() { ::uz::detail::AddModuleRegistrar(Func); } \
    };                                                                 \
    static UzModule_##Name s_uzModule_##Name;                          \
    }
