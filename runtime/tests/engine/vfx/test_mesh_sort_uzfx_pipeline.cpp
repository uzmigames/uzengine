/// End-to-end test: `.uzfx` JSON -> DeserializeEmitter -> ApplyEmitterStackToModuleStack
/// -> MeshRenderModule with sortMode == ViewDepth.
///
/// Exercises the sample asset shipped with the
/// `phase16_vfx-particle-sort-pipeline-refactor` task (5.1). Validates that:
///
/// 1. The serializer accepts a top-level `stages` array containing a
///    `Render` stage with a `mesh_render` module.
/// 2. `ApplyEmitterStackToModuleStack` instantiates `MeshRenderModule`
///    via `ModuleRegistry::Create` and applies the embedded JSON config
///    via the new `ApplyJsonConfig` override.
/// 3. The materialized module reads `sort_mode: "ViewDepth"` correctly.
///
/// No GPU hardware required — CPU-only.

#include <gtest/gtest.h>

#include "components/apply_module_stack.h"
#include "components/emitter_serializer.h"
#include "components/particle_emitter.h"
#include "modules/mesh_render_module.h"
#include "modules/module_registry.h"

#include <string>

using namespace uz;

namespace {

/// Sample asset content embedded directly so the test does not depend on
/// disk I/O or a working VFS. Mirrors `sample/content/vfx/tests/
/// mesh_sort_shards.uzfx` byte-for-byte (minus the `_comment` field which
/// is trimmed for diff resilience).
constexpr const char* kMeshSortShardsJson = R"({
    "maxParticles": 200,
    "emissionRate": 40.0,
    "lifetime": 5.0,
    "blendMode": 2,
    "stages": [
        {
            "stage": "Render",
            "modules": [
                {
                    "type": "mesh_render",
                    "instanceId": "shard_renderer",
                    "enabled": true,
                    "config": {
                        "mesh_path": "meshes/test_shard.uzmesh",
                        "material_path": "materials/test_glass.uzmat",
                        "mesh_scale": [1.0, 1.0, 1.0],
                        "cast_shadow": false,
                        "rotation_mode": "Random",
                        "sort_mode": "ViewDepth",
                        "max_instances": 256
                    }
                }
            ]
        }
    ]
})";

/// Builds a minimal registry with only the modules this test cares about.
/// Avoids dragging the full `RegisterBuiltinModules` (and its dependencies)
/// into the test harness.
ModuleRegistry MakeRegistryWithMeshRender() {
    ModuleRegistry registry;
    registry.Register<MeshRenderModule>("mesh_render");
    return registry;
}

} // anonymous namespace

// =============================================================================
// DeserializeEmitter populates `e.stack` with the render module entry
// =============================================================================

TEST(UZ_MeshSortUzfxPipeline, Deserialize_PopulatesRenderStage) {
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(kMeshSortShardsJson, e));

    const auto& renderEntry = e.stack.GetEntry(VfxStage::Render);
    ASSERT_EQ(renderEntry.modules.size(), 1u)
        << "Render stage must hold exactly one VfxModuleInstance";

    const auto& mod = renderEntry.modules[0];
    EXPECT_EQ(mod.moduleTypeName, "mesh_render");
    EXPECT_EQ(mod.instanceId, "shard_renderer");
    EXPECT_TRUE(mod.enabled);
    EXPECT_FALSE(mod.configJson.empty())
        << "configJson must hold the embedded mesh-render config blob";
}

// =============================================================================
// ApplyEmitterStackToModuleStack builds the runtime MeshRenderModule
// =============================================================================

TEST(UZ_MeshSortUzfxPipeline, ApplyStack_InstantiatesMeshRenderModule) {
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(kMeshSortShardsJson, e));

    const auto registry = MakeRegistryWithMeshRender();
    ApplyEmitterStackToModuleStack(e, registry);

    ASSERT_NE(e.moduleStack.renderModule, nullptr)
        << "Render module must be set on the runtime moduleStack";

    EXPECT_STREQ(e.moduleStack.renderModule->GetName(), "mesh_render");

    // Down-cast to inspect the typed config — the registry returned an
    // IVFXModule* but the test verifies the concrete type.
    const auto* mesh = dynamic_cast<const MeshRenderModule*>(
        e.moduleStack.renderModule.get());
    ASSERT_NE(mesh, nullptr) << "Registered factory must produce MeshRenderModule";

    const auto& cfg = mesh->GetConfig();
    EXPECT_EQ(cfg.sortMode, MeshSortMode::ViewDepth);
    EXPECT_EQ(cfg.rotationMode, MeshRotationMode::Random);
    EXPECT_EQ(cfg.meshPath, "meshes/test_shard.uzmesh");
    EXPECT_EQ(cfg.materialPath, "materials/test_glass.uzmat");
    EXPECT_FALSE(cfg.castShadow);
    EXPECT_EQ(cfg.maxInstances, 256u);
}

