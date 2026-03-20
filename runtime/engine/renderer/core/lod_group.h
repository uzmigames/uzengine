/// UzEngine LOD Group — Level of Detail data types.
///
/// Defines the per-mesh LOD level array and per-object LOD selection state.
/// Each static mesh can have up to `kMaxLODs` (8) discrete LOD levels, each
/// with a screen size threshold that controls when the level is selected.
///
/// # Parity
/// @ref FStaticMeshRenderData::ScreenSize[MAX_STATIC_MESH_LODS]
/// @ref LOD0ScreenSize = 2.0f, AutoComputeLODPowerBase = 0.75f
/// @ref ComputeBoundsScreenSize
/// @ref ComputeStaticMeshLOD
///
/// # Thread Safety
/// These are plain data structs with no shared mutable state. They are
/// populated on the main/render thread and read during draw list construction.

#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of discrete LOD levels per mesh.
///
/// @ref MAX_STATIC_MESH_LODS = 8
/// UzEngine uses 8 to match reference, though most meshes only define 2-4 levels.
static constexpr uint32_t kMaxLODs = 8;

/// Default screen size for LOD 0 (full detail).
///
/// @ref LOD0ScreenSize = 2.0f
/// This is a diameter metric: ScreenMultiple * SphereRadius / Distance * 2.
static constexpr float kLOD0ScreenSize = 2.0f;

/// Power base for auto-computing LOD screen sizes when no explicit thresholds
/// are provided. LOD N gets screenSize = LOD0 * pow(base, N).
///
/// @ref AutoComputeLODPowerBase = 0.75f
static constexpr float kAutoComputeLODPowerBase = 0.75f;

// ─────────────────────────────────────────────────────────────────────────────
// LODLevel — a single discrete level of detail
// ─────────────────────────────────────────────────────────────────────────────

/// A single LOD level within a LODGroup.
///
/// Each level stores a mesh resource index and the minimum screen size
/// threshold at which this LOD should be selected. When the object's computed
/// screen size falls below the threshold, the next coarser LOD is used.
///
/// # Screen Size Metric
/// @ref ComputeBoundsScreenSize
/// Screen size is defined as the projected diameter of the bounding sphere:
///   screenSize = ScreenMultiple * SphereRadius / max(1, Distance) * 2
/// where ScreenMultiple = max(0.5 * ProjM[0][0], 0.5 * ProjM[1][1]).
/// This is NOT a [0,1] fraction — it can exceed 1.0 for objects that fill
/// more than half the screen. LOD 0 defaults to 2.0 (full screen diameter).
struct LODLevel {
    /// Index into the mesh resource table (same as DrawCommand::meshId).
    ///
    /// Each LOD level can reference a different GPU mesh with reduced
    /// triangle count. If only one mesh exists, all LOD levels share
    /// the same meshIndex.
    uint32_t meshIndex = 0;

    /// Screen size threshold for this LOD level.
    ///
    /// @ref FStaticMeshRenderData::ScreenSize
    /// The LOD is selected when the computed screen size is >= this threshold
    /// (after applying LOD bias scaling). Units match reference's projected diameter
    /// metric (LOD 0 default = 2.0, not normalised to [0,1]).
    float screenSizeThreshold = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// LODGroup — array of LOD levels for a single mesh asset
// ─────────────────────────────────────────────────────────────────────────────

/// Collection of LOD levels for a single static mesh.
///
/// LOD levels are sorted from highest detail (index 0, largest threshold)
/// to lowest detail (index numLevels-1, smallest threshold). This matches
/// the reference engine's convention where ScreenSize[0] > ScreenSize[1] > ... > ScreenSize[N].
///
/// @ref FStaticMeshRenderData
///
/// # Default State
/// A default-constructed LODGroup has 1 level (LOD 0 only) with meshIndex=0
/// and screenSizeThreshold=kLOD0ScreenSize. This means objects without
/// explicit LOD data always render at full detail — the LOD system is
/// transparent for single-LOD meshes.
struct LODGroup {
    /// Fixed-size array of LOD levels.
    ///
    /// Only the first `numLevels` entries are valid. Remaining entries
    /// are zero-initialized and must not be accessed.
    LODLevel levels[kMaxLODs] = {};

    /// Number of valid LOD levels (1 to kMaxLODs).
    ///
    /// At least 1 level is always present (LOD 0). Values of 0 are treated
    /// as 1 by the LOD selector.
    uint32_t numLevels = 1;

