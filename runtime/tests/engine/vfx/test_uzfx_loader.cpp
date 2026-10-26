/// UzfxLoader unit tests — validates .uzfx JSON parsing and emitter instantiation.

#include <gtest/gtest.h>
#include "engine/asset/formats/uzfx_loader.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/modules/spawn_module.h"
#include "mock_rhi_device.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Load — JSON parsing
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzfxLoader, LoadValidJson) {
    const char* json = R"({
        "name": "Test Effect",
        "version": 1,
        "emitter": {
            "max_particles": 5000,
            "emission_rate": 200.0,
            "lifetime": 3.0,
            "looping": true
        },
        "modules": {
            "spawn": {
                "type": "spawn",
                "velocity": [0.0, 0.0, 1000.0],
                "spread_angle": 30.0,
                "lifespan_min": 1.0,
                "lifespan_max": 3.0
            },
            "update": [
                {"type": "update", "gravity": [0.0, 0.0, -981.0], "drag": 0.98},
                {"type": "turbulence", "strength": 2.0, "frequency": 0.5, "octaves": 3},
                {"type": "decay"}
            ],
            "render": {
                "type": "sprite",
                "width": 0.5,
                "height": 0.5
            }
        }
    })";

    auto desc = UzfxLoader::Load(json);
    EXPECT_EQ(desc.name, "Test Effect");
    ASSERT_EQ(desc.emitters.size(), 1u);
    const auto& em = desc.emitters[0];
    EXPECT_EQ(em.maxParticles, 5000u);
    EXPECT_FLOAT_EQ(em.emissionRate, 200.0f);
    EXPECT_FLOAT_EQ(em.lifetime, 3.0f);
    EXPECT_TRUE(em.looping);
    EXPECT_EQ(em.spawnModule.type, "spawn");
    EXPECT_EQ(em.updateModules.size(), 3u);
    EXPECT_EQ(em.updateModules[0].type, "update");
    EXPECT_EQ(em.updateModules[1].type, "turbulence");
    EXPECT_EQ(em.updateModules[2].type, "decay");
    EXPECT_EQ(em.renderModule.type, "sprite");
}

TEST(UzfxLoader, LoadEmptyJson) {
    auto desc = UzfxLoader::Load("{}");
    EXPECT_EQ(desc.name, "unnamed");
    ASSERT_EQ(desc.emitters.size(), 1u);
    const auto& em = desc.emitters[0];
    EXPECT_EQ(em.maxParticles, 10000u);
    EXPECT_TRUE(em.spawnModule.type.empty());
    EXPECT_TRUE(em.updateModules.empty());
    EXPECT_TRUE(em.renderModule.type.empty());
}

TEST(UzfxLoader, LoadInvalidJson) {
    auto desc = UzfxLoader::Load("not json at all {{{");
    EXPECT_TRUE(desc.name.empty());
}

TEST(UzfxLoader, LoadEmitterOnlyNoModules) {
    const char* json = R"({
        "name": "Minimal",
        "emitter": {"max_particles": 100, "emission_rate": 10.0}
    })";

    auto desc = UzfxLoader::Load(json);
    EXPECT_EQ(desc.name, "Minimal");
    ASSERT_EQ(desc.emitters.size(), 1u);
    EXPECT_EQ(desc.emitters[0].maxParticles, 100u);
    EXPECT_FLOAT_EQ(desc.emitters[0].emissionRate, 10.0f);
    EXPECT_FLOAT_EQ(desc.emitters[0].lifetime, 2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Instantiate — emitter creation from descriptor
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzfxLoader, InstantiateFromDesc) {
    const char* json = R"({
        "name": "Fountain",
        "emitter": {
            "max_particles": 1000,
            "emission_rate": 100.0,
            "lifetime": 2.0
        },
        "modules": {
            "spawn": {"type": "spawn", "velocity": [0, 0, 500]},
            "update": [
                {"type": "update", "gravity": [0, -9.81, 0], "drag": 0.98},
                {"type": "decay"}
            ],
            "render": {"type": "sprite", "width": 0.3, "height": 0.3}
        }
    })";

    auto desc = UzfxLoader::Load(json);

    test::MockRHIDevice device;
    ParticleBudget budget(500000);

    ParticleEmitter emitter;
    ASSERT_TRUE(UzfxLoader::Instantiate(desc, emitter, &device, budget));

    EXPECT_EQ(emitter.maxParticles, 1000u);
    EXPECT_FLOAT_EQ(emitter.emissionRate, 100.0f);
    EXPECT_FLOAT_EQ(emitter.lifetime, 2.0f);
    EXPECT_TRUE(emitter.isEmitting);
    EXPECT_GT(emitter.allocatedFromBudget, 0u);
    EXPECT_EQ(emitter.moduleStack.spawnModules.size(), 1u);
    EXPECT_EQ(emitter.moduleStack.updateModules.size(), 2u);
    EXPECT_NE(emitter.moduleStack.renderModule, nullptr);

    emitter.Shutdown(&device, budget);
    EXPECT_EQ(budget.GetUsed(), 0u);
}