// =============================================================================
// ApplyEmitterStackToModuleStack is idempotent
// =============================================================================

TEST(UZ_MeshSortUzfxPipeline, ApplyStack_CalledTwice_DoesNotDuplicate) {
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(kMeshSortShardsJson, e));
    const auto registry = MakeRegistryWithMeshRender();

    ApplyEmitterStackToModuleStack(e, registry);
    auto* first = dynamic_cast<MeshRenderModule*>(e.moduleStack.renderModule.get());
    ASSERT_NE(first, nullptr);
    first->ApplyJsonConfig(nlohmann::json{{"sort_mode", "None"}, {"max_instances", 1}});
    ASSERT_EQ(first->GetConfig().sortMode, MeshSortMode::None);
    ASSERT_EQ(first->GetConfig().maxInstances, 1u);

    // An allocator may reuse the address of the released module. Verify the
    // observable contract: materialization replaces stale runtime configuration.
    ApplyEmitterStackToModuleStack(e, registry);
    ASSERT_NE(e.moduleStack.renderModule, nullptr);

    const auto* mesh = dynamic_cast<const MeshRenderModule*>(
        e.moduleStack.renderModule.get());
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->GetConfig().sortMode, MeshSortMode::ViewDepth);
    EXPECT_EQ(mesh->GetConfig().maxInstances, 256u);
    EXPECT_TRUE(e.moduleStack.spawnModules.empty());
    EXPECT_TRUE(e.moduleStack.updateModules.empty());

    // Removing the serialized module must also remove the previously live one.
    e.stack.GetEntry(VfxStage::Render).modules[0].enabled = false;
    ApplyEmitterStackToModuleStack(e, registry);
    EXPECT_EQ(e.moduleStack.renderModule, nullptr);
}

// =============================================================================
// Disabled modules are skipped
// =============================================================================

TEST(UZ_MeshSortUzfxPipeline, ApplyStack_SkipsDisabledRenderModule) {
    constexpr const char* kDisabledJson = R"({
        "stages": [
            {
                "stage": "Render",
                "modules": [
                    {
                        "type": "mesh_render",
                        "enabled": false,
                        "config": { "sort_mode": "ViewDepth" }
                    }
                ]
            }
        ]
    })";
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(kDisabledJson, e));

    const auto registry = MakeRegistryWithMeshRender();
    ApplyEmitterStackToModuleStack(e, registry);

    EXPECT_EQ(e.moduleStack.renderModule, nullptr)
        << "Disabled render modules must not be instantiated";
}

// =============================================================================
// Unknown module types are skipped without aborting the rest of the stack
// =============================================================================

TEST(UZ_MeshSortUzfxPipeline, ApplyStack_UnknownTypeIsSkipped) {
    constexpr const char* kUnknownTypeJson = R"({
        "stages": [
            {
                "stage": "Render",
                "modules": [
                    {
                        "type": "mesh_render_typo_xyz",
                        "enabled": true,
                        "config": { "sort_mode": "ViewDepth" }
                    }
                ]
            }
        ]
    })";
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(kUnknownTypeJson, e));

    const auto registry = MakeRegistryWithMeshRender();
    ApplyEmitterStackToModuleStack(e, registry);

    EXPECT_EQ(e.moduleStack.renderModule, nullptr)
        << "Unknown module type names must not produce a render module";
}

// =============================================================================
// CustomAttribute with an empty attribute name still downgrades to None on
// load (validates that `MeshRenderModule::ApplyJsonConfig` reuses the same
// `ParseConfig` path as direct construction — the downgrade-and-warn rule
// applies uniformly across both code paths).
// =============================================================================

TEST(UZ_MeshSortUzfxPipeline,
     ApplyStack_CustomAttributeWithoutName_DowngradesToNone) {
    constexpr const char* kCustomNoNameJson = R"({
        "stages": [
            {
                "stage": "Render",
                "modules": [
                    {
                        "type": "mesh_render",
                        "enabled": true,
                        "config": { "sort_mode": "CustomAttribute" }
                    }
                ]
            }
        ]
    })";
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(kCustomNoNameJson, e));

    const auto registry = MakeRegistryWithMeshRender();
    ApplyEmitterStackToModuleStack(e, registry);

    ASSERT_NE(e.moduleStack.renderModule, nullptr);
    const auto* mesh = dynamic_cast<const MeshRenderModule*>(
        e.moduleStack.renderModule.get());
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->GetConfig().sortMode, MeshSortMode::None)
        << "CustomAttribute with empty custom_sort_attribute must downgrade "
           "to None at parse time, identical to direct ParseConfig behaviour";
}
