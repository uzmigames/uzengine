/// Unit tests for the billboard fallback guard — item 4.2.
///
/// Covers the guard condition in frame_particles.cpp:399-413:
///
///     if (emitter->moduleStack.renderModule &&
///         std::strcmp(emitter->moduleStack.renderModule->GetName(),
///                     "mesh_render") == 0) {
///         continue;  // skip billboard batch for mesh emitters
///     }
///
/// The guard identifies mesh-render emitters by string-comparing GetName()
/// against the literal "mesh_render".  These tests verify:
///
///   1. MeshRenderModule::GetName() returns exactly "mesh_render".
///   2. strcmp("mesh_render", "mesh_render") == 0 (guard fires).
///   3. strcmp("mesh_render", "sprite_render") != 0 (guard does NOT fire).
///   4. A null renderModule pointer (default EmitterModuleStack state) is
///      handled safely — the guard short-circuits on the null check.
///   5. MeshRenderModule default-constructs without crashing.
///   6. The EmitterModuleStack.renderModule field is null by default.
///   7. SetRenderModule correctly installs a MeshRenderModule.
///   8. After SetRenderModule, GetName() comparison matches "mesh_render".
///
/// Integration coverage gap:
/// The full billboard-batch vertex count assertion (billboard batch contains
/// 0 verts from the mesh emitter, N verts from a sprite emitter) requires a
/// live FrameContext including m_hdrSceneColor, a GBuffer, a CameraManager,
/// and GPU particle buffers.  That scenario is covered by item 3.4 (visual A/B
/// with mesh_sort_shards.uzvfx — the "vtx=0" log line from AddParticleRenderPass
/// confirms the guard fired).
///
/// All tests in this file are CPU-only — no GPU device required.
///
/// @ref runtime/engine/renderer/frame/frame_particles.cpp:399-413
/// @ref runtime/engine/vfx/modules/mesh_render_module.h

#include <gtest/gtest.h>

#include "modules/mesh_render_module.h"
#include "modules/emitter_module_stack.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <string>

using namespace uz;

