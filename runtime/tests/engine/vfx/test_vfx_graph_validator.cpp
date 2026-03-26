/// Unit tests for VFXGraph validator (ValidateVFXGraph).
///
/// Covers: valid graph passes, no modules fails, duplicate parameter names fail,
/// duplicate event names fail, event emission_count==0 fails, event
/// velocity_scale<=0 fails, emitter max_particles==0 fails, emitter
/// lifespan_default<=0 fails, invalid parameter types (Int, Bool, Curve) fail,
/// and that the valid flag and error list are consistent.
///
/// All tests build minimal VFXGraph objects via LoadFromString to avoid
/// duplicating construction logic. No GPU hardware required.

#include <gtest/gtest.h>

#include "asset/vfx_graph.h"
#include "asset/vfx_graph_validator.h"
#include "modules/module_registry.h"
#include "modules/register_builtin_modules.h"

#include <string>

using namespace uz;

namespace {

ModuleRegistry MakeRegistry() {
    ModuleRegistry reg;
    RegisterBuiltinModules(reg);
    return reg;
}

/// Loads a graph and asserts it is non-null (helper to reduce boilerplate).
std::unique_ptr<VFXGraph> LoadGraph(const std::string& json,
                                    const ModuleRegistry& reg) {
    auto graph = VFXGraph::LoadFromString(json, reg);
    return graph;
}

} // anonymous namespace

// =============================================================================
// Valid graph — baseline passing case
// =============================================================================

/// A properly configured graph with one module, one valid parameter, one event,
/// and valid emitter config passes all validation checks.
TEST(VFXGraphValidator, ValidGraph_PassesAllChecks) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "speed", "type": "float", "default": 1.0, "min": 0.0, "max": 5.0 }
      ],
      "events": [
        { "name": "burst", "emission_count": 100, "velocity_scale": 1.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_TRUE(result.valid)       << "Valid graph must pass validation";
    EXPECT_TRUE(result.errors.empty()) << "No errors expected for valid graph";
}

// =============================================================================
// No modules — required to have at least one
// =============================================================================

/// A graph with an empty modules array fails validation with an error about modules.
TEST(VFXGraphValidator, NoModules_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": []
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty())
        << "At least one error must be reported when there are no modules";
    // Confirm the error message mentions modules
    bool foundModuleError = false;
    for (const auto& err : result.errors) {
        if (err.find("module") != std::string::npos) {
            foundModuleError = true;
            break;
        }
    }
    EXPECT_TRUE(foundModuleError)
        << "Error message must mention 'module' for empty modules list";
}

// =============================================================================
// Duplicate parameter names
// =============================================================================

/// Two parameters with the same name produce a validation error.
TEST(VFXGraphValidator, DuplicateParameterNames_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "speed", "type": "float", "default": 1.0 },
        { "name": "speed", "type": "float", "default": 2.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundDupError = false;
    for (const auto& err : result.errors) {
        if (err.find("speed") != std::string::npos) {
            foundDupError = true;
            break;
        }
    }
    EXPECT_TRUE(foundDupError)
        << "Error must reference the duplicate parameter name 'speed'";
}

/// Three parameters where two share a name — only one duplicate error reported.
TEST(VFXGraphValidator, DuplicateParameterNames_TwoDifferentDuplicates_BothReported) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "alpha", "type": "float", "default": 1.0 },
        { "name": "beta",  "type": "float", "default": 1.0 },
        { "name": "alpha", "type": "float", "default": 2.0 },
        { "name": "beta",  "type": "float", "default": 3.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    // Should have at least 2 errors (one for each duplicate)
    EXPECT_GE(result.errors.size(), 2u);
}

// =============================================================================
// Duplicate event names
// =============================================================================