TEST(UzfxLoader, InstantiateAllModuleTypes) {
    const char* json = R"({
        "name": "AllModules",
        "emitter": {"max_particles": 500},
        "modules": {
            "spawn": {"type": "spawn"},
            "update": [
                {"type": "update"},
                {"type": "turbulence", "strength": 1.0},
                {"type": "attractor", "position": [0,0,0], "strength": 10.0},
                {"type": "orbit", "center": [0,0,0], "speed": 2.0},
                {"type": "collision", "bounce_factor": 0.5},
                {"type": "vector_field", "intensity": 3.0},
                {"type": "decay"}
            ],
            "render": {"type": "mesh", "mesh_path": "meshes/spark.obj"}
        }
    })";

    auto desc = UzfxLoader::Load(json);

    test::MockRHIDevice device;
    ParticleBudget budget(500000);

    ParticleEmitter emitter;
    ASSERT_TRUE(UzfxLoader::Instantiate(desc, emitter, &device, budget));

    EXPECT_EQ(emitter.moduleStack.spawnModules.size(), 1u);
    EXPECT_EQ(emitter.moduleStack.updateModules.size(), 7u);
    EXPECT_NE(emitter.moduleStack.renderModule, nullptr);

    emitter.Shutdown(&device, budget);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh render module — sort + rotation + custom-attribute fields
//
// Validates that the production `.uzvfx` loader (UzfxLoader::InstantiateEmitter)
// reads every field added by phase16_vfx-particle-sort-pipeline-refactor:
// `rotation_mode`, `sort_mode`, `custom_sort_attribute`, `max_instances`.
// Before this task those fields were silently ignored even when present in
// the asset; ensuring they round-trip through the production path is what
// makes the depth-sort feature usable from data.
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/vfx/modules/mesh_render_module.h"

TEST(UzfxLoader, MeshRender_ParsesSortAndRotationFields) {
    const char* json = R"({
        "name": "ShardMeshes",
        "emitter": {"max_particles": 500, "lifetime": 4.0},
        "modules": {
            "spawn": {"type": "spawn"},
            "render": {
                "type": "mesh",
                "mesh_path": "meshes/test_shard.uzmesh",
                "material_path": "materials/test_glass.uzmat",
                "scale": [2.0, 1.0, 0.5],
                "cast_shadow": false,
                "rotation_mode": "Random",
                "sort_mode": "ViewDepth",
                "max_instances": 256
            }
        }
    })";

    auto desc = UzfxLoader::Load(json);
    ASSERT_FALSE(desc.emitters.empty());

    test::MockRHIDevice device;
    ParticleBudget budget(500'000);

    ParticleEmitter emitter;
    ASSERT_TRUE(UzfxLoader::Instantiate(desc, emitter, &device, budget));

    ASSERT_NE(emitter.moduleStack.renderModule, nullptr);
    const auto* mesh = dynamic_cast<const MeshRenderModule*>(
        emitter.moduleStack.renderModule.get());
    ASSERT_NE(mesh, nullptr) << "render type='mesh' must produce MeshRenderModule";

    const auto& cfg = mesh->GetConfig();
    EXPECT_EQ(cfg.rotationMode, MeshRotationMode::Random)
        << "rotation_mode must be parsed from .uzvfx render block";
    EXPECT_EQ(cfg.sortMode, MeshSortMode::ViewDepth)
        << "sort_mode must be parsed from .uzvfx render block";
    EXPECT_EQ(cfg.maxInstances, 256u)
        << "max_instances must be parsed from .uzvfx render block";

    emitter.Shutdown(&device, budget);
}

TEST(UzfxLoader, MeshRender_DefaultsWhenSortFieldsAbsent) {
    // Legacy .uzvfx files that predate the sort pipeline must continue to
    // load with sortMode == None and rotationMode == VelocityAligned (the
    // historical engine default before the new fields existed).
    const char* json = R"({
        "name": "LegacyMeshAsset",
        "emitter": {"max_particles": 200},
        "modules": {
            "spawn": {"type": "spawn"},
            "render": {
                "type": "mesh",
                "mesh_path": "meshes/legacy.uzmesh"
            }
        }
    })";

    auto desc = UzfxLoader::Load(json);
    test::MockRHIDevice device;
    ParticleBudget budget(500'000);

    ParticleEmitter emitter;
    ASSERT_TRUE(UzfxLoader::Instantiate(desc, emitter, &device, budget));

    const auto* mesh = dynamic_cast<const MeshRenderModule*>(
        emitter.moduleStack.renderModule.get());
    ASSERT_NE(mesh, nullptr);

    EXPECT_EQ(mesh->GetConfig().sortMode, MeshSortMode::None)
        << "Missing sort_mode must default to None for backward compat";
    EXPECT_EQ(mesh->GetConfig().rotationMode, MeshRotationMode::VelocityAligned)
        << "Missing rotation_mode must default to VelocityAligned";

    emitter.Shutdown(&device, budget);
}

