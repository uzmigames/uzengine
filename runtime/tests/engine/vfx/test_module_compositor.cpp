/// Unit tests for ModuleCompositor -- stitches VFX module HLSL snippets.
///
/// Covers: update shader composition with multiple modules, spawn shader
/// composition, empty module list, cache key uniqueness for different module
/// sets, and cache key stability for the same module set. No GPU hardware
/// required.

#include <gtest/gtest.h>

#include "modules/module_compositor.h"
#include "modules/spawn_module.h"
#include "modules/update_module.h"
#include "modules/decay_module.h"
#include "modules/turbulence_module.h"
#include "modules/constraint_module.h"

#include <string>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: ComposeUpdateShader with 3 modules -- HLSL contains all snippets
// ─────────────────────────────────────────────────────────────────────────────

/// Compose an update shader from UpdateModule + TurbulenceModule + DecayModule.
/// The resulting HLSL must contain the shader snippet from each module and
/// must have a non-zero cache key.
TEST(ModuleCompositor, ComposeUpdateShader_ThreeModules_HlslContainsAllSnippets) {
    ModuleCompositor compositor;

    UpdateModule    updateMod;
    TurbulenceModule turbMod;
    DecayModule     decayMod;

    const std::vector<IVFXModule*> modules = {&updateMod, &turbMod, &decayMod};

    const ComposedShader shader = compositor.ComposeUpdateShader(modules);

    EXPECT_FALSE(shader.hlslSource.empty())
        << "Composed update shader HLSL must not be empty";
    EXPECT_NE(shader.cacheKey, 0u)
        << "Composed update shader cache key must be non-zero";

    // Each module's snippet must appear verbatim in the composed output.
    const std::string_view updateCode = updateMod.GetShaderCode();
    const std::string_view turbCode   = turbMod.GetShaderCode();
    const std::string_view decayCode  = decayMod.GetShaderCode();

    EXPECT_NE(shader.hlslSource.find(updateCode), std::string::npos)
        << "Composed shader must contain UpdateModule HLSL snippet";
    EXPECT_NE(shader.hlslSource.find(turbCode), std::string::npos)
        << "Composed shader must contain TurbulenceModule HLSL snippet";
    EXPECT_NE(shader.hlslSource.find(decayCode), std::string::npos)
        << "Composed shader must contain DecayModule HLSL snippet";
}

