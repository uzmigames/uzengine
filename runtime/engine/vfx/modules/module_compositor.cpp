/// VFX Module Compositor implementation -- HLSL composition and cache keys.

#include "module_compositor.h"

#include "engine/logging/log.h"

#include <cstddef>
#include <string>

namespace uz {

namespace {

// ---------------------------------------------------------------------------
// FNV-1a 64-bit hash -- stable, fast, non-cryptographic
// ---------------------------------------------------------------------------

/// Computes FNV-1a 64-bit hash over a byte range.
///
/// Used to generate cache keys from concatenated module names.
/// Deterministic and order-sensitive -- different module orders
/// produce different hashes.
uint64_t Fnv1a(const char* data, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(data[i]));
        hash *= 1099511628211ull;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Shared HLSL snippets
// ---------------------------------------------------------------------------

/// Common buffer declarations shared by all VFX compute shaders.
constexpr const char* kBufferDeclarations =
    "// ── Standard particle buffers ──\n"
    "RWStructuredBuffer<float3> positions : register(u0);\n"
    "RWStructuredBuffer<float3> velocities : register(u1);\n"
    "RWStructuredBuffer<float>  ages       : register(u2);\n"
    "StructuredBuffer<float>    lifespans  : register(t0);\n"
    "RWStructuredBuffer<float4> colors     : register(u3);\n"
    "RWStructuredBuffer<float>  sizes      : register(u4);\n"
    "\n"
    "cbuffer VFXConstants : register(b0) {\n"
    "    uint  numParticles;\n"
    "    float deltaTime;\n"
    "    uint  frameNumber;\n"
    "    uint  _pad0;\n"
    "};\n\n";

/// Update dispatch wrapper -- opens the CSMain function.
constexpr const char* kUpdateDispatchHeader =
    "[numthreads(256, 1, 1)]\n"
    "void CSMain(uint3 tid : SV_DispatchThreadID) {\n"
    "    uint idx = tid.x;\n"
    "    if (idx >= numParticles) return;\n\n";

/// Spawn dispatch wrapper -- opens the CSMain function.
constexpr const char* kSpawnDispatchHeader =
    "[numthreads(256, 1, 1)]\n"
    "void CSMain(uint3 tid : SV_DispatchThreadID) {\n"
    "    uint idx = tid.x;\n"
    "    if (idx >= numParticles) return;\n\n"
    "    // Initialize defaults before spawn modules.\n"
    "    positions[idx] = float3(0, 0, 0);\n"
    "    velocities[idx] = float3(0, 0, 0);\n"
    "    colors[idx] = float4(1, 1, 1, 1);\n"
    "    sizes[idx] = 1.0;\n\n";

/// Closing brace for CSMain.
constexpr const char* kDispatchFooter = "}\n";

// ---------------------------------------------------------------------------
// Cache key computation
// ---------------------------------------------------------------------------

/// Computes a stable cache key from an ordered list of module names.
///
/// Hashes each module name concatenated with a separator so that
/// ["A", "B"] and ["AB"] produce different keys.
uint64_t ComputeCacheKey(const std::vector<IVFXModule*>& modules,
                         const char* prefix) {
    std::string keyInput(prefix);
    for (const auto* mod : modules) {
        if (!mod) continue;
        keyInput += '|';
        keyInput += mod->GetName();
    }
    return Fnv1a(keyInput.c_str(), keyInput.size());
}

/// Appends module HLSL snippets to the output string.
void AppendModuleSnippets(std::string& out,
                          const std::vector<IVFXModule*>& modules) {
    for (const auto* mod : modules) {
        if (!mod) {
            UZ_LOG(VFX, Warning,
                   "ModuleCompositor: null module pointer, skipping");
            continue;
        }

        const auto code = mod->GetShaderCode();
        out += "    // ── Module: ";
        out += mod->GetName();
        out += " ──\n";
        out += "    {\n";
        out += code;
        out += "\n    }\n\n";
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ModuleCompositor::ComposeUpdateShader
// ---------------------------------------------------------------------------

ComposedShader ModuleCompositor::ComposeUpdateShader(
    const std::vector<IVFXModule*>& modules) const {

    ComposedShader result;

    std::string& src = result.hlslSource;
    src.reserve(2048);

    src += "#include \"vfx_common.hlsli\"\n\n";
    src += kBufferDeclarations;
    src += kUpdateDispatchHeader;

    AppendModuleSnippets(src, modules);

    src += kDispatchFooter;

    result.cacheKey = ComputeCacheKey(modules, "update");
    return result;
}

// ---------------------------------------------------------------------------
// ModuleCompositor::ComposeSpawnShader
// ---------------------------------------------------------------------------

ComposedShader ModuleCompositor::ComposeSpawnShader(
    const std::vector<IVFXModule*>& spawnModules) const {

    ComposedShader result;

    std::string& src = result.hlslSource;
    src.reserve(2048);

    src += "#include \"vfx_common.hlsli\"\n\n";
    src += kBufferDeclarations;
    src += kSpawnDispatchHeader;

    AppendModuleSnippets(src, spawnModules);

    src += kDispatchFooter;

    result.cacheKey = ComputeCacheKey(spawnModules, "spawn");
    return result;
}

} // namespace uz
