/// Unit tests for ModuleRegistry -- VFX module factory registry.
///
/// Covers: registration + creation of a known module, creation of an unknown
/// name, duplicate registration overwrite, IsRegistered queries, and
/// GetRegisteredNames completeness. No GPU hardware required.

#include <gtest/gtest.h>

#include "modules/module_registry.h"
#include "modules/spawn_module.h"
#include "modules/update_module.h"
#include "modules/decay_module.h"
#include "modules/turbulence_module.h"
#include "modules/constraint_module.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Register and create a module by name
// ─────────────────────────────────────────────────────────────────────────────

/// Register SpawnModule under "spawn". Create by name. Verify the returned
/// pointer is non-null and GetName() matches the registered identifier.
TEST(ModuleRegistry, RegisterAndCreate_KnownName_ReturnsCorrectModule) {
    ModuleRegistry registry;
    registry.Register<SpawnModule>("spawn");

    auto module = registry.Create("spawn");

    ASSERT_NE(module, nullptr)
        << "Create must return a non-null pointer for a registered module";
    EXPECT_STREQ(module->GetName(), "spawn")
        << "Created module GetName() must match registered identifier";
}

/// Register UpdateModule and verify the same pattern.
TEST(ModuleRegistry, RegisterAndCreate_UpdateModule_ReturnsCorrectModule) {
    ModuleRegistry registry;
    registry.Register<UpdateModule>("update");

    auto module = registry.Create("update");

    ASSERT_NE(module, nullptr);
    EXPECT_STREQ(module->GetName(), "update");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Create with unknown name returns nullptr
// ─────────────────────────────────────────────────────────────────────────────

/// Calling Create() for a name that was never registered must return nullptr
/// without crashing.
TEST(ModuleRegistry, CreateUnknown_UnregisteredName_ReturnsNullptr) {
    ModuleRegistry registry;
    registry.Register<SpawnModule>("spawn");

    auto module = registry.Create("nonexistent_module");

    EXPECT_EQ(module, nullptr)
        << "Create must return nullptr for an unregistered module name";
}

/// Empty string also must return nullptr.
TEST(ModuleRegistry, CreateUnknown_EmptyName_ReturnsNullptr) {
    ModuleRegistry registry;

    auto module = registry.Create("");

    EXPECT_EQ(module, nullptr)
        << "Create with empty name must return nullptr";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Duplicate registration -- second registration overwrites first
// ─────────────────────────────────────────────────────────────────────────────

/// Registering a different concrete type under the same name must not crash.
/// The second registration overwrites the first; Create() must then return
/// an instance of the second type.
TEST(ModuleRegistry, DuplicateRegistration_SameName_SecondOverwritesFirst) {
    ModuleRegistry registry;

    // First registration: SpawnModule under "test_module".
    registry.Register<SpawnModule>("test_module");
    {
        auto first = registry.Create("test_module");
        ASSERT_NE(first, nullptr);
        EXPECT_STREQ(first->GetName(), "spawn")
            << "Before overwrite, must create SpawnModule";
    }

    // Second registration: DecayModule under same name. Must not crash.
    registry.Register<DecayModule>("test_module");
    {
        auto second = registry.Create("test_module");
        ASSERT_NE(second, nullptr)
            << "After duplicate registration, Create must still succeed";
        EXPECT_STREQ(second->GetName(), "decay")
            << "After overwrite, Create must return DecayModule";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: IsRegistered
// ─────────────────────────────────────────────────────────────────────────────

/// IsRegistered must return true for a registered name and false for any
/// unregistered name, including edge cases (empty string, similar-but-not-equal).
TEST(ModuleRegistry, IsRegistered_AfterRegister_ReturnsTrueForKnownName) {
    ModuleRegistry registry;
    registry.Register<TurbulenceModule>("turbulence");

    EXPECT_TRUE(registry.IsRegistered("turbulence"))
        << "IsRegistered must return true immediately after Register";
    EXPECT_FALSE(registry.IsRegistered("Turbulence"))
        << "IsRegistered must be case-sensitive ('Turbulence' != 'turbulence')";
    EXPECT_FALSE(registry.IsRegistered(""))
        << "IsRegistered must return false for empty string";
    EXPECT_FALSE(registry.IsRegistered("spawn"))
        << "IsRegistered must return false for a different, unregistered name";
}

/// IsRegistered on an empty registry must always return false.
TEST(ModuleRegistry, IsRegistered_EmptyRegistry_AlwaysFalse) {
    ModuleRegistry registry;

    EXPECT_FALSE(registry.IsRegistered("spawn"))
        << "Empty registry must return false for any name";
    EXPECT_FALSE(registry.IsRegistered("decay"))
        << "Empty registry must return false for any name";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: GetRegisteredNames returns all registered modules
// ─────────────────────────────────────────────────────────────────────────────

/// Register 3 distinct modules. GetRegisteredNames() must return a vector
/// containing all 3 names (order unspecified).
TEST(ModuleRegistry, GetRegisteredNames_ThreeModules_ContainsAllThree) {
    ModuleRegistry registry;
    registry.Register<SpawnModule>("spawn");
    registry.Register<UpdateModule>("update");
    registry.Register<ConstraintModule>("constraint");

    const auto names = registry.GetRegisteredNames();

    EXPECT_EQ(names.size(), 3u)
        << "GetRegisteredNames must return exactly 3 entries for 3 registered modules";

    const auto containsName = [&](const std::string& n) {
        return std::find(names.begin(), names.end(), n) != names.end();
    };

    EXPECT_TRUE(containsName("spawn"))
        << "GetRegisteredNames must contain 'spawn'";
    EXPECT_TRUE(containsName("update"))
        << "GetRegisteredNames must contain 'update'";
    EXPECT_TRUE(containsName("constraint"))
        << "GetRegisteredNames must contain 'constraint'";
}

/// GetRegisteredNames on an empty registry must return an empty vector.
TEST(ModuleRegistry, GetRegisteredNames_EmptyRegistry_ReturnsEmptyVector) {
    ModuleRegistry registry;

    const auto names = registry.GetRegisteredNames();

    EXPECT_TRUE(names.empty())
        << "GetRegisteredNames on empty registry must return an empty vector";
}

/// After a duplicate registration the count must not increase (overwrite, not append).
TEST(ModuleRegistry, GetRegisteredNames_DuplicateRegister_CountUnchanged) {
    ModuleRegistry registry;
    registry.Register<DecayModule>("decay");
    registry.Register<DecayModule>("decay"); // duplicate

    const auto names = registry.GetRegisteredNames();

    EXPECT_EQ(names.size(), 1u)
        << "Duplicate registration must not increase the registered-names count";
}
