// test_cvar.cpp — Tests for CVar core types and CVarRegistry.

#include <gtest/gtest.h>
#include "engine/core/cvar.h"
#include "engine/core/cvar_registry.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace uz;

// ============================================================================
// CVarRegistry — registration basics
// ============================================================================

TEST(CVarRegistry, RegisterFloat_GetReturnsDefault) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.fov", "Field of view", 90.0f);

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name, "r.fov");
    EXPECT_EQ(entry->description, "Field of view");
    EXPECT_EQ(entry->type, CVarType::FLOAT);
    EXPECT_EQ(entry->flags, CVAR_NONE);
    EXPECT_FLOAT_EQ(std::get<float>(entry->default_value), 90.0f);
    EXPECT_FLOAT_EQ(reg.get<float>("r.fov"), 90.0f);
}

TEST(CVarRegistry, SetFloat_GetReturnsNewValue) {
    CVarRegistry reg;
    reg.register_cvar("r.fov", "Field of view", 90.0f);

    bool ok = reg.set<float>("r.fov", 120.0f);

    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(reg.get<float>("r.fov"), 120.0f);
}

TEST(CVarRegistry, SetReadonly_ReturnsFalse_ValueUnchanged) {
    CVarRegistry reg;
    reg.register_cvar("sys.version", "Engine version", 1);

    // Make it readonly by registering with the flag
    CVarRegistry reg2;
    reg2.register_cvar("sys.version", "Engine version", 1,
                       CVAR_READONLY);

    bool ok = reg2.set<int32_t>("sys.version", 999);

    EXPECT_FALSE(ok);
    EXPECT_EQ(reg2.get<int32_t>("sys.version"), 1);
}

TEST(CVarRegistry, FindByName_UnknownReturnsNullptr) {
    CVarRegistry reg;

    const CVarEntry* result = reg.find_by_name("nonexistent");

    EXPECT_EQ(result, nullptr);
}

TEST(CVarRegistry, FindByNameMut_UnknownReturnsNullptr) {
    CVarRegistry reg;

    CVarEntry* result = reg.find_by_name_mut("nonexistent");

    EXPECT_EQ(result, nullptr);
}

TEST(CVarRegistry, RegisterMultiple_GetAllReturnsAll) {
    CVarRegistry reg;
    reg.register_cvar("a.bool", "A bool", true);
    reg.register_cvar("b.int", "An int", int32_t{42});
    reg.register_cvar("c.float", "A float", 3.14f);

    auto all = reg.get_all();

    ASSERT_EQ(all.size(), 3u);

    // Collect names for order-independent comparison.
    std::vector<std::string> names;
    names.reserve(all.size());
    for (const auto* e : all) {
        names.push_back(e->name);
    }
    std::sort(names.begin(), names.end());

    EXPECT_EQ(names[0], "a.bool");
    EXPECT_EQ(names[1], "b.int");
    EXPECT_EQ(names[2], "c.float");
}

// ============================================================================
// Type-safe registration and get/set for each supported type
// ============================================================================

TEST(CVarRegistry, RegisterBool_TypeSafeGetSet) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.vsync", "Enable vsync", true);

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type, CVarType::BOOL);
    EXPECT_TRUE(reg.get<bool>("r.vsync"));

    EXPECT_TRUE(reg.set<bool>("r.vsync", false));
    EXPECT_FALSE(reg.get<bool>("r.vsync"));
}

TEST(CVarRegistry, RegisterInt_TypeSafeGetSet) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.shadow_res", "Shadow resolution",
                                    int32_t{2048});

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type, CVarType::INT);
    EXPECT_EQ(reg.get<int32_t>("r.shadow_res"), 2048);

    EXPECT_TRUE(reg.set<int32_t>("r.shadow_res", 4096));
    EXPECT_EQ(reg.get<int32_t>("r.shadow_res"), 4096);
}

TEST(CVarRegistry, RegisterString_TypeSafeGetSet) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("sys.map", "Current map",
                                    std::string{"de_dust2"});

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type, CVarType::STRING);
    EXPECT_EQ(reg.get<std::string>("sys.map"), "de_dust2");

    EXPECT_TRUE(reg.set<std::string>("sys.map", std::string{"de_inferno"}));
    EXPECT_EQ(reg.get<std::string>("sys.map"), "de_inferno");
}

// ============================================================================
// Duplicate registration
// ============================================================================

TEST(CVarRegistry, RegisterDuplicate_ReturnsNullptr) {
    CVarRegistry reg;
    auto* first = reg.register_cvar("r.fov", "Field of view", 90.0f);
    auto* second = reg.register_cvar("r.fov", "Duplicate!", 120.0f);

    EXPECT_NE(first, nullptr);
    EXPECT_EQ(second, nullptr);

    // Original value is preserved.
    EXPECT_FLOAT_EQ(reg.get<float>("r.fov"), 90.0f);
}

// ============================================================================
// Set on nonexistent CVar returns false
// ============================================================================

TEST(CVarRegistry, SetNonexistent_ReturnsFalse) {
    CVarRegistry reg;

    bool ok = reg.set<float>("ghost", 1.0f);

    EXPECT_FALSE(ok);
}

// ============================================================================
// find_by_name on existing CVar returns correct entry
// ============================================================================

TEST(CVarRegistry, FindByName_ExistingReturnsEntry) {
    CVarRegistry reg;
    reg.register_cvar("r.bloom", "Bloom intensity", 0.5f);

    const CVarEntry* found = reg.find_by_name("r.bloom");

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "r.bloom");
    EXPECT_EQ(found->type, CVarType::FLOAT);
    EXPECT_FLOAT_EQ(std::get<float>(found->current_value), 0.5f);
}

