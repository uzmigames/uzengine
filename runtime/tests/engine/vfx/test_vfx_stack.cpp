/// Unit tests for the Niagara-parity VFX stack data model (phase 11.1.0).
///
/// Covers:
///   - `VfxStage` name round-trip (`VfxStageName` / `VfxStageFromName`).
///   - `VfxOverrideFlag` name round-trip.
///   - `VfxStackEntry` add / insert / remove / move / swap / find invariants.
///   - `VfxStack` construction (every slot's `stage` field matches its index),
///     `TotalModuleCount`, `IsEmpty`, `Clear`, and `FindInstance` behaviour.
///   - JSON round-trip through `SerializeEmitter` / `DeserializeEmitter`:
///     an emitter with a populated stack survives a save+load cycle, and
///     a legacy JSON without the `stages` key loads to an empty stack.
///
/// @ref engine/vfx/stack/vfx_stack.h
/// @ref engine/vfx/components/emitter_serializer.h

#include <gtest/gtest.h>

#include "vfx/stack/vfx_stack.h"
#include "vfx/components/emitter_serializer.h"
#include "vfx/components/particle_emitter.h"

#include <string>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Stage enum round-trip
// ─────────────────────────────────────────────────────────────────────────────

/// Every `VfxStage` name survives a round-trip through
/// `VfxStageName` + `VfxStageFromName`.
TEST(VfxStage, NameRoundTrip_AllStages) {
    const VfxStage stages[] = {
        VfxStage::SystemSpawn,
        VfxStage::SystemUpdate,
        VfxStage::EmitterSpawn,
        VfxStage::EmitterUpdate,
        VfxStage::ParticleSpawn,
        VfxStage::ParticleUpdate,
        VfxStage::Render,
    };
    for (VfxStage s : stages) {
        const char* name = VfxStageName(s);
        ASSERT_NE(name, nullptr);
        VfxStage parsed = VfxStage::ParticleUpdate;
        ASSERT_TRUE(VfxStageFromName(name, parsed))
            << "Round-trip failed for stage " << name;
        EXPECT_EQ(parsed, s);
    }
}

/// Unknown and nullptr inputs leave the output untouched and return false.
TEST(VfxStage, FromName_Unknown_ReturnsFalse) {
    VfxStage sentinel = VfxStage::EmitterSpawn;
    EXPECT_FALSE(VfxStageFromName("Nope", sentinel));
    EXPECT_EQ(sentinel, VfxStage::EmitterSpawn);

    EXPECT_FALSE(VfxStageFromName(nullptr, sentinel));
    EXPECT_EQ(sentinel, VfxStage::EmitterSpawn);

    EXPECT_FALSE(VfxStageFromName("", sentinel));
    EXPECT_EQ(sentinel, VfxStage::EmitterSpawn);
}

/// `kVfxStageCount` matches the enum Count value and equals 7.
TEST(VfxStage, CountMatchesEnum) {
    EXPECT_EQ(kVfxStageCount, 7);
    EXPECT_EQ(kVfxStageCount, static_cast<int>(VfxStage::Count));
}

// ─────────────────────────────────────────────────────────────────────────────
// Override flag round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOverrideFlag, NameRoundTrip_AllFlags) {
    const VfxOverrideFlag flags[] = {
        VfxOverrideFlag::None,
        VfxOverrideFlag::Inherited,
        VfxOverrideFlag::OverrideAdd,
        VfxOverrideFlag::OverrideReplace,
        VfxOverrideFlag::OverrideRemove,
    };
    for (VfxOverrideFlag f : flags) {
        const char* name = VfxOverrideFlagName(f);
        ASSERT_NE(name, nullptr);
        VfxOverrideFlag parsed = VfxOverrideFlag::None;
        ASSERT_TRUE(VfxOverrideFlagFromName(name, parsed));
        EXPECT_EQ(parsed, f);
    }
}