    /// Initializes a default single-LOD group.
    ///
    /// LOD 0 has meshIndex=0 and the reference default screen size of 2.0.
    LODGroup() noexcept {
        levels[0].meshIndex = 0;
        levels[0].screenSizeThreshold = kLOD0ScreenSize;
    }

    /// Initializes the LODGroup with auto-computed screen size thresholds.
    ///
    /// @ref pow(AutoComputeLODPowerBase, LODIndex)
    ///
    /// LOD 0 gets kLOD0ScreenSize (2.0). Subsequent levels get
    /// LOD0 * pow(0.75, N), matching the reference engine's auto-computation when
    /// no explicit screen sizes are authored.
    ///
    /// # Arguments
    /// * `meshIndices` — Array of mesh resource indices, one per LOD level.
    /// * `count` — Number of LOD levels (clamped to [1, kMaxLODs]).
    void InitAutoThresholds(const uint32_t* meshIndices, uint32_t count) noexcept {
        numLevels = count < 1 ? 1 : (count > kMaxLODs ? kMaxLODs : count);
        for (uint32_t i = 0; i < numLevels; ++i) {
            levels[i].meshIndex = meshIndices[i];
            if (i == 0) {
                levels[i].screenSizeThreshold = kLOD0ScreenSize;
            } else {
                // @ref implementation detail
                float threshold = kLOD0ScreenSize;
                for (uint32_t p = 0; p < i; ++p) {
                    threshold *= kAutoComputeLODPowerBase;
                }
                levels[i].screenSizeThreshold = threshold;
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// LODSelection — per-object per-frame LOD selection state
// ─────────────────────────────────────────────────────────────────────────────

/// Per-object LOD selection result, cached across frames.
///
/// Tracks the current and previous LOD indices to detect transitions,
/// and provides a crossfade alpha for dithered LOD transitions.
///
/// # Crossfade
/// @ref bUseDithered LOD transition
/// When the LOD changes between frames, `transitionFramesRemaining` is set
/// to `kDefaultTransitionFrames` and decremented each frame. During the
/// transition, both the old and new LOD meshes are rendered with
/// screen-door dithering based on `transitionAlpha`.
struct LODSelection {
    /// Currently selected LOD index (into LODGroup::levels).
    uint32_t currentLOD = 0;

    /// LOD index from the previous frame.
    ///
    /// When currentLOD != previousLOD, a transition is initiated.
    uint32_t previousLOD = 0;

    /// Crossfade blend factor [0, 1].
    ///
    /// 1.0 = fully transitioned to currentLOD (no crossfade active).
    /// Values < 1.0 indicate an active transition where both LODs
    /// should be rendered with dithered blending.
    float transitionAlpha = 1.0f;

    /// Number of frames remaining in the current LOD transition.
    ///
    /// 0 = no active transition. Decremented each frame during crossfade.
    uint8_t transitionFramesRemaining = 0;

    /// Default number of frames for a LOD crossfade transition.
    ///
    /// 8 frames at 60 FPS = ~133 ms transition, matching the reference engine's typical
    /// temporal dithered LOD transition duration.
    static constexpr uint8_t kDefaultTransitionFrames = 8;

    /// Updates the transition state for a new frame.
    ///
    /// If `newLOD` differs from `currentLOD`, initiates a new transition.
    /// If a transition is already in progress, advances it by one frame.
    ///
    /// # Arguments
    /// * `newLOD` — The LOD index selected for this frame.
    void Update(uint32_t newLOD) noexcept {
        if (newLOD != currentLOD) {
            // New transition: remember the outgoing LOD and start fading.
            previousLOD = currentLOD;
            currentLOD = newLOD;
            transitionFramesRemaining = kDefaultTransitionFrames;
            transitionAlpha = 0.0f;
        } else if (transitionFramesRemaining > 0) {
            // Advance existing transition.
            --transitionFramesRemaining;
            transitionAlpha = 1.0f - static_cast<float>(transitionFramesRemaining)
                              / static_cast<float>(kDefaultTransitionFrames);
        } else {
            // No transition in progress — fully settled.
            transitionAlpha = 1.0f;
        }
    }

    /// Returns true if a LOD crossfade transition is currently active.
    [[nodiscard]] bool IsTransitioning() const noexcept {
        return transitionFramesRemaining > 0;
    }
};

} // namespace uz