TEST(CVarRegistry, FindByNameMut_ExistingReturnsEntry) {
    CVarRegistry reg;
    reg.register_cvar("r.bloom", "Bloom intensity", 0.5f);

    CVarEntry* found = reg.find_by_name_mut("r.bloom");

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "r.bloom");
}

// ============================================================================
// Flags bitmask combinations
// ============================================================================

TEST(CVarRegistry, FlagsCombination_ReadonlyAndSaved) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar(
        "sys.id", "System ID", int32_t{1},
        CVAR_READONLY | CVAR_SAVED);

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->flags, CVAR_READONLY | CVAR_SAVED);

    // Cannot set because READONLY.
    EXPECT_FALSE(reg.set<int32_t>("sys.id", 2));
    EXPECT_EQ(reg.get<int32_t>("sys.id"), 1);
}

TEST(CVarRegistry, FlagsCheat) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("sv.cheats", "Enable cheats", false,
                                    CVAR_CHEAT);

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->flags & CVAR_CHEAT, CVAR_CHEAT);

    // CVAR_CHEAT alone does not block set() — cheat enforcement is a
    // higher-level policy, not a registry concern.
    EXPECT_TRUE(reg.set<bool>("sv.cheats", true));
    EXPECT_TRUE(reg.get<bool>("sv.cheats"));
}

// ============================================================================
// CVar<T> typed handle
// ============================================================================

TEST(CVarHandle, GetReturnsCurrentValue) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.gamma", "Gamma", 2.2f);
    CVar<float> cv(entry);

    EXPECT_FLOAT_EQ(cv.get(), 2.2f);
}

TEST(CVarHandle, SetUpdatesValue) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.gamma", "Gamma", 2.2f);
    CVar<float> cv(entry);

    EXPECT_TRUE(cv.set(1.8f));
    EXPECT_FLOAT_EQ(cv.get(), 1.8f);

    // Also visible through the registry.
    EXPECT_FLOAT_EQ(reg.get<float>("r.gamma"), 1.8f);
}

TEST(CVarHandle, SetReadonly_ReturnsFalse) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("sys.build", "Build number",
                                    int32_t{100}, CVAR_READONLY);
    CVar<int32_t> cv(entry);

    EXPECT_FALSE(cv.set(200));
    EXPECT_EQ(cv.get(), 100);
}

TEST(CVarHandle, EntryAccessor) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.fov", "FOV", 90.0f);
    CVar<float> cv(entry);

    EXPECT_EQ(cv.entry(), entry);
    EXPECT_EQ(cv.entry()->name, "r.fov");
}

TEST(CVarHandle, DefaultConstructed_IsInvalid) {
    CVar<float> cv;

    EXPECT_FALSE(cv.is_valid());
    EXPECT_EQ(cv.entry(), nullptr);
}

TEST(CVarHandle, ConstructedWithEntry_IsValid) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.fov", "FOV", 90.0f);
    CVar<float> cv(entry);

    EXPECT_TRUE(cv.is_valid());
}

TEST(CVarHandle, BoolHandle) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("r.hdr", "HDR", true);
    CVar<bool> cv(entry);

    EXPECT_TRUE(cv.get());
    EXPECT_TRUE(cv.set(false));
    EXPECT_FALSE(cv.get());
}

TEST(CVarHandle, StringHandle) {
    CVarRegistry reg;
    auto* entry = reg.register_cvar("sys.name", "Name",
                                    std::string{"UzEngine"});
    CVar<std::string> cv(entry);

    EXPECT_EQ(cv.get(), "UzEngine");
    EXPECT_TRUE(cv.set(std::string{"TestEngine"}));
    EXPECT_EQ(cv.get(), "TestEngine");
}

// ============================================================================
// GetAll on empty registry
// ============================================================================

TEST(CVarRegistry, GetAll_EmptyReturnsEmpty) {
    CVarRegistry reg;

    auto all = reg.get_all();

    EXPECT_TRUE(all.empty());
}

// ============================================================================
// Default value preserved after set
// ============================================================================

TEST(CVarRegistry, DefaultValuePreservedAfterSet) {
    CVarRegistry reg;
    reg.register_cvar("r.fov", "FOV", 90.0f);

    reg.set<float>("r.fov", 120.0f);

    const auto* entry = reg.find_by_name("r.fov");
    ASSERT_NE(entry, nullptr);
    EXPECT_FLOAT_EQ(std::get<float>(entry->default_value), 90.0f);
    EXPECT_FLOAT_EQ(std::get<float>(entry->current_value), 120.0f);
}

// ============================================================================
// CVarFlags enum values
// ============================================================================

TEST(CVarFlags, EnumValues) {
    EXPECT_EQ(CVAR_NONE, 0u);
    EXPECT_EQ(CVAR_READONLY, 1u);
    EXPECT_EQ(CVAR_CHEAT, 2u);
    EXPECT_EQ(CVAR_SAVED, 4u);
}

// ============================================================================
// CVarType enum coverage
// ============================================================================

TEST(CVarType, AllTypesRegistered) {
    CVarRegistry reg;
    reg.register_cvar("t.bool", "b", false);
    reg.register_cvar("t.int", "i", int32_t{0});
    reg.register_cvar("t.float", "f", 0.0f);
    reg.register_cvar("t.string", "s", std::string{""});

    EXPECT_EQ(reg.find_by_name("t.bool")->type, CVarType::BOOL);
    EXPECT_EQ(reg.find_by_name("t.int")->type, CVarType::INT);
    EXPECT_EQ(reg.find_by_name("t.float")->type, CVarType::FLOAT);
    EXPECT_EQ(reg.find_by_name("t.string")->type, CVarType::STRING);
}
