/// VFX Advanced Modules Demo -- headless showcase of collision, mesh rendering,
/// LOD evaluation, and significance-based budget enforcement.
///
/// Demonstrates four independent VFX subsystems without a GPU or window:
///
/// **Phase 1 -- Collision Module**
///   Constructs a `CollisionModule` for each of the three response types
///   (Kill, Bounce, Slide). Validates config fields via `GetConfig()`, verifies
///   `GetShaderCode()` returns non-empty HLSL, and exercises the
///   `CollisionResponseToString` / `CollisionResponseFromString` helpers.
///   Prints the response name, shader code length, and all config fields.
///
/// **Phase 2 -- Mesh Render Module**
///   Constructs a `MeshRenderModule` for each rotation mode (None,
///   VelocityAligned, Random). Confirms that shader snippets differ between
///   modes. Prints config summary and the first 120 characters of each snippet.
///
/// **Phase 3 -- LOD System**
///   Implemented in `phase_lod.cpp`. Moves a simulated camera from 0 m to
///   300 m away from a stationary emitter in 31 steps. Evaluates
///   `VFXLodEvaluator::Evaluate()` at each step and prints the table.
///
/// **Phase 4 -- Budget Stress Test**
///   Implemented in `phase_budget.cpp`. Creates 50 `VFXEmitterSignificance`
///   entries and runs `EnforceBudget()` with a 100K-particle cap across all
///   four quality levels.
///
/// # Separate TUs for Phase 3 and 4
/// `vfx_lod.h` and `vfx_significance_manager.h` both declare `VFXLodLevel`
/// inside `namespace uz`. Including both in the same translation unit would
/// produce a redefinition error. Phases 3 and 4 are therefore compiled as
/// separate translation units (phase_lod.cpp, phase_budget.cpp) that each
/// include only the relevant header subset.
///
/// # Build
///   cd demos/vfx_advanced
///   zig build
///   ./zig-out/bin/vfx_advanced_demo
///
/// # Expected Output
///   All four phases complete; final line:
///   "[PASS] VFX advanced modules demo completed successfully."

#include <algorithm>
#include <cstdint>
#include "engine_loop.h"
#include "log.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// VFX module includes (Phase 1 and 2 only)
// ─────────────────────────────────────────────────────────────────────────────

// NOTE: vfx_lod.h and vfx_significance_manager.h both define VFXLodLevel in
// namespace uz. They must NOT be included in the same translation unit.
// Phase 3 (LOD) lives in phase_lod.cpp; Phase 4 (budget) in phase_budget.cpp.

#include "engine/vfx/modules/collision_module.h"
#include "engine/vfx/modules/mesh_render_module.h"

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations for phase functions in separate TUs
// ─────────────────────────────────────────────────────────────────────────────

/// Runs the LOD distance + screen-coverage evaluation phase.
/// Implemented in phase_lod.cpp to isolate vfx_lod.h from phase_budget.cpp.
bool RunLodPhase();

/// Runs the significance manager + budget enforcement stress test.
/// Implemented in phase_budget.cpp to isolate vfx_significance_manager.h.
bool RunBudgetPhase();

// ─────────────────────────────────────────────────────────────────────────────
// Demo constants
// ─────────────────────────────────────────────────────────────────────────────

/// Fixed timestep used in demo output labels (matches 60 FPS convention).
static constexpr float kDt = 1.0f / 60.0f;
(void)kDt;  // suppresses unused-variable warning; retained for documentation.

// ─────────────────────────────────────────────────────────────────────────────
// Utility helpers (local to this TU)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Prints a horizontal section separator to stdout.
void PrintSeparator() {
    std::printf("──────────────────────────────────────────────────────────────\n");
}

