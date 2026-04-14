/// Unit tests for ViewportToolbar and ViewportToolbarState.
///
/// Coverage targets (Sessions 4-6 of phase0_editor-viewport-toolbar):
///   - viewport_toolbar_state.h  → ViewportToolbarState defaults, ToolbarProfile enum
///   - viewport_toolbar.h/.cpp   → RebuildLayout, SyncActiveStates, SetProfile
///
/// No GPU, audio, or platform hardware required — all tests run against
/// pure CPU logic. The toolbar Init() / Render() paths are not tested here
/// because they require a live RHI device.

#include "viewport_toolbar_state.h"
#include "viewport_toolbar.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// ViewportToolbarState — defaults
// ─────────────────────────────────────────────────────────────────────────────

/// Default transform mode is Translate.
TEST(ViewportToolbarState, DefaultTransformMode_IsTranslate) {
    ViewportToolbarState state;
    EXPECT_EQ(state.transformMode, GizmoMode::Translate)
        << "Default transform mode must be Translate";
}

/// Default gizmo space is World.
TEST(ViewportToolbarState, DefaultGizmoSpace_IsWorld) {
    ViewportToolbarState state;
    EXPECT_EQ(state.gizmoSpace, GizmoSpace::World)
        << "Default gizmo space must be World";
}

/// Default debug view mode is Lit.
TEST(ViewportToolbarState, DefaultViewMode_IsLit) {
    ViewportToolbarState state;
    EXPECT_EQ(state.viewMode, DebugViewMode::Lit)
        << "Default debug view mode must be Lit";
}

/// Default quality level is 3 (Ultra).
TEST(ViewportToolbarState, DefaultQualityLevel_IsUltra) {
    ViewportToolbarState state;
    EXPECT_EQ(state.qualityLevel, 3u)
        << "Default quality level must be 3 (Ultra)";
}

/// Default profile is Full.
TEST(ViewportToolbarState, DefaultProfile_IsFull) {
    ViewportToolbarState state;
    EXPECT_EQ(state.profile, ToolbarProfile::Full)
        << "Default toolbar profile must be Full";
}

/// Default showFlags is non-zero (all enabled).
TEST(ViewportToolbarState, DefaultShowFlags_NonZero) {
    ViewportToolbarState state;
    EXPECT_NE(state.showFlags, 0u)
        << "Default show flags must be non-zero (all flags enabled)";
}

/// ShowFlags default equals ShowFlags::AllEnabled().bits.
TEST(ViewportToolbarState, DefaultShowFlags_EqualAllEnabled) {
    ViewportToolbarState state;
    EXPECT_EQ(state.showFlags, ShowFlags::AllEnabled().bits)
        << "Default show flags must equal ShowFlags::AllEnabled()";
}

// ─────────────────────────────────────────────────────────────────────────────
// ToolbarProfile enum values
// ─────────────────────────────────────────────────────────────────────────────

/// ToolbarProfile has 4 distinct values.
TEST(ToolbarProfile, FourDistinctValues) {
    EXPECT_NE(ToolbarProfile::Full,            ToolbarProfile::MaterialPreview);
    EXPECT_NE(ToolbarProfile::Full,            ToolbarProfile::MeshPreview);
    EXPECT_NE(ToolbarProfile::Full,            ToolbarProfile::Minimal);
    EXPECT_NE(ToolbarProfile::MaterialPreview, ToolbarProfile::MeshPreview);
    EXPECT_NE(ToolbarProfile::MaterialPreview, ToolbarProfile::Minimal);
    EXPECT_NE(ToolbarProfile::MeshPreview,     ToolbarProfile::Minimal);
}

// ─────────────────────────────────────────────────────────────────────────────
// ViewportToolbar — RebuildLayout / SetProfile (headless, no GPU)
// ─────────────────────────────────────────────────────────────────────────────

/// Full profile produces more buttons than Minimal profile.
TEST(ViewportToolbar, FullProfile_MoreButtonsThanMinimal) {
    ViewportToolbar toolbar;

    toolbar.SetProfile(ToolbarProfile::Full);
    toolbar.RebuildLayout();
    const std::size_t fullCount = toolbar.GetButtonCount();

    toolbar.SetProfile(ToolbarProfile::Minimal);
    toolbar.RebuildLayout();
    const std::size_t minimalCount = toolbar.GetButtonCount();

    EXPECT_GT(fullCount, minimalCount)
        << "Full profile must have more buttons than Minimal";
}

/// MeshPreview profile produces fewer buttons than Full.
TEST(ViewportToolbar, MeshPreviewProfile_FewerButtonsThanFull) {
    ViewportToolbar toolbar;

    toolbar.SetProfile(ToolbarProfile::Full);
    toolbar.RebuildLayout();
    const std::size_t fullCount = toolbar.GetButtonCount();

    toolbar.SetProfile(ToolbarProfile::MeshPreview);
    toolbar.RebuildLayout();
    const std::size_t meshCount = toolbar.GetButtonCount();

    EXPECT_LT(meshCount, fullCount)
        << "MeshPreview profile must have fewer buttons than Full";
}

/// MaterialPreview profile produces fewer buttons than Full.
TEST(ViewportToolbar, MaterialPreviewProfile_FewerButtonsThanFull) {
    ViewportToolbar toolbar;

    toolbar.SetProfile(ToolbarProfile::Full);
    toolbar.RebuildLayout();
    const std::size_t fullCount = toolbar.GetButtonCount();

    toolbar.SetProfile(ToolbarProfile::MaterialPreview);
    toolbar.RebuildLayout();
    const std::size_t matCount = toolbar.GetButtonCount();

    EXPECT_LT(matCount, fullCount)
        << "MaterialPreview profile must have fewer buttons than Full";
}

/// Minimal profile produces at least one button (view mode buttons always present).
TEST(ViewportToolbar, MinimalProfile_AtLeastOneButton) {
    ViewportToolbar toolbar;

    toolbar.SetProfile(ToolbarProfile::Minimal);
    toolbar.RebuildLayout();

    EXPECT_GE(toolbar.GetButtonCount(), 1u)
        << "Minimal profile must have at least one button";
}

/// RebuildLayout is idempotent — calling twice gives same count.
TEST(ViewportToolbar, RebuildLayout_Idempotent) {
    ViewportToolbar toolbar;
    toolbar.SetProfile(ToolbarProfile::Full);

    toolbar.RebuildLayout();
    const std::size_t first = toolbar.GetButtonCount();

    toolbar.RebuildLayout();
    const std::size_t second = toolbar.GetButtonCount();

    EXPECT_EQ(first, second)
        << "RebuildLayout must be idempotent";
}

/// SetProfile changes the stored profile in ViewportToolbarState.
TEST(ViewportToolbar, SetProfile_UpdatesState) {
    ViewportToolbar toolbar;
    toolbar.SetProfile(ToolbarProfile::MeshPreview);
    EXPECT_EQ(toolbar.GetState().profile, ToolbarProfile::MeshPreview)
        << "SetProfile must update the stored toolbar state";
}

/// Default profile after construction is Full.
TEST(ViewportToolbar, DefaultProfile_IsFull) {
    ViewportToolbar toolbar;
    EXPECT_EQ(toolbar.GetState().profile, ToolbarProfile::Full)
        << "Default toolbar profile must be Full";
}

/// IsInitialized returns false before Init() is called.
TEST(ViewportToolbar, IsInitialized_FalseBeforeInit) {
    ViewportToolbar toolbar;
    EXPECT_FALSE(toolbar.IsInitialized())
        << "IsInitialized() must return false before Init() is called";
}