TEST(VfxOverrideFlag, FromName_Unknown_ReturnsFalse) {
    VfxOverrideFlag sentinel = VfxOverrideFlag::Inherited;
    EXPECT_FALSE(VfxOverrideFlagFromName("Banana", sentinel));
    EXPECT_EQ(sentinel, VfxOverrideFlag::Inherited);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxStackEntry add / remove / move / swap / find
// ─────────────────────────────────────────────────────────────────────────────

static VfxModuleInstance MakeInstance(const char* type, const char* id) {
    VfxModuleInstance m;
    m.moduleTypeName = type;
    m.instanceId     = id;
    return m;
}

TEST(VfxStackEntry, Add_AppendsAndReturnsIndex) {
    VfxStackEntry e;
    EXPECT_TRUE(e.IsEmpty());
    EXPECT_EQ(e.Add(MakeInstance("Gravity", "a")), 0u);
    EXPECT_EQ(e.Add(MakeInstance("Drag", "b")), 1u);
    EXPECT_EQ(e.Size(), 2u);
    EXPECT_EQ(e.modules[0].moduleTypeName, "Gravity");
    EXPECT_EQ(e.modules[1].moduleTypeName, "Drag");
}

TEST(VfxStackEntry, InsertAt_ClampsOutOfRange) {
    VfxStackEntry e;
    e.Add(MakeInstance("A", "a"));
    e.Add(MakeInstance("C", "c"));

    // Insert in the middle
    const size_t mid = e.InsertAt(1, MakeInstance("B", "b"));
    EXPECT_EQ(mid, 1u);
    ASSERT_EQ(e.Size(), 3u);
    EXPECT_EQ(e.modules[0].instanceId, "a");
    EXPECT_EQ(e.modules[1].instanceId, "b");
    EXPECT_EQ(e.modules[2].instanceId, "c");

    // Out-of-range clamps to end
    const size_t end = e.InsertAt(9999, MakeInstance("D", "d"));
    EXPECT_EQ(end, 3u);
    EXPECT_EQ(e.modules.back().instanceId, "d");
}

TEST(VfxStackEntry, Remove_OutOfRange_NoMutation) {
    VfxStackEntry e;
    e.Add(MakeInstance("A", "a"));
    EXPECT_FALSE(e.Remove(5));
    EXPECT_EQ(e.Size(), 1u);

    EXPECT_TRUE(e.Remove(0));
    EXPECT_TRUE(e.IsEmpty());
}

TEST(VfxStackEntry, MoveUpMoveDown_EdgesRejected) {
    VfxStackEntry e;
    e.Add(MakeInstance("A", "a"));
    e.Add(MakeInstance("B", "b"));
    e.Add(MakeInstance("C", "c"));

    EXPECT_FALSE(e.MoveUp(0));   // already at top
    EXPECT_FALSE(e.MoveDown(2)); // already at bottom
    EXPECT_FALSE(e.MoveUp(99));  // oob
    EXPECT_FALSE(e.MoveDown(99));

    EXPECT_TRUE(e.MoveUp(2));
    // order: A, C, B
    EXPECT_EQ(e.modules[0].instanceId, "a");
    EXPECT_EQ(e.modules[1].instanceId, "c");
    EXPECT_EQ(e.modules[2].instanceId, "b");

    EXPECT_TRUE(e.MoveDown(0));
    // order: C, A, B
    EXPECT_EQ(e.modules[0].instanceId, "c");
    EXPECT_EQ(e.modules[1].instanceId, "a");
    EXPECT_EQ(e.modules[2].instanceId, "b");
}

TEST(VfxStackEntry, Swap_SameIndex_NoOpTrue) {
    VfxStackEntry e;
    e.Add(MakeInstance("A", "a"));
    EXPECT_TRUE(e.Swap(0, 0));
    EXPECT_EQ(e.modules[0].instanceId, "a");
}

TEST(VfxStackEntry, Find_ReturnsIndexOrSentinel) {
    VfxStackEntry e;
    e.Add(MakeInstance("A", "alpha"));
    e.Add(MakeInstance("B", "beta"));

    EXPECT_EQ(e.Find("alpha"), 0u);
    EXPECT_EQ(e.Find("beta"), 1u);
    EXPECT_EQ(e.Find("gamma"), static_cast<size_t>(-1));
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxStack construction + aggregate helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStack, Construct_StageFieldsMatchIndex) {
    VfxStack s;
    const auto& entries = s.Entries();
    ASSERT_EQ(entries.size(), static_cast<size_t>(kVfxStageCount));
    for (uint8_t i = 0; i < kVfxStageCount; ++i) {
        EXPECT_EQ(entries[i].stage, static_cast<VfxStage>(i))
            << "Entry slot " << int(i) << " stage mismatch";
    }
}

TEST(VfxStack, IsEmpty_FreshStack) {
    VfxStack s;
    EXPECT_TRUE(s.IsEmpty());
    EXPECT_EQ(s.TotalModuleCount(), 0u);
}

TEST(VfxStack, TotalModuleCount_AggregatesAllStages) {
    VfxStack s;
    s.GetEntry(VfxStage::ParticleSpawn).Add(MakeInstance("Init", "p_init"));
    s.GetEntry(VfxStage::ParticleUpdate).Add(MakeInstance("Gravity", "p_grav"));
    s.GetEntry(VfxStage::ParticleUpdate).Add(MakeInstance("Drag", "p_drag"));
    s.GetEntry(VfxStage::Render).Add(MakeInstance("Sprite", "r_sprite"));

    EXPECT_FALSE(s.IsEmpty());
    EXPECT_EQ(s.TotalModuleCount(), 4u);
}

TEST(VfxStack, Clear_EmptiesAllStagesButKeepsStages) {
    VfxStack s;
    s.GetEntry(VfxStage::Render).Add(MakeInstance("Sprite", "r"));
    s.Clear();
    EXPECT_TRUE(s.IsEmpty());
    // Stage fields preserved
    for (uint8_t i = 0; i < kVfxStageCount; ++i) {
        EXPECT_EQ(s.Entries()[i].stage, static_cast<VfxStage>(i));
    }
}

TEST(VfxStack, FindInstance_HitAndMiss) {
    VfxStack s;
    s.GetEntry(VfxStage::ParticleUpdate).Add(MakeInstance("Gravity", "g1"));
    s.GetEntry(VfxStage::Render).Add(MakeInstance("Sprite", "r1"));

    VfxStage stage = VfxStage::SystemSpawn;
    size_t index = 999;

    EXPECT_TRUE(s.FindInstance("g1", stage, index));
    EXPECT_EQ(stage, VfxStage::ParticleUpdate);
    EXPECT_EQ(index, 0u);

    EXPECT_TRUE(s.FindInstance("r1", stage, index));
    EXPECT_EQ(stage, VfxStage::Render);

    stage = VfxStage::SystemSpawn;
    index = 999;
    EXPECT_FALSE(s.FindInstance("missing", stage, index));
    // Out-params untouched on miss
    EXPECT_EQ(stage, VfxStage::SystemSpawn);
    EXPECT_EQ(index, 999u);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON round-trip via emitter_serializer
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStackSerializer, EmptyStack_NoStagesKeyInOutput) {
    ParticleEmitter e;
    std::string out = SerializeEmitter(e);
    // Empty stack must not emit `stages` — legacy files stay diff-minimal.
    EXPECT_EQ(out.find("\"stages\""), std::string::npos);
}

TEST(VfxStackSerializer, PopulatedStack_RoundTripPreservesLayout) {
    ParticleEmitter src;

    auto& ps = src.stack.GetEntry(VfxStage::ParticleSpawn);
    {
        VfxModuleInstance m = MakeInstance("InitPosition", "spawn_init");
        m.configJson = R"({"radius":2.5})";
        m.enabled = true;
        m.overrideFlag = VfxOverrideFlag::OverrideReplace;
        ps.Add(m);
    }

    auto& pu = src.stack.GetEntry(VfxStage::ParticleUpdate);
    {
        VfxModuleInstance m = MakeInstance("Gravity", "upd_gravity");
        m.configJson = R"({"vector":[0,-980,0]})";
        m.enabled = false;
        m.overrideFlag = VfxOverrideFlag::Inherited;
        pu.Add(m);
    }
    {
        VfxModuleInstance m = MakeInstance("Drag", "upd_drag");
        // configJson left empty — serializer must tolerate this
        pu.Add(m);
    }

    auto& r = src.stack.GetEntry(VfxStage::Render);
    r.Add(MakeInstance("SpriteRenderer", "render_sprite"));

    const std::string json = SerializeEmitter(src);

    ParticleEmitter dst;
    ASSERT_TRUE(DeserializeEmitter(json, dst));

    ASSERT_FALSE(dst.stack.IsEmpty());
    EXPECT_EQ(dst.stack.TotalModuleCount(), 4u);

    // ParticleSpawn entry
    const auto& dps = dst.stack.GetEntry(VfxStage::ParticleSpawn);
    ASSERT_EQ(dps.Size(), 1u);
    EXPECT_EQ(dps.modules[0].moduleTypeName, "InitPosition");
    EXPECT_EQ(dps.modules[0].instanceId, "spawn_init");
    EXPECT_TRUE(dps.modules[0].enabled);
    EXPECT_EQ(dps.modules[0].overrideFlag, VfxOverrideFlag::OverrideReplace);
    // configJson survives — either verbatim or re-encoded with equivalent fields.
    EXPECT_NE(dps.modules[0].configJson.find("2.5"), std::string::npos);

    // ParticleUpdate entry (two modules, ordering preserved)
    const auto& dpu = dst.stack.GetEntry(VfxStage::ParticleUpdate);
    ASSERT_EQ(dpu.Size(), 2u);
    EXPECT_EQ(dpu.modules[0].instanceId, "upd_gravity");
    EXPECT_EQ(dpu.modules[1].instanceId, "upd_drag");
    EXPECT_FALSE(dpu.modules[0].enabled);
    EXPECT_EQ(dpu.modules[0].overrideFlag, VfxOverrideFlag::Inherited);

    // Render entry
    const auto& dr = dst.stack.GetEntry(VfxStage::Render);
    ASSERT_EQ(dr.Size(), 1u);
    EXPECT_EQ(dr.modules[0].moduleTypeName, "SpriteRenderer");

    // Stage field invariant preserved post-deserialize
    for (uint8_t i = 0; i < kVfxStageCount; ++i) {
        EXPECT_EQ(dst.stack.Entries()[i].stage, static_cast<VfxStage>(i));
    }
}

TEST(VfxStackSerializer, LegacyJsonWithoutStages_LoadsEmptyStack) {
    // Legacy asset — produced before phase 11.1.
    const std::string legacyJson = R"({
        "maxParticles": 1024,
        "emissionRate": 50.0,
        "lifetime": 2.0,
        "spawnVelocity": [0,1,0],
        "gravity": [0,-9.8,0]
    })";

    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(legacyJson, e));
    EXPECT_TRUE(e.stack.IsEmpty())
        << "Legacy JSON without 'stages' must produce an empty stack, "
           "falling back to inline-config behaviour.";
    EXPECT_EQ(e.maxParticles, 1024u);
    EXPECT_FLOAT_EQ(e.emissionRate, 50.0f);
}