/// Prints a major phase heading.
///
/// # Parameters
/// - `title`: Phase description to display between separator lines.
void PrintPhaseHeader(const char* title) {
    std::printf("\n");
    PrintSeparator();
    std::printf("  %s\n", title);
    PrintSeparator();
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1: Collision Module
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies CollisionModule construction, config fields, shader code, and
/// string conversion helpers for all three response types.
///
/// For each response type the function:
/// - Constructs a `CollisionModule` from an explicit `CollisionModuleConfig`.
/// - Calls `GetShaderCode()` and records its length.
/// - Verifies `GetName()` returns `"collision"`.
/// - Calls `CollisionResponseToString()` and `CollisionResponseFromString()`.
/// - Prints a summary row of all config fields.
///
/// # Returns
/// `true` if all three modules produce non-empty shader code and name/string
/// round-trips are correct; `false` on the first failure.
static bool RunCollisionPhase() {
    PrintPhaseHeader("Phase 1 -- Collision Module (Kill / Bounce / Slide)");

    // The three configurations exercise every branch in GetShaderCode().
    struct TestCase {
        const char*          label;
        uz::CollisionResponse response;
        float                bounceFactor;
        bool                 depthEnabled;
        float                collisionOffset;
        float                minBounceSpeed;
    };

    static constexpr TestCase kCases[] = {
        // label,     response,                               bounce,  depth,  offset,  minSpd
        { "Kill",   uz::CollisionResponse::Kill,   0.0f,  false, 0.005f, 0.0f  },
        { "Bounce", uz::CollisionResponse::Bounce, 0.6f,  true,  0.01f,  0.1f  },
        { "Slide",  uz::CollisionResponse::Slide,  0.0f,  true,  0.02f,  0.0f  },
    };

    bool allOk = true;

    std::printf("  %-8s  %-8s  %-7s  %-12s  %-8s  %-12s  %s\n",
                "Response", "Name", "bounce", "depthEnabled", "offset",
                "minBounceSpd", "ShaderLen");
    PrintSeparator();

    for (const auto& tc : kCases) {
        uz::CollisionModuleConfig cfg;
        cfg.response             = tc.response;
        cfg.bounceFactor         = tc.bounceFactor;
        cfg.depthBufferCollision = tc.depthEnabled;
        cfg.collisionOffset      = tc.collisionOffset;
        cfg.minBounceSpeed       = tc.minBounceSpeed;

        const uz::CollisionModule module{cfg};

        // Verify module name is stable.
        const char* moduleName = module.GetName();
        if (std::string(moduleName) != "collision") {
            std::fprintf(stderr,
                         "  [FAIL] GetName() returned '%s', expected 'collision'\n",
                         moduleName);
            allOk = false;
        }

        // Shader code must be non-empty for all response types.
        const std::string_view shaderCode = module.GetShaderCode();
        if (shaderCode.empty()) {
            std::fprintf(stderr,
                         "  [FAIL] %s: GetShaderCode() returned empty string\n",
                         tc.label);
            allOk = false;
        }

        // String round-trip: enum → string → enum must be identity.
        const char* responseStr = uz::CollisionResponseToString(tc.response);
        const uz::CollisionResponse roundTrip =
            uz::CollisionResponseFromString(responseStr);
        if (roundTrip != tc.response) {
            std::fprintf(stderr,
                         "  [FAIL] %s: string round-trip failed "
                         "(string='%s', parsed back to %d)\n",
                         tc.label, responseStr, static_cast<int>(roundTrip));
            allOk = false;
        }

        // Verify GetConfig() reflects the values we provided.
        const uz::CollisionModuleConfig& stored = module.GetConfig();
        if (stored.response             != tc.response       ||
            stored.bounceFactor         != tc.bounceFactor   ||
            stored.depthBufferCollision != tc.depthEnabled) {
            std::fprintf(stderr,
                         "  [FAIL] %s: GetConfig() fields do not match constructor\n",
                         tc.label);
            allOk = false;
        }

        std::printf("  %-8s  %-8s  %7.2f  %-12s  %8.3f  %12.3f  %zu\n",
                    tc.label,
                    moduleName,
                    static_cast<double>(cfg.bounceFactor),
                    cfg.depthBufferCollision ? "yes" : "no",
                    static_cast<double>(cfg.collisionOffset),
                    static_cast<double>(cfg.minBounceSpeed),
                    shaderCode.size());
    }

    PrintSeparator();

    // Verify an unrecognized string falls back gracefully (no crash).
    const uz::CollisionResponse fallback =
        uz::CollisionResponseFromString("invalid_response_xyz");
    std::printf("  Fallback for unrecognised string: %s\n",
                uz::CollisionResponseToString(fallback));

    // Verify the default constructor produces a Bounce response.
    const uz::CollisionModule defaultModule;
    if (defaultModule.GetConfig().response != uz::CollisionResponse::Bounce) {
        std::fprintf(stderr,
                     "  [FAIL] Default CollisionModule should have Bounce response\n");
        allOk = false;
    }
    std::printf("  Default CollisionModule response: %s  [%s]\n",
                uz::CollisionResponseToString(defaultModule.GetConfig().response),
                allOk ? "OK" : "FAIL");

    return allOk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2: Mesh Render Module
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies MeshRenderModule construction and HLSL snippet divergence across
/// the three rotation modes.
///
/// For each rotation mode (None, VelocityAligned, Random):
/// - Constructs a `MeshRenderModule` from an explicit `MeshRenderModuleConfig`.
/// - Calls `GetShaderCode()` and records the length and first 120 characters.
/// - Calls `GetName()` and verifies the stable identifier `"mesh_render"`.
/// - Calls `MeshRotationModeToString()` / `MeshRotationModeFromString()` for
///   round-trip validation.
///
/// After all three are collected, verifies that at least two of the three
/// snippets differ (mode-specific branching must be present in the HLSL).
///
/// # Returns
/// `true` if all modules produce non-empty snippets and at least two differ;
/// `false` on any failure.
static bool RunMeshRenderPhase() {
    PrintPhaseHeader("Phase 2 -- Mesh Render Module (None / VelocityAligned / Random)");

    struct TestCase {
        const char*          label;
        uz::MeshRotationMode rotationMode;
        const char*          meshPath;
        const char*          materialPath;
        uz::Vec3             scale;
        bool                 castShadow;
    };

    static const TestCase kCases[] = {
        {
            "None",
            uz::MeshRotationMode::None,
            "meshes/debris/rock_chunk.uzmesh",
            "materials/vfx/debris_opaque.uzmat",
            uz::Vec3{1.0f, 1.0f, 1.0f},
            false,
        },
        {
            "VelocityAligned",
            uz::MeshRotationMode::VelocityAligned,
            "meshes/projectiles/shell_casing.uzmesh",
            "materials/vfx/metal_debris.uzmat",
            uz::Vec3{0.5f, 0.5f, 0.5f},
            false,
        },
        {
            "Random",
            uz::MeshRotationMode::Random,
            "meshes/foliage/leaf_card.uzmesh",
            "materials/vfx/leaf_translucent.uzmat",
            uz::Vec3{2.0f, 2.0f, 2.0f},
            true,
        },
    };

    bool allOk = true;

    // Collect shader code strings for the post-loop divergence check.
    std::vector<std::string> shaderSnippets;
    shaderSnippets.reserve(std::size(kCases));

    for (const auto& tc : kCases) {
        uz::MeshRenderModuleConfig cfg;
        cfg.rotationMode = tc.rotationMode;
        cfg.meshPath     = tc.meshPath;
        cfg.materialPath = tc.materialPath;
        cfg.meshScale    = tc.scale;
        cfg.castShadow   = tc.castShadow;

        const uz::MeshRenderModule module{cfg};

        // Name stability check.
        const char* moduleName = module.GetName();
        if (std::string(moduleName) != "mesh_render") {
            std::fprintf(stderr,
                         "  [FAIL] %s: GetName() returned '%s', expected 'mesh_render'\n",
                         tc.label, moduleName);
            allOk = false;
        }

        // Shader code must be non-empty.
        const std::string_view shaderCode = module.GetShaderCode();
        if (shaderCode.empty()) {
            std::fprintf(stderr,
                         "  [FAIL] %s: GetShaderCode() returned empty string\n",
                         tc.label);
            allOk = false;
        }
        shaderSnippets.emplace_back(shaderCode);

        // String round-trip for rotation mode.
        const char* modeStr = uz::MeshRotationModeToString(tc.rotationMode);
        const uz::MeshRotationMode roundTrip =
            uz::MeshRotationModeFromString(modeStr);
        if (roundTrip != tc.rotationMode) {
            std::fprintf(stderr,
                         "  [FAIL] %s: rotation mode round-trip failed "
                         "(string='%s', parsed=%d)\n",
                         tc.label, modeStr, static_cast<int>(roundTrip));
            allOk = false;
        }

        // Config accessor reflects constructor values.
        const uz::MeshRenderModuleConfig& stored = module.GetConfig();
        if (stored.rotationMode != tc.rotationMode ||
            stored.castShadow   != tc.castShadow   ||
            stored.meshPath     != tc.meshPath) {
            std::fprintf(stderr,
                         "  [FAIL] %s: GetConfig() fields do not match constructor\n",
                         tc.label);
            allOk = false;
        }

        // Print summary.
        std::printf("  [%s]\n", tc.label);
        std::printf("    mesh     : %s\n", cfg.meshPath.c_str());
        std::printf("    material : %s\n", cfg.materialPath.c_str());
        std::printf("    scale    : (%.1f, %.1f, %.1f)\n",
                    static_cast<double>(cfg.meshScale.x),
                    static_cast<double>(cfg.meshScale.y),
                    static_cast<double>(cfg.meshScale.z));
        std::printf("    shadow   : %s\n", cfg.castShadow ? "yes" : "no");
        std::printf("    rotation : %s  (round-trip: %s)\n", modeStr, modeStr);
        std::printf("    shader   : %zu bytes\n", shaderCode.size());

        // First 120 chars of the snippet as a sanity preview.
        const size_t previewLen = std::min(shaderCode.size(), size_t{120});
        std::printf("    preview  : \"%.*s%s\"\n",
                    static_cast<int>(previewLen),
                    shaderCode.data(),
                    (shaderCode.size() > 120) ? "..." : "");
        std::printf("\n");
    }

    // At least two of the three rotation-mode snippets must differ.
    bool anyDiffer = false;
    for (size_t i = 0; i < shaderSnippets.size() && !anyDiffer; ++i) {
        for (size_t j = i + 1; j < shaderSnippets.size(); ++j) {
            if (shaderSnippets[i] != shaderSnippets[j]) {
                anyDiffer = true;
                break;
            }
        }
    }

    if (!anyDiffer) {
        std::fprintf(stderr,
                     "  [FAIL] All three rotation modes produced identical HLSL -- "
                     "mode-specific branching is missing\n");
        allOk = false;
    } else {
        std::printf("  [OK]  Shader snippets differ across rotation modes\n");
    }

    // Default constructor should produce VelocityAligned.
    const uz::MeshRenderModule defaultModule;
    if (defaultModule.GetConfig().rotationMode != uz::MeshRotationMode::VelocityAligned) {
        std::fprintf(stderr,
                     "  [FAIL] Default MeshRenderModule should use VelocityAligned\n");
        allOk = false;
    }
    std::printf("  Default rotation mode: %s  [%s]\n",
                uz::MeshRotationModeToString(defaultModule.GetConfig().rotationMode),
                allOk ? "OK" : "FAIL");

    return allOk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main entry point
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Wraps the vfx_advanced demo as an EngineLoop subsystem.
class VfxAdvancedSubsystem final : public uz::ISubsystem {
public:
    [[nodiscard]] const char* GetName() const override { return "VfxAdvanced"; }
    [[nodiscard]] uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }
    [[nodiscard]] uz::TickGroup GetTickGroup() const override { return uz::TickGroup::None; }

    [[nodiscard]] bool Init() override {
std::printf("══════════════════════════════════════════════════════════════\n");
    std::printf("         UzEngine -- VFX Advanced Modules Demo\n");
    std::printf("  Collision · Mesh Render · LOD · Significance Budget\n");
    std::printf("══════════════════════════════════════════════════════════════\n");

    bool allPass = true;

    if (!RunCollisionPhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 1 (Collision Module) failed.\n");
        allPass = false;
    }

    if (!RunMeshRenderPhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 2 (Mesh Render Module) failed.\n");
        allPass = false;
    }

    // Phases 3 and 4 are implemented in separate TUs because vfx_lod.h and
    // vfx_significance_manager.h both declare VFXLodLevel in namespace uz.
    // Including both in a single TU would cause a redefinition error.
    if (!RunLodPhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 3 (LOD System) failed.\n");
        allPass = false;
    }

    if (!RunBudgetPhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 4 (Budget Stress Test) failed.\n");
        allPass = false;
    }

    std::printf("\n══════════════════════════════════════════════════════════════\n");
    if (allPass) {
        std::printf("[PASS] VFX advanced modules demo completed successfully.\n");
        return true;
    }

    std::fprintf(stderr, "[FAIL] One or more phases failed.\n");
    return 1;
    }

    void Shutdown() override {}
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::EngineLoop loop;
    loop.RegisterSubsystem(std::make_unique<VfxAdvancedSubsystem>());

    uz::EngineConfig config = uz::ParseCommandLine(argc, argv);
    config.maxFrames = 1;
    int result = loop.Run(config);

    uz::LogShutdown();
    return result;
}