/// A dead/unspawned slot zero must not prevent surviving sorted threads from
/// publishing their instance count. Only inspect the post-dead-check path;
/// the separate thread-zero initialization before that check is allowed.
TEST(MeshGuard_SortedInstanceCount, PostDeadCheckSortedPathHasNoThreadZeroGate) {
    const auto path = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path().parent_path()
        / "shaders/runtime/vfx/particle_mesh_instance_cs.hlsl";
    std::ifstream file(path);
    ASSERT_TRUE(file.is_open()) << path;
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    const std::string code = std::regex_replace(
        source, std::regex(R"(//[^\r\n]*|/\*[\s\S]*?\*/)"), " ");

    std::smatch deadCheck;
    ASSERT_TRUE(std::regex_search(
        code, deadCheck,
        std::regex(R"(if\s*\(\s*lifespan\s*<=\s*0\.0f\s*\|\|\s*age\s*>=\s*lifespan\s*\)\s*return\s*;)")))
        << "The dead-particle check must be found before inspecting the sorted path";
    const std::string survivingPath = deadCheck.suffix().str();
    std::smatch sortedBranch;
    ASSERT_TRUE(std::regex_search(
        survivingPath, sortedBranch, std::regex(R"(if\s*\(\s*isSorted\s*\)\s*\{)")))
        << "The sorted instance-count branch must be present";

    const size_t begin = sortedBranch.position() + sortedBranch.length();
    size_t end = begin;
    unsigned depth = 1;
    for (; end < survivingPath.size() && depth != 0; ++end) {
        if (survivingPath[end] == '{') ++depth;
        if (survivingPath[end] == '}') --depth;
    }
    ASSERT_EQ(depth, 0u) << "The sorted branch must have a closing brace";
    const std::string sortedPath = survivingPath.substr(begin, end - begin - 1);
    EXPECT_FALSE(std::regex_search(
        sortedPath, std::regex(R"(if\s*\(\s*sortedSlot\s*==\s*0[uU]?\s*\))")))
        << "Every surviving sorted thread must publish its count; slot zero "
           "can return at the dead-particle check";
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshRenderModule::GetName() — string identity
// ─────────────────────────────────────────────────────────────────────────────

/// MeshRenderModule::GetName() must return exactly "mesh_render".
/// The billboard guard does a strcmp against this literal — any mismatch
/// silently re-enables the double-render bug.
TEST(MeshGuard_BillboardSkipsMeshEmitter, MeshRenderModule_GetName_ExactlyMeshRender) {
    MeshRenderModule mod;
    EXPECT_STREQ(mod.GetName(), "mesh_render")
        << "GetName() must return \"mesh_render\" — used by the billboard guard "
           "strcmp in frame_particles.cpp:409-411";
}

/// GetName() must return a non-null pointer so the strcmp in the guard cannot
/// dereference a null.
TEST(MeshGuard_BillboardSkipsMeshEmitter, MeshRenderModule_GetName_IsNotNull) {
    MeshRenderModule mod;
    EXPECT_NE(mod.GetName(), nullptr)
        << "GetName() must never return nullptr";
}

/// GetName() must return the same pointer on repeated calls — it is a
/// static literal, not a heap-allocated string, so the address must be stable.
TEST(MeshGuard_BillboardSkipsMeshEmitter, MeshRenderModule_GetName_IsStableAddress) {
    MeshRenderModule mod;
    const char* first  = mod.GetName();
    const char* second = mod.GetName();
    EXPECT_EQ(first, second)
        << "GetName() must return the same static-lifetime pointer on every call";
}

// ─────────────────────────────────────────────────────────────────────────────
// Guard condition — strcmp semantics
// ─────────────────────────────────────────────────────────────────────────────

/// strcmp("mesh_render", "mesh_render") == 0 → guard fires, emitter skipped.
/// This is the exact condition in frame_particles.cpp:410-411.
TEST(MeshGuard_BillboardSkipsMeshEmitter, Strcmp_MeshRender_vs_MeshRender_IsZero) {
    MeshRenderModule mod;
    EXPECT_EQ(std::strcmp(mod.GetName(), "mesh_render"), 0)
        << "strcmp(mesh_render, mesh_render) must be 0 — guard fires";
}

/// strcmp("mesh_render", "sprite_render") != 0 → guard does NOT fire for
/// sprite emitters.  A sprite emitter must NOT be skipped by the mesh guard.
TEST(MeshGuard_BillboardSkipsMeshEmitter, Strcmp_MeshRender_vs_SpriteRender_IsNonZero) {
    MeshRenderModule mod;
    EXPECT_NE(std::strcmp(mod.GetName(), "sprite_render"), 0)
        << "strcmp(mesh_render, sprite_render) must be != 0 — guard must not "
           "skip sprite emitters";
}

/// strcmp("mesh_render", "") != 0 → guard does not fire for a module with
/// an empty name (defensive — no module should have an empty name).
TEST(MeshGuard_BillboardSkipsMeshEmitter, Strcmp_MeshRender_vs_Empty_IsNonZero) {
    MeshRenderModule mod;
    EXPECT_NE(std::strcmp(mod.GetName(), ""), 0)
        << "Guard must not fire for modules with an empty name";
}

// ─────────────────────────────────────────────────────────────────────────────
// EmitterModuleStack — null renderModule is handled safely by the guard
// ─────────────────────────────────────────────────────────────────────────────

/// A freshly constructed EmitterModuleStack has renderModule == nullptr.
/// The billboard guard checks `emitter->moduleStack.renderModule &&` before
/// strcmp, so a null render module must never trigger the guard.
TEST(MeshGuard_BillboardSkipsMeshEmitter, DefaultModuleStack_RenderModule_IsNull) {
    EmitterModuleStack stack;
    EXPECT_EQ(stack.renderModule, nullptr)
        << "EmitterModuleStack default-constructed renderModule must be null";
}

/// Evaluating the guard condition (null check → short-circuit) on a default
/// EmitterModuleStack must yield false — the emitter is NOT skipped (no render
/// module means it is a pure-sim emitter, not a mesh emitter).
TEST(MeshGuard_BillboardSkipsMeshEmitter, NullRenderModule_GuardCondition_IsFalse) {
    EmitterModuleStack stack;

    // Mirrors the guard: `renderModule && strcmp(renderModule->GetName(), "mesh_render") == 0`
    const bool guardFired =
        stack.renderModule != nullptr &&
        std::strcmp(stack.renderModule->GetName(), "mesh_render") == 0;

    EXPECT_FALSE(guardFired)
        << "Guard must not fire when renderModule is null";
}

// ─────────────────────────────────────────────────────────────────────────────
// EmitterModuleStack::SetRenderModule — MeshRenderModule installation
// ─────────────────────────────────────────────────────────────────────────────

/// After SetRenderModule(make_unique<MeshRenderModule>()), renderModule must
/// be non-null.
TEST(MeshGuard_BillboardSkipsMeshEmitter, AfterSetRenderModule_RenderModule_IsNotNull) {
    EmitterModuleStack stack;
    stack.SetRenderModule(std::make_unique<MeshRenderModule>());
    EXPECT_NE(stack.renderModule, nullptr)
        << "After SetRenderModule, renderModule must be non-null";
}

/// After installing a MeshRenderModule, evaluating the guard must yield true —
/// the emitter IS identified as a mesh emitter and would be skipped by the
/// billboard loop.
TEST(MeshGuard_BillboardSkipsMeshEmitter, MeshRenderModuleInstalled_GuardCondition_IsTrue) {
    EmitterModuleStack stack;
    stack.SetRenderModule(std::make_unique<MeshRenderModule>());

    // Mirrors the guard exactly as written in frame_particles.cpp:409-411.
    const bool guardFired =
        stack.renderModule != nullptr &&
        std::strcmp(stack.renderModule->GetName(), "mesh_render") == 0;

    EXPECT_TRUE(guardFired)
        << "Billboard guard must fire (true) when the render module is MeshRenderModule";
}

/// Replacing a MeshRenderModule with nullptr via SetRenderModule must clear
/// the renderModule — subsequent guard evaluation must yield false.
TEST(MeshGuard_BillboardSkipsMeshEmitter, SetRenderModuleNull_ClearsRenderModule) {
    EmitterModuleStack stack;
    stack.SetRenderModule(std::make_unique<MeshRenderModule>());
    ASSERT_NE(stack.renderModule, nullptr) << "precondition";

    stack.SetRenderModule(nullptr);

    const bool guardFired =
        stack.renderModule != nullptr &&
        std::strcmp(stack.renderModule->GetName(), "mesh_render") == 0;

    EXPECT_FALSE(guardFired)
        << "Guard must not fire after render module is cleared via SetRenderModule(nullptr)";
}

/// EmitterModuleStack::Clear() must reset renderModule to nullptr.
TEST(MeshGuard_BillboardSkipsMeshEmitter, ClearStack_RenderModule_BecomesNull) {
    EmitterModuleStack stack;
    stack.SetRenderModule(std::make_unique<MeshRenderModule>());
    ASSERT_NE(stack.renderModule, nullptr) << "precondition";

    stack.Clear();

    EXPECT_EQ(stack.renderModule, nullptr)
        << "EmitterModuleStack::Clear must reset renderModule to nullptr";
}

/// After Clear(), the billboard guard must not fire.
TEST(MeshGuard_BillboardSkipsMeshEmitter, AfterClear_GuardCondition_IsFalse) {
    EmitterModuleStack stack;
    stack.SetRenderModule(std::make_unique<MeshRenderModule>());
    stack.Clear();

    const bool guardFired =
        stack.renderModule != nullptr &&
        std::strcmp(stack.renderModule->GetName(), "mesh_render") == 0;

    EXPECT_FALSE(guardFired)
        << "Guard must not fire after EmitterModuleStack::Clear()";
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshRenderModule — default construction does not crash
// ─────────────────────────────────────────────────────────────────────────────

/// MeshRenderModule must be default-constructible without crashing.
TEST(MeshGuard_BillboardSkipsMeshEmitter, MeshRenderModule_DefaultConstruct_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        MeshRenderModule mod;
        (void)mod;
    });
}