/// Two events with the same name produce a validation error.
TEST(VFXGraphValidator, DuplicateEventNames_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "events": [
        { "name": "burst", "emission_count": 100, "velocity_scale": 1.0 },
        { "name": "burst", "emission_count": 200, "velocity_scale": 2.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundDupError = false;
    for (const auto& err : result.errors) {
        if (err.find("burst") != std::string::npos) {
            foundDupError = true;
            break;
        }
    }
    EXPECT_TRUE(foundDupError)
        << "Error must reference the duplicate event name 'burst'";
}

// =============================================================================
// Event emission_count == 0
// =============================================================================

/// An event with emission_count set to 0 fails validation.
TEST(VFXGraphValidator, EventEmissionCountZero_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "events": [
        { "name": "impact", "emission_count": 0, "velocity_scale": 1.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundError = false;
    for (const auto& err : result.errors) {
        if (err.find("impact") != std::string::npos &&
            err.find("emission_count") != std::string::npos) {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError)
        << "Error must reference the event name 'impact' and 'emission_count'";
}

// =============================================================================
// Event velocity_scale <= 0
// =============================================================================

/// An event with velocity_scale of exactly 0 fails validation.
TEST(VFXGraphValidator, EventVelocityScaleZero_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "events": [
        { "name": "pop", "emission_count": 50, "velocity_scale": 0.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundError = false;
    for (const auto& err : result.errors) {
        if (err.find("pop") != std::string::npos &&
            err.find("velocity_scale") != std::string::npos) {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError)
        << "Error must reference event 'pop' and 'velocity_scale'";
}

/// An event with velocity_scale of -1.0 (strictly negative) also fails.
TEST(VFXGraphValidator, EventVelocityScaleNegative_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "events": [
        { "name": "reverse", "emission_count": 10, "velocity_scale": -1.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

// =============================================================================
// Emitter max_particles == 0
// =============================================================================

/// Emitter max_particles of 0 fails validation.
TEST(VFXGraphValidator, EmitterMaxParticlesZero_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 0, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundError = false;
    for (const auto& err : result.errors) {
        if (err.find("max_particles") != std::string::npos) {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError)
        << "Error must mention 'max_particles'";
}

// =============================================================================
// Emitter lifespan_default <= 0
// =============================================================================

/// Emitter lifespan_default of 0.0 fails validation.
TEST(VFXGraphValidator, EmitterLifespanDefaultZero_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 0.0 },
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundError = false;
    for (const auto& err : result.errors) {
        if (err.find("lifespan_default") != std::string::npos) {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError)
        << "Error must mention 'lifespan_default'";
}

/// Emitter lifespan_default of -1.0 (strictly negative) also fails.
TEST(VFXGraphValidator, EmitterLifespanDefaultNegative_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": -1.0 },
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

// =============================================================================
// Invalid parameter types — Int, Bool, Curve are not valid graph-level types
// =============================================================================

/// Parameter with type "int" produces a type validation error.
TEST(VFXGraphValidator, ParameterTypeInt_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "count", "type": "int", "default": 0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    // Note: the loader silently maps unknown/unsupported types to Float.
    // The validator enforces that Int-typed params are invalid.
    // Internally the loader maps "int" to ParameterType::Int via ParseParameterType.
    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundError = false;
    for (const auto& err : result.errors) {
        if (err.find("count") != std::string::npos) {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError)
        << "Error must reference the parameter name 'count'";
}

/// Parameter with type "bool" produces a type validation error.
TEST(VFXGraphValidator, ParameterTypeBool_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "loop_enabled", "type": "bool", "default": 0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundError = false;
    for (const auto& err : result.errors) {
        if (err.find("loop_enabled") != std::string::npos) {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError);
}

/// Parameter with type "curve" produces a type validation error.
TEST(VFXGraphValidator, ParameterTypeCurve_FailsValidation) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 1000, "lifespan_default": 2.0 },
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "opacity_curve", "type": "curve", "default": 0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    ASSERT_FALSE(result.errors.empty());
    bool foundError = false;
    for (const auto& err : result.errors) {
        if (err.find("opacity_curve") != std::string::npos) {
            foundError = true;
            break;
        }
    }
    EXPECT_TRUE(foundError);
}

// =============================================================================
// Multiple simultaneous errors — valid flag reflects all failures
// =============================================================================

/// A graph with no modules, zero max_particles, and zero lifespan accumulates
/// multiple errors; valid must be false and error count must be >= 3.
TEST(VFXGraphValidator, MultipleErrors_AllAccumulated) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 0, "lifespan_default": 0.0 },
      "modules": []
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    EXPECT_FALSE(result.valid);
    EXPECT_GE(result.errors.size(), 3u)
        << "Expected errors for: no modules, max_particles==0, lifespan_default==0";
}

// =============================================================================
// valid flag is consistent with errors list
// =============================================================================

/// When valid==true, errors must be empty; when valid==false, errors must be non-empty.
TEST(VFXGraphValidator, ValidFlagConsistentWithErrorsList_PassingGraph) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "emitter": { "max_particles": 100, "lifespan_default": 1.0 },
      "modules": [ { "type": "decay" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = LoadGraph(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto result = ValidateVFXGraph(*graph);

    // Consistency: valid XOR has_errors must be satisfied
    if (result.valid) {
        EXPECT_TRUE(result.errors.empty())
            << "valid==true requires errors to be empty";
    } else {
        EXPECT_FALSE(result.errors.empty())
            << "valid==false requires at least one error";
    }
}
