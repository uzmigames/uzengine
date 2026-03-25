/// UzEngine Path Resolution
///
/// Provides exe-relative path resolution for engine resources.
/// All engine resources (DLLs, shaders, configs) are discovered relative to
/// the running executable — NEVER relative to CWD, NEVER via absolute paths.
///
/// # Design (follows standard engine pattern)
/// - Equivalent: FPlatformProcess::BaseDir() → exe directory
/// - Godot: OS::get_executable_path().get_base_dir() → exe directory
/// - UzEngine: uz::GetExeDir() → exe directory
///
/// # Layout Convention
/// ```
/// build/bin/
///     uzengine.exe             # Engine executable
///     test_runtime.exe         # Test executable
///     dxcompiler.dll           # DXC (same directory as exe)
///     dxil.dll                 # DXC validator
///     D3D12/                   # Agility SDK subdirectory
///         D3D12Core.dll
///         d3d12SDKLayers.dll
///     shaders/                 # Future: built-in shaders
/// ```

#pragma once

#include <string>
#include <filesystem>

namespace uz {

/// Overrides the engine resource directory used by GetExeDir().
///
/// When the engine runs as a .node addon inside Electron, GetModuleFileNameW
/// returns the Electron host executable directory (e.g., `F:/IDEs/VS Code/`),
/// not the addon directory. Call this function with the addon's own directory
/// before uz_init() or uz_create_viewport() to redirect all resource discovery
/// (dxcompiler.dll, dxil.dll, D3D12/, shaders/) to the correct location.
///
/// Passing an empty path clears the override and reverts to exe-relative
/// discovery.
///
/// # Arguments
/// * `dir` — Absolute path to the directory containing engine DLLs and shaders.
///
/// # Thread Safety
/// Must be called from the main thread before any resource resolution occurs.
void SetEngineDir(const std::filesystem::path& dir);

/// Returns the directory containing the running executable.
///
/// If SetEngineDir() was called with a non-empty path, that path is returned
/// instead of the real exe directory. Otherwise, uses the platform-native API
/// (GetModuleFileNameW on Windows, /proc/self/exe on Linux,
/// _NSGetExecutablePath on macOS). Result is cached after first call when no
/// override is active.
///
/// Example: if exe is at `C:/project/build/bin/test_runtime.exe`,
/// returns `C:/project/build/bin`.
std::filesystem::path GetExeDir();

/// Resolves a resource path relative to the executable directory.
///
/// If the input is already absolute, returns it unchanged.
/// If relative, prepends GetExeDir().
///
/// Example:
///   ResolveEngineResource("dxcompiler.dll")
///     → "C:/project/build/bin/dxcompiler.dll"
///   ResolveEngineResource("D3D12/D3D12Core.dll")
///     → "C:/project/build/bin/D3D12/D3D12Core.dll"
///   ResolveEngineResource("C:/absolute/path.dll")
///     → "C:/absolute/path.dll" (unchanged)
std::filesystem::path ResolveEngineResource(const std::string& relativePath);

} // namespace uz
