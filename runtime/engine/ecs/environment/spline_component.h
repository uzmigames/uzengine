/// UzEngine SplineComponent — ECS data for authored Catmull-Rom paths.
///
/// Stores the control points of a centripetal Catmull-Rom spline in
/// object-local space (centimeters). The scene VFX sync (`SyncVfxEmitters`)
/// transforms these points by the entity's `SceneComponent` world matrix
/// each frame and copies the result into a target emitter's
/// `splineSource`. The VFX runtime then bakes a `VfxSplineBuffer` GPU
/// upload from that source so the spawn/update compute shaders can sample
/// the curve via the bindless data interface.
///
/// # Pairing with VfxEmitterComponent
/// To drive an emitter from this component, set the emitter's
/// `splineTargetEntity` field to the entity that owns this
/// `SplineComponent`. The sync ignores the link unless both the entity is
/// valid AND it owns a `SplineComponent`.
///
/// # Layout — mirrors `VfxSpline`
/// The fields here are the authoring subset of `VfxSpline` (the runtime
/// CPU evaluator). The sync copies them verbatim — `controlPoints`,
/// `tension`, `looping`, and the arc-length sample count — so the
/// downstream evaluator sees byte-for-byte the same data the user
/// authored, with zero translation logic.
///
/// # Usage
/// ```cpp
/// auto pathEntity = world.CreateEntity();
/// world.AddComponent<SceneComponent>(pathEntity);  // world transform
/// auto& path = world.AddComponent<SplineComponent>(pathEntity);
/// path.controlPoints = { {0,0,0}, {100,0,50}, {200,0,0}, {300,0,50} };
/// path.tension = 0.5f;        // Niagara-default centripetal
/// path.looping = false;
///
/// auto& emitter = world.GetComponent<VfxEmitterComponent>(emitterEntity);
/// emitter.splineTargetEntity = pathEntity;  // wire the spline -> emitter link
/// ```
///
/// @ref UE5 USplineComponent — authoring-side spline reference
/// @ref engine/vfx/data_interfaces/vfx_spline_di.h `VfxSpline`
/// @ref Catmull & Rom 1974 "A class of local interpolating splines"

#pragma once

#include "vector3.h"

#include <cstdint>
#include <vector>

namespace uz {

/// Authoring-side Catmull-Rom spline component.
///
/// Owns the control-point list and evaluator parameters consumed by the
/// VFX spline data interface. A valid spline needs at least 2 control
/// points; with fewer points the VFX sync still copies the (empty) data
/// but the simulation shaders short-circuit on `cbSplinePointCount < 2`.
/// All fields are plain data — no virtuals, no constructors with side
/// effects — so the component travels through `entt` as a POD.
struct SplineComponent {
    /// Authored control points in object-local space (centimeters).
    /// Transformed by the owning entity's `SceneComponent::GetWorldMatrix()`
    /// each frame inside `SyncVfxEmitters` before being copied to the
    /// runtime emitter's `splineSource.controlPoints`.
    ///
    /// Minimum 2 points required for the spline to evaluate; the VFX
    /// runtime gates on `pointCount >= 2` before consuming the SRV.
    std::vector<Vec3> controlPoints;

    /// Catmull-Rom tension. Mirrors `VfxSpline::tension`. 0.5 is the
    /// Niagara default (centripetal); 0 is loose uniform Catmull-Rom;
    /// 1 is chordal (tight corners). Range typically [0, 1].
    /// @ref Yuksel et al. 2011 "Parameterization and Applications of
    ///      Catmull-Rom Curves"
    float tension = 0.5f;

    /// When true, the spline wraps — the last control point connects
    /// back to the first. Mirrors `VfxSpline::looping`. Useful for
    /// orbital paths and continuous loops.
    bool looping = false;

    /// Resolution of the arc-length lookup table baked by
    /// `VfxSpline::BuildArcLengthTable`. Higher values = smoother
    /// uniform-speed sampling at the cost of CPU build time and GPU
    /// upload bytes (`32 * resolution`).
    ///
    /// Mirrors `VfxSpline::arcLengthSamples`. Defaults to 64 to match
    /// `VfxSpline`'s own default exactly — the runtime can override this
    /// per-emitter via `kVfxSplineDefaultResolution` if needed.
    /// @ref engine/vfx/data_interfaces/vfx_spline_buffer.h
    ///      `kVfxSplineDefaultResolution`
    uint32_t arcLengthSamples = 64;
};

} // namespace uz
