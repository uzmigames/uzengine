/// UzfxLoader unit tests — validates .uzfx JSON parsing and emitter instantiation.

#include <gtest/gtest.h>
#include "engine/vfx/asset/uzfx_loader.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "mock_device.h"

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
                "velocity": [0.0, 10.0, 0.0],
                "spread_angle": 30.0,
                "lifespan_min": 1.0,
                "lifespan_max": 3.0
            },
            "update": [
                {"type": "update", "gravity": [0.0, -9.81, 0.0], "drag": 0.98},
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
    EXPECT_EQ(desc.version, 1u);
    EXPECT_EQ(desc.maxParticles, 5000u);
    EXPECT_FLOAT_EQ(desc.emissionRate, 200.0f);
    EXPECT_FLOAT_EQ(desc.lifetime, 3.0f);
    EXPECT_TRUE(desc.looping);
    EXPECT_EQ(desc.spawnModule.type, "spawn");
    EXPECT_EQ(desc.updateModules.size(), 3u);
    EXPECT_EQ(desc.updateModules[0].type, "update");
    EXPECT_EQ(desc.updateModules[1].type, "turbulence");
    EXPECT_EQ(desc.updateModules[2].type, "decay");
    EXPECT_EQ(desc.renderModule.type, "sprite");
}

TEST(UzfxLoader, LoadEmptyJson) {
    auto desc = UzfxLoader::Load("{}");
    EXPECT_EQ(desc.name, "unnamed");
    EXPECT_EQ(desc.maxParticles, 10000u);
    EXPECT_TRUE(desc.spawnModule.type.empty());
    EXPECT_TRUE(desc.updateModules.empty());
    EXPECT_TRUE(desc.renderModule.type.empty());
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
    EXPECT_EQ(desc.maxParticles, 100u);
    EXPECT_FLOAT_EQ(desc.emissionRate, 10.0f);
    EXPECT_FLOAT_EQ(desc.lifetime, 2.0f);  // default
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
            "spawn": {"type": "spawn", "velocity": [0, 5, 0]},
            "update": [
                {"type": "update", "gravity": [0, -9.81, 0], "drag": 0.98},
                {"type": "decay"}
            ],
            "render": {"type": "sprite", "width": 0.3, "height": 0.3}
        }
    })";

    auto desc = UzfxLoader::Load(json);

    DemoMockDevice device;
    ParticleBudget budget;
    budget.Init(500000);

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

    DemoMockDevice device;
    ParticleBudget budget;
    budget.Init(500000);

    ParticleEmitter emitter;
    ASSERT_TRUE(UzfxLoader::Instantiate(desc, emitter, &device, budget));

    EXPECT_EQ(emitter.moduleStack.spawnModules.size(), 1u);
    EXPECT_EQ(emitter.moduleStack.updateModules.size(), 7u);
    EXPECT_NE(emitter.moduleStack.renderModule, nullptr);

    emitter.Shutdown(&device, budget);
}
