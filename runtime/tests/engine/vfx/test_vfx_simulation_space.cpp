/// Unit tests for phase 7.7.0 - simulation-space authoring field + enum.
///
/// Covers the shared `VfxSimulationSpace` enum contract (matching the HLSL
/// uint convention), its string round-trip, and the `ParticleEmitter`
/// default preserving world-space behaviour.
///
/// @ref phase7_7.7.0-vfx-local-space-simulation

#include <gtest/gtest.h>

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/vfx_simulation_space.h"

using namespace uz;

TEST(VfxSimulationSpace, DefaultIsWorldSpace) {
    ParticleEmitter e;
    EXPECT_EQ(e.simulationSpace, 0u);
    EXPECT_EQ(static_cast<VfxSimulationSpace>(e.simulationSpace),
              VfxSimulationSpace::WorldSpace);
}

TEST(VfxSimulationSpace, EnumValuesMatchHLSLConvention) {
    // HLSL cbuffers use uint 0/1 for the branch — keep the enum values
    // locked to that contract so shader-side branches stay legible.
    EXPECT_EQ(static_cast<uint8_t>(VfxSimulationSpace::WorldSpace), 0u);
    EXPECT_EQ(static_cast<uint8_t>(VfxSimulationSpace::LocalSpace), 1u);
}

TEST(VfxSimulationSpace, ToStringAllBranches) {
    EXPECT_STREQ(VfxSimulationSpaceToString(VfxSimulationSpace::WorldSpace), "WorldSpace");
    EXPECT_STREQ(VfxSimulationSpaceToString(VfxSimulationSpace::LocalSpace), "LocalSpace");
}

TEST(VfxSimulationSpace, FromStringRoundTrip) {
    VfxSimulationSpace out;
    ASSERT_TRUE(VfxSimulationSpaceFromString("WorldSpace", out));
    EXPECT_EQ(out, VfxSimulationSpace::WorldSpace);
    ASSERT_TRUE(VfxSimulationSpaceFromString("LocalSpace", out));
    EXPECT_EQ(out, VfxSimulationSpace::LocalSpace);
}

TEST(VfxSimulationSpace, FromStringRejectsUnknown) {
    VfxSimulationSpace out;
    EXPECT_FALSE(VfxSimulationSpaceFromString("Bogus", out));
    EXPECT_FALSE(VfxSimulationSpaceFromString("", out));
    EXPECT_FALSE(VfxSimulationSpaceFromString(nullptr, out));
}

TEST(VfxSimulationSpace, AssignableToEmitterField) {
    // Authors flip the mode by assigning the enum cast; verify the round-trip.
    ParticleEmitter e;
    e.simulationSpace = static_cast<uint8_t>(VfxSimulationSpace::LocalSpace);
    EXPECT_EQ(static_cast<VfxSimulationSpace>(e.simulationSpace),
              VfxSimulationSpace::LocalSpace);
}