TEST(UzfxLoader, MeshRender_CustomAttributeWithoutNameDowngradesToNone) {
    // The production loader emits a one-time warning AND falls back to
    // sortMode=None when an asset selects CustomAttribute without naming
    // the attribute — same contract as MeshRenderModule::ParseConfig.
    const char* json = R"({
        "name": "BadCustomSort",
        "emitter": {"max_particles": 100},
        "modules": {
            "spawn": {"type": "spawn"},
            "render": {
                "type": "mesh",
                "mesh_path": "meshes/x.uzmesh",
                "sort_mode": "CustomAttribute"
            }
        }
    })";

    auto desc = UzfxLoader::Load(json);
    test::MockRHIDevice device;
    ParticleBudget budget(500'000);

    ParticleEmitter emitter;
    ASSERT_TRUE(UzfxLoader::Instantiate(desc, emitter, &device, budget));

    const auto* mesh = dynamic_cast<const MeshRenderModule*>(
        emitter.moduleStack.renderModule.get());
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->GetConfig().sortMode, MeshSortMode::None)
        << "CustomAttribute without custom_sort_attribute must downgrade to None";
    EXPECT_TRUE(mesh->GetConfig().customSortAttribute.empty());

    emitter.Shutdown(&device, budget);
}

TEST(UzfxLoader, MeshRender_CustomAttributeWithNamePreservesMode) {
    const char* json = R"({
        "name": "ValidCustomSort",
        "emitter": {"max_particles": 100},
        "modules": {
            "spawn": {"type": "spawn"},
            "render": {
                "type": "mesh",
                "mesh_path": "meshes/x.uzmesh",
                "sort_mode": "CustomAttribute",
                "custom_sort_attribute": "Mass"
            }
        }
    })";

    auto desc = UzfxLoader::Load(json);
    test::MockRHIDevice device;
    ParticleBudget budget(500'000);

    ParticleEmitter emitter;
    ASSERT_TRUE(UzfxLoader::Instantiate(desc, emitter, &device, budget));

    const auto* mesh = dynamic_cast<const MeshRenderModule*>(
        emitter.moduleStack.renderModule.get());
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->GetConfig().sortMode, MeshSortMode::CustomAttribute);
    EXPECT_EQ(mesh->GetConfig().customSortAttribute, "Mass");

    emitter.Shutdown(&device, budget);
}

// ─────────────────────────────────────────────────────────────────────────────
// Crafted assets — wrong element types must not throw out of the loader
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzfxLoader, Vec3WithNonNumericElements_FallsBackToDefaultWithoutThrowing) {
    // get<float>() on a string/null/object element throws nlohmann::type_error;
    // the loader must treat such vectors as absent instead.
    const char* json = R"({
        "name": "Crafted",
        "emitter": {"max_particles": 100},
        "modules": {
            "spawn": {"type": "spawn", "velocity": ["x", null, {}]},
            "update": [
                {"type": "update", "gravity": [true, 1, 2]},
                {"type": "attractor", "position": [[1], [2], [3]]}
            ]
        }
    })";

    auto desc = UzfxLoader::Load(json);

    test::MockRHIDevice device;
    ParticleBudget budget(500'000);
    ParticleEmitter emitter;
    bool instantiated = false;
    EXPECT_NO_THROW(instantiated = UzfxLoader::Instantiate(desc, emitter, &device, budget));
    ASSERT_TRUE(instantiated);

    ASSERT_EQ(emitter.moduleStack.spawnModules.size(), 1u);
    const auto* spawn = dynamic_cast<const SpawnModule*>(
        emitter.moduleStack.spawnModules[0].get());
    ASSERT_NE(spawn, nullptr);
    EXPECT_FLOAT_EQ(spawn->velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(spawn->velocity.y, 0.0f);
    EXPECT_FLOAT_EQ(spawn->velocity.z, 200.0f); // loader default for a missing velocity
    EXPECT_EQ(emitter.moduleStack.updateModules.size(), 2u);

    emitter.Shutdown(&device, budget);
}