/// MeshRenderModule constructed with explicit config must not crash.
TEST(MeshGuard_BillboardSkipsMeshEmitter, MeshRenderModule_ExplicitConfig_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        MeshRenderModuleConfig cfg;
        cfg.meshPath     = "meshes/tetra.uzmesh";
        cfg.materialPath = "materials/cyan_shard.uzmat";
        MeshRenderModule mod(cfg);
        (void)mod;
    });
}

/// GetName() is const and callable on a const MeshRenderModule instance.
TEST(MeshGuard_BillboardSkipsMeshEmitter, GetName_OnConstModule_ReturnsCorrectString) {
    const MeshRenderModule mod;
    EXPECT_STREQ(mod.GetName(), "mesh_render");
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshRenderModule — GPU buffer accessors before CreateGPUBuffers
// ─────────────────────────────────────────────────────────────────────────────
//
// Regression coverage for `phase0_vfx-mesh-particle-visibility-debug`:
// the visibility bug was caused by `MeshRenderModule::CreateGPUBuffers`
// never being called from `ParticleEmitter::Initialize`, leaving
// `instanceColorBuffer_` as `Invalid()`. The bindless
// `instanceColorsUavIndex` then fell back to descriptor 0 (a garbage
// resource), and the pixel shader's `ResourceDescriptorHeap[g_instanceColorsBufferIdx]`
// returned zeros — every fragment computed black, blanking the entire
// HDR composite.
//
// These CPU-only tests pin the contract: a freshly-constructed
// `MeshRenderModule` MUST report `HasGPUBuffers() == false` and
// MUST return `Invalid()` from both buffer accessors. The full
// integration ("ParticleEmitter::Initialize calls CreateGPUBuffers
// for mesh emitters") is covered by the visual A/B test (item 3.x)
// because constructing a real `IRHIDevice` is GPU-bound.

/// Default-constructed MeshRenderModule has no GPU buffers allocated.
/// This is the precondition that `ParticleEmitter::Initialize` relies on
/// when it calls `meshModule->CreateGPUBuffers(device)` exactly once.
TEST(MeshGuard_BillboardSkipsMeshEmitter,
     DefaultMeshRenderModule_HasGPUBuffers_IsFalse) {
    MeshRenderModule mod;
    EXPECT_FALSE(mod.HasGPUBuffers())
        << "A freshly-constructed MeshRenderModule must report "
           "HasGPUBuffers()==false; CreateGPUBuffers must be called by the "
           "owning ParticleEmitter before bindless descriptor lookups happen";
}

/// Default-constructed MeshRenderModule returns Invalid() from
/// GetInstanceColorBuffer(). If this ever silently flips to a default-valid
/// handle, `ParticleEmitter::Initialize` would skip the
/// `CreateGPUBuffers(device)` call (because of an `IsValid()` early-out
/// somewhere upstream), and the mesh-particle visibility regression would
/// re-appear: PS reads from descriptor 0 → `instanceColor.rgb*lighting==0` →
/// fully-black HDR composite.
/// @ref phase0_vfx-mesh-particle-visibility-debug
TEST(MeshGuard_BillboardSkipsMeshEmitter,
     DefaultMeshRenderModule_InstanceColorBuffer_IsInvalid) {
    MeshRenderModule mod;
    EXPECT_FALSE(mod.GetInstanceColorBuffer().IsValid())
        << "GetInstanceColorBuffer() must be Invalid() before CreateGPUBuffers; "
           "a default-valid handle would mask the missing CreateGPUBuffers call "
           "and re-introduce the visibility regression "
           "(phase0_vfx-mesh-particle-visibility-debug).";
}

/// Default-constructed MeshRenderModule returns Invalid() from
/// GetInstanceTransformBuffer() — same regression-pinning rationale as
/// the color buffer.
TEST(MeshGuard_BillboardSkipsMeshEmitter,
     DefaultMeshRenderModule_InstanceTransformBuffer_IsInvalid) {
    MeshRenderModule mod;
    EXPECT_FALSE(mod.GetInstanceTransformBuffer().IsValid())
        << "GetInstanceTransformBuffer() must be Invalid() before "
           "CreateGPUBuffers; tested for symmetry with the color-buffer "
           "regression test.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration coverage gap note
// ─────────────────────────────────────────────────────────────────────────────
// The spec scenario "billboard batch contains 0 vertices from this emitter
// and the mesh batch contains 1 entry" requires:
//   - A live FrameContext (hdrSceneColor, gbuffer, cameraManager)
//   - An initialized GPU particle buffer (GPUParticleBuffer::Initialize)
//   - A real IRHIDevice for buffer allocation
// These cannot be created in a CPU-only unit test.
//
// The integration coverage is provided by:
//   - Item 3.4: visual A/B screenshot of mesh_sort_shards.uzvfx confirms the
//     200 cyan tetrahedra are visible and the billboard-pass "vtx=" log line
//     reports 0 from the mesh emitter.
//   - The "ParticleMeshRenderPass: first execution -- 1 emitter" log line in
//     the same visual test confirms the mesh batch was built correctly.