TEST(VfxStackSerializer, Deserialize_BadStageNameFallsBackToParticleUpdate) {
    // An unknown stage name must not break the load. Serializer behaviour:
    // VfxStageFromName fails → `stage` stays at its default
    // (ParticleUpdate, matching the serializer's default init).
    const std::string json = R"({
        "stages": [
            {
                "stage": "NotAStage",
                "modules": [
                    {"type": "Gravity", "instanceId": "g"}
                ]
            }
        ]
    })";

    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    // Module lands in the fallback stage.
    const auto& pu = e.stack.GetEntry(VfxStage::ParticleUpdate);
    ASSERT_EQ(pu.Size(), 1u);
    EXPECT_EQ(pu.modules[0].instanceId, "g");
}

TEST(VfxStackSerializer, Deserialize_ModuleWithoutType_IsSkipped) {
    // Tolerance: a module without `type` is skipped silently so a
    // corrupted asset does not abort the entire load.
    const std::string json = R"({
        "stages": [
            {
                "stage": "ParticleUpdate",
                "modules": [
                    {"instanceId": "no_type"},
                    {"type": "Gravity", "instanceId": "keep"}
                ]
            }
        ]
    })";

    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    const auto& pu = e.stack.GetEntry(VfxStage::ParticleUpdate);
    ASSERT_EQ(pu.Size(), 1u);
    EXPECT_EQ(pu.modules[0].instanceId, "keep");
}