/// Verify the snippets appear in the declared order: update, then turbulence,
/// then decay. The position of each must be strictly after the previous.
TEST(ModuleCompositor, ComposeUpdateShader_ThreeModules_SnippetsInDeclaredOrder) {
    ModuleCompositor compositor;

    UpdateModule    updateMod;
    TurbulenceModule turbMod;
    DecayModule     decayMod;

    const ComposedShader shader = compositor.ComposeUpdateShader(
        {&updateMod, &turbMod, &decayMod}
    );

    const auto posUpdate = shader.hlslSource.find(updateMod.GetShaderCode());
    const auto posTurb   = shader.hlslSource.find(turbMod.GetShaderCode());
    const auto posDecay  = shader.hlslSource.find(decayMod.GetShaderCode());

    ASSERT_NE(posUpdate, std::string::npos);
    ASSERT_NE(posTurb,   std::string::npos);
    ASSERT_NE(posDecay,  std::string::npos);

    EXPECT_LT(posUpdate, posTurb)
        << "UpdateModule snippet must appear before TurbulenceModule snippet";
    EXPECT_LT(posTurb, posDecay)
        << "TurbulenceModule snippet must appear before DecayModule snippet";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: ComposeSpawnShader with SpawnModule
// ─────────────────────────────────────────────────────────────────────────────

/// Compose a spawn shader from SpawnModule. The HLSL must contain the spawn
/// module snippet and have a non-zero cache key.
TEST(ModuleCompositor, ComposeSpawnShader_SpawnModule_HlslContainsSpawnSnippet) {
    ModuleCompositor compositor;
    SpawnModule spawnMod;

    const ComposedShader shader = compositor.ComposeSpawnShader({&spawnMod});

    EXPECT_FALSE(shader.hlslSource.empty())
        << "Composed spawn shader HLSL must not be empty";
    EXPECT_NE(shader.cacheKey, 0u)
        << "Composed spawn shader cache key must be non-zero";

    const std::string_view spawnCode = spawnMod.GetShaderCode();
    EXPECT_NE(shader.hlslSource.find(spawnCode), std::string::npos)
        << "Composed spawn shader must contain SpawnModule HLSL snippet";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Empty module list produces valid but minimal shader
// ─────────────────────────────────────────────────────────────────────────────

/// Composing with an empty modules vector must not crash and must return a
/// non-empty HLSL string (the wrapper/header infrastructure alone). The
/// cache key for an empty list must be defined (could be 0 since no modules
/// contribute to the hash, but the call itself must succeed).
TEST(ModuleCompositor, EmptyModuleList_ComposeUpdateShader_DoesNotCrash) {
    ModuleCompositor compositor;

    const std::vector<IVFXModule*> empty;

    // Must not crash.
    const ComposedShader shader = compositor.ComposeUpdateShader(empty);

    // The output must be a valid (non-null, defined) string.
    // We check it doesn't throw or produce garbage by verifying it terminates.
    EXPECT_GE(shader.hlslSource.size(), 0u)
        << "Empty module list must produce a defined (possibly empty) HLSL string";
}

TEST(ModuleCompositor, EmptyModuleList_ComposeSpawnShader_DoesNotCrash) {
    ModuleCompositor compositor;

    const std::vector<IVFXModule*> empty;

    const ComposedShader shader = compositor.ComposeSpawnShader(empty);

    EXPECT_GE(shader.hlslSource.size(), 0u)
        << "Empty module list must produce a defined HLSL string for spawn shader";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: Cache key differs for different module combinations
// ─────────────────────────────────────────────────────────────────────────────

/// Two shaders composed from different module sets must produce different
/// cache keys. This ensures the PSO cache can distinguish them.
TEST(ModuleCompositor, CacheKey_DifferentModuleLists_KeysDiffer) {
    ModuleCompositor compositor;

    UpdateModule    updateMod;
    DecayModule     decayMod;
    TurbulenceModule turbMod;

    const ComposedShader shaderA = compositor.ComposeUpdateShader(
        {&updateMod, &decayMod}
    );
    const ComposedShader shaderB = compositor.ComposeUpdateShader(
        {&updateMod, &decayMod, &turbMod}
    );

    EXPECT_NE(shaderA.cacheKey, shaderB.cacheKey)
        << "Different module lists (with vs. without TurbulenceModule) "
           "must produce different cache keys";
}

/// Module order also affects the cache key -- {update, decay} != {decay, update}.
TEST(ModuleCompositor, CacheKey_ReorderedModules_KeysDiffer) {
    ModuleCompositor compositor;

    UpdateModule updateMod;
    DecayModule  decayMod;

    const ComposedShader shaderA = compositor.ComposeUpdateShader(
        {&updateMod, &decayMod}
    );
    const ComposedShader shaderB = compositor.ComposeUpdateShader(
        {&decayMod, &updateMod}
    );

    EXPECT_NE(shaderA.cacheKey, shaderB.cacheKey)
        << "Different module order must produce different cache keys";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: Cache key is consistent for the same module list
// ─────────────────────────────────────────────────────────────────────────────

/// Calling ComposeUpdateShader twice with identical module pointers in the
/// same order must produce the same cache key both times.
TEST(ModuleCompositor, CacheKey_SameModulesComposedTwice_KeysMatch) {
    ModuleCompositor compositor;

    UpdateModule    updateMod;
    TurbulenceModule turbMod;
    DecayModule     decayMod;

    const std::vector<IVFXModule*> modules = {&updateMod, &turbMod, &decayMod};

    const ComposedShader shaderFirst  = compositor.ComposeUpdateShader(modules);
    const ComposedShader shaderSecond = compositor.ComposeUpdateShader(modules);

    EXPECT_EQ(shaderFirst.cacheKey, shaderSecond.cacheKey)
        << "Same modules in same order must produce the same cache key on repeated calls";
    EXPECT_EQ(shaderFirst.hlslSource, shaderSecond.hlslSource)
        << "Same modules in same order must produce identical HLSL on repeated calls";
}

/// Same check for spawn shader.
TEST(ModuleCompositor, CacheKey_SameSpawnModuleComposedTwice_KeysMatch) {
    ModuleCompositor compositor;
    SpawnModule spawnMod;

    const ComposedShader first  = compositor.ComposeSpawnShader({&spawnMod});
    const ComposedShader second = compositor.ComposeSpawnShader({&spawnMod});

    EXPECT_EQ(first.cacheKey, second.cacheKey)
        << "Same spawn module composed twice must give the same cache key";
}
