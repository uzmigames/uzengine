<!-- doc-meta -->
<!-- analysis-type: UE5 architecture -->
<!-- focus: Transform Gizmo Interaction System -->
<!-- target: UzEngine gizmo implementation -->
<!-- coverage: hit-testing, drag behavior, coordinate spaces -->
<!-- version: 5.6 -->

# UE5 Transform Gizmo Interaction System — Complete Specification

Analysis of UE5 5.6 gizmo hit-testing, drag behavior, and interaction model from source code.

**Source Code Base**: `W:\UE_5.6_Source\Engine\Source\Runtime\InteractiveToolsFramework\`

## A. Hit Testing (Ray-Geometry Intersection)

### A.1 Hit Detection Pipeline

**Flow**: `IGizmoClickTarget::IsHit()` → `UGizmoElementHitTarget::IsHit()` → `UGizmoElementBase::LineTrace()`

**File**: `GizmoElementHitTargets.cpp:12-21`

```cpp
FInputRayHit UGizmoElementHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
    if (GizmoElement && GizmoViewContext && GizmoTransformProxy && (!Condition || Condition(ClickPos)))
    {
        UGizmoElementBase::FLineTraceTraversalState LineTraceState;
        LineTraceState.Initialize(GizmoViewContext, GizmoTransformProxy->GetTransform());
        return GizmoElement->LineTrace(GizmoViewContext, LineTraceState, ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction);
    }
    return FInputRayHit();
}
```

**Key Details**:
- Ray is passed as **world-space origin + direction**
- Hit testing respects **view-dependent culling** (angles < 3°)
- Each gizmo element implements its own `LineTrace()` method
- Returns `FInputRayHit` with hit depth parameter (distance along ray)

### A.2 Pixel Hit Distance Threshold (Hit Buffer)

**Default**: `7.0` pixels

**File**: `GizmoElementBase.h:428`

```cpp
float PixelHitDistanceThreshold = 7.0;
```

**Applied During Hit Testing**:
- Converted to **world space** via: `PixelHitThreshold_World = PixelToWorldScale * 7.0`
- Used to **expand hit geometry** (e.g., cylinder radius increased, circle radius increased)
- **Adaptive scaling** via `HitMask`: if a higher-priority element is hit, threshold set to 0

**File**: `GizmoElementArrow.cpp:90-101`

```cpp
if (UGizmoElementBase* HitMaskGizmo = HitMask.Get())
{
    const FInputRayHit MaskHit = HitMaskGizmo->LineTrace(ViewContext, LineTraceState, RayOrigin, RayDirection);
    if (MaskHit.bHit)
    {
        static constexpr float NoPixelHitThreshold = 0.f;
        UpdatePixelThreshold(CylinderElement, NoPixelHitThreshold);
        UpdatePixelThreshold(ConeElement, NoPixelHitThreshold);
        UpdatePixelThreshold(BoxElement, NoPixelHitThreshold);
    }
}
```

### A.3 Axis Stem Hit Testing (Ray-Cylinder Intersection)

**Geometry**: Infinite cylinder along axis, capped with bullet points

**Implementation**: Ray-cylinder intersection in `GizmoMath::RayCylinderIntersection()`

**File**: `GizmoElementCylinder.cpp:35-77`

```cpp
FInputRayHit UGizmoElementCylinder::LineTrace(const UGizmoViewContext* ViewContext,
    const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection)
{
    FLineTraceTraversalState CurrentLineTraceState(LineTraceState);
    bool bHittableViewDependent = UpdateLineTraceState(ViewContext, Base, CurrentLineTraceState);

    if (bHittableViewDependent)
    {
        bool bIntersects = false;
        double RayParam = 0.0;

        const double PixelHitThresholdAdjust = CurrentLineTraceState.PixelToWorldScale * PixelHitDistanceThreshold;
        const double WorldHeight = Height * CurrentLineTraceState.LocalToWorldTransform.GetScale3D().X + PixelHitThresholdAdjust * 2.0;
        const double WorldRadius = Radius * CurrentLineTraceState.LocalToWorldTransform.GetScale3D().X + PixelHitThresholdAdjust;
        const FVector WorldDirection = CurrentLineTraceState.LocalToWorldTransform.TransformVectorNoScale(Direction);
        const FVector LocalCenter = Direction * Height * 0.5;
        const FVector WorldCenter = CurrentLineTraceState.LocalToWorldTransform.TransformPosition(LocalCenter);

        FVector ClampedRayOrigin(RayOrigin);
        const double DepthBias = UE::GizmoUtil::ClampRayOrigin(ViewContext, ClampedRayOrigin, RayDirection);

        GizmoMath::RayCylinderIntersection(
            WorldCenter, WorldDirection, WorldRadius, WorldHeight,
            ClampedRayOrigin, RayDirection,
            bIntersects, RayParam);

        if (bIntersects)
        {
            RayParam += DepthBias;
            FInputRayHit RayHit(static_cast<float>(RayParam));
            RayHit.SetHitObject(this);
            RayHit.HitIdentifier = PartIdentifier;
            return RayHit;
        }
    }

    return FInputRayHit();
}
```

**Key Measurements**:
- **Radius** (default): varies by gizmo type (arrows ~0.5-0.75 world units)
- **Cylinder height**: extends full arrow length
- **Hit threshold application**: `WorldRadius = Radius + PixelThresholdAdjust`

### A.4 Plane Handle Hit Testing (Ray-Disk Intersection)

**Geometry**: Filled disk in plane normal to both axes being handled

**Implementation**: Ray-circle intersection in `UGizmoElementCircle::LineTrace()`

**File**: `GizmoElementCircle.cpp:48-113`

Two modes: **mesh hit** and **line hit**

#### A.4.1 Mesh Hit (Most Common)

```cpp
if (bHitMesh)
{
    WorldRadius += PixelHitThresholdAdjust;  // Expand disk radius by 7 pixels

    UE::Geometry::FLinearIntersection Result;
    IntersectionUtil::RayCircleIntersection(RayOrigin, RayDirection, WorldCenter, WorldRadius, WorldNormal, Result);

    if (Result.intersects)
    {
        FInputRayHit RayHit(static_cast<float>(Result.parameter.Min));
        RayHit.SetHitObject(this);
        RayHit.HitIdentifier = PartIdentifier;
        return RayHit;
    }
}
```

**Details**:
- **Radius** (default): ~0.6-0.8 world units (plane handles are smaller than axis stems)
- Hit any point inside disk projection onto camera ray
- Returns **closest intersection point** along ray

#### A.4.2 Line Hit (Rotation Circle)

For thin circle outlines (e.g., rotation arcs):

```cpp
else if (bHitLine)
{
    FPlane Plane(WorldCenter, WorldNormal);
    double HitDepth = FMath::RayPlaneIntersectionParam(RayOrigin, RayDirection, Plane);
    if (HitDepth < 0)
    {
        return FInputRayHit();
    }

    FVector HitPoint = RayOrigin + RayDirection * HitDepth;

    FVector NearestCirclePos;
    GizmoMath::ClosetPointOnCircle(HitPoint, WorldCenter, WorldNormal, static_cast<float>(WorldRadius), NearestCirclePos);

    FRay Ray(RayOrigin, RayDirection, true);
    FVector NearestRayPos = Ray.ClosestPoint(NearestCirclePos);

    const double HitBuffer = PixelHitThresholdAdjust + LineThickness;
    double Distance = FVector::Distance(NearestCirclePos, NearestRayPos);

    if (Distance <= HitBuffer)
    {
        FInputRayHit RayHit(static_cast<float>(HitDepth));
        RayHit.SetHitObject(this);
        RayHit.HitIdentifier = PartIdentifier;
        return RayHit;
    }
}
```

**Hit Threshold for Rotation Circle**:
- `HitBuffer = PixelHitThresholdAdjust + LineThickness`
- Distance from ray to circle edge must be **≤ HitBuffer**
- **LineThickness**: typically 1-2 pixels (varies by gizmo style)

### A.5 Rotation Circle Hit Testing

**Geometry**: Circle in plane perpendicular to rotation axis

**Behavior**:
- Ray is intersected with **plane** containing circle (plane normal = rotation axis)
- Nearest point on circle to intersection found
- Distance from ray to circle measured in world space
- **Hit threshold**: 7 pixels (expanded via PixelHitDistanceThreshold)

**File**: `GizmoElementCircle.cpp:83-110`

Key equation:
```
HitBuffer = PixelToWorldScale * 7.0 + LineThickness
Distance = |ClosestCirclePoint - ClosestRayPoint|
bHit = Distance <= HitBuffer
```

### A.6 Center Handle Hit Testing

**Geometry**: Small box or sphere at gizmo origin

**Implementation**: Box or sphere ray intersection (same pattern as cylinder)

**Default Size**: ~0.4-0.5 world units (smallest handle)

**File**: `GizmoElementBox.cpp` - uses similar pattern to cylinder

### A.7 Priority Order (Overlapping Hits)

**Resolution**: First hit along ray wins (closest hit depth)

**File**: `AxisPositionGizmo.cpp:130-142`

```cpp
FInputRayHit UAxisPositionGizmo::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
    FInputRayHit GizmoHit;
    if (HitTarget && AxisSource && ParameterSource)
    {
        GizmoHit = HitTarget->IsHit(PressPos);  // Returns closest hit
        if (GizmoHit.bHit)
        {
            LastHitPosition = PressPos.WorldRay.PointAt(GizmoHit.HitDepth);
        }
    }
    return GizmoHit;
}
```

**Behavior**:
- All elements tested with same ray
- **Closest intersection** is returned
- No manual priority system—geometry ordering determines priority
- Gizmo hierarchies can use view-dependent culling to hide non-priority elements

---

## B. Translate Drag Behavior

### B.1 Axis Translation (1D)

**Interaction**: Click on axis stem → drag along world space ray

**File**: `AxisPositionGizmo.cpp:144-216`

#### B.1.1 Press Phase

```cpp
void UAxisPositionGizmo::OnClickPress(const FInputDeviceRay& PressPos)
{
    InteractionOrigin = LastHitPosition;  // Where user clicked
    InteractionAxis = AxisSource->GetDirection();  // Axis direction (unit vector)

    // Find nearest point on axis to the mouse ray
    FVector NearestPt; float RayNearestParam;
    GizmoMath::NearestPointOnLineToRay(InteractionOrigin, InteractionAxis,
        PressPos.WorldRay.Origin, PressPos.WorldRay.Direction,
        InteractionStartPoint, InteractionStartParameter,
        NearestPt, RayNearestParam);

    FVector AxisOrigin = AxisSource->GetOrigin();

    double DirectionSign = FVector::DotProduct(InteractionStartPoint - AxisOrigin, InteractionAxis);
    ParameterSign = (bEnableSignedAxis && DirectionSign < 0) ? -1.0f : 1.0f;

    InteractionCurPoint = InteractionStartPoint;
    InteractionStartParameter *= ParameterSign;
    InteractionCurParameter = InteractionStartParameter;

    InitialTargetParameter = ParameterSource->GetParameter();
    ParameterSource->BeginModify();
    bInInteraction = true;
}
```

**Key Points**:
- **Does NOT use ray-plane projection** — uses **closest point on line to ray** (3D line-ray nearest point)
- `InteractionStartPoint` = nearest point on **axis** to **mouse ray**
- `InteractionStartParameter` = distance along axis from origin
- Sign of parameter preserved for **negative scaling** (when enabled)

#### B.1.2 Drag Phase

```cpp
void UAxisPositionGizmo::OnClickDrag(const FInputDeviceRay& DragPos)
{
    FVector HitPoint;

    // Custom destination (optional, for scene alignment)
    FCustomDestinationParams Params;
    Params.WorldRay = &DragPos.WorldRay;
    if (ShouldUseCustomDestinationFunc() && CustomDestinationFunc(Params, HitPoint))
    {
        GizmoMath::NearestPointOnLine(InteractionOrigin, InteractionAxis, HitPoint,
            InteractionCurPoint, InteractionCurParameter);
        InteractionCurParameter += bCustomDestinationAlignsAxisOrigin ? InteractionStartAxisOriginParameterOffset : 0;
    }
    else
    {
        // Standard drag: project current ray to axis
        float RayNearestParam; float AxisNearestParam;
        FVector RayNearestPt;
        GizmoMath::NearestPointOnLineToRay(InteractionOrigin, InteractionAxis,
            DragPos.WorldRay.Origin, DragPos.WorldRay.Direction,
            InteractionCurPoint, AxisNearestParam,
            RayNearestPt, RayNearestParam);

        InteractionCurParameter = ParameterSign * AxisNearestParam;
    }

    float DeltaParam = InteractionCurParameter - InteractionStartParameter;
    float NewParamValue = InitialTargetParameter + DeltaParam;

    ParameterSource->SetParameter(NewParamValue);
}
```

**Algorithm**:
1. Project **current mouse ray** to **axis** using `NearestPointOnLineToRay()`
2. Compute **new parameter** along axis: `AxisNearestParam`
3. Compute **delta**: `NewParamValue = InitialValue + (CurrentParam - StartParam)`
4. Send to `ParameterSource` which applies translation

**What This Means**:
- User can **move mouse perpendicular to axis** without affecting parameter
- Translation is **smooth and continuous** along the axis
- **Camera angle independent** — no plane selection needed
- **Signed axis mode** (scaling): preserves sign during drag

### B.2 Plane Translation (2D)

**Interaction**: Click on plane handle → drag across plane

**File**: `PlanePositionGizmo.cpp` (407 lines, not fully shown)

**Algorithm**:
1. Get **plane normal** from AxisSource (combined axis direction)
2. On press: Find **ray-plane intersection** at start
3. On drag: Find **ray-plane intersection** at current position
4. Convert 3D plane points to **2D tangent coordinates**
5. Send UV delta to `ParameterSource`

**Key Difference from Axis**:
- Uses **ray-plane intersection**, not line-ray nearest point
- Projects mouse ray onto the plane directly
- Returns **2D coordinates** in plane tangent space

**Tangent Axes**:
- Computed from plane normal using `MakeNormalPlaneBasis()`
- Or obtained from `AxisSource->GetTangentVectors()`

### B.3 Mouse Delta Conversion

**NOT direct screen-space projection** — all conversions are **3D world space**

**Process**:
1. Convert mouse position to **world ray** (origin + direction)
2. Find intersection/nearest-point with **gizmo geometry** (axis, plane, or circle)
3. Measure **change in 3D position**
4. Project back to **1D parameter** (axis) or **2D parameter** (plane)

**Key Insight**: This is **view-agnostic** — works with any camera angle, perspective or orthographic.

---

## C. Rotate Drag Behavior

### C.1 Rotation Angle Computation

**Interaction**: Click on rotation circle → drag around circle

**File**: `AxisAngleGizmo.cpp:92-187`

#### C.1.1 Press Phase

```cpp
void UAxisAngleGizmo::OnClickPress(const FInputDeviceRay& PressPos)
{
    check(bInInteraction == false);

    RotationAxis = AxisSource->GetDirection();
    RotationOrigin = GizmoMath::ProjectPointOntoLine(LastHitPosition, AxisSource->GetOrigin(), RotationAxis);

    if (AxisSource->HasTangentVectors())
    {
        AxisSource->GetTangentVectors(RotationPlaneX, RotationPlaneY);
    }
    else
    {
        GizmoMath::MakeNormalPlaneBasis(RotationAxis, RotationPlaneX, RotationPlaneY);
    }

    bool bIntersects; FVector IntersectionPoint;
    GizmoMath::RayPlaneIntersectionPoint(
        RotationOrigin, RotationAxis,
        PressPos.WorldRay.Origin, PressPos.WorldRay.Direction,
        bIntersects, IntersectionPoint);
    check(bIntersects);

    InteractionStartPoint = IntersectionPoint;
    InteractionCurPoint = InteractionStartPoint;

    InteractionStartAngle = GizmoMath::ComputeAngleInPlane(InteractionCurPoint,
        RotationOrigin, RotationAxis, RotationPlaneX, RotationPlaneY);
    InteractionCurAngle = InteractionStartAngle;

    float StartAngleAbs = FMath::Abs(InteractionStartAngle);
    ClosestAxisStartAngle = StartAngleAbs <= PI / 4 ? 0
        : StartAngleAbs >= PI * 3 / 4 ? PI
        : InteractionStartAngle > 0 ? PI / 2 : - PI / 2;

    InitialTargetAngle = AngleSource->GetParameter();
    AngleSource->BeginModify();

    bInInteraction = true;
}
```

**Key Details**:
- **Plane normal** = rotation axis
- **Tangent vectors** = RotationPlaneX and RotationPlaneY (basis vectors in plane)
- **Start angle** = angle from tangent X to intersection point
- **Angle sign** = standard 2D plane angle convention (atan2)

#### C.1.2 Drag Phase

```cpp
void UAxisAngleGizmo::OnClickDrag(const FInputDeviceRay& DragPos)
{
    check(bInInteraction);

    FVector HitPoint;
    float DeltaAngle;

    FCustomDestinationParams Params;
    Params.WorldRay = &DragPos.WorldRay;
    if (ShouldUseCustomDestinationFunc() && CustomDestinationFunc(Params, HitPoint))
    {
        InteractionCurPoint = HitPoint;
        InteractionCurAngle = GizmoMath::ComputeAngleInPlane(InteractionCurPoint,
            RotationOrigin, RotationAxis, RotationPlaneX, RotationPlaneY);

        DeltaAngle = InteractionCurAngle - ClosestAxisStartAngle;
    }
    else
    {
        bool bIntersects; FVector IntersectionPoint;
        GizmoMath::RayPlaneIntersectionPoint(
            RotationOrigin, RotationAxis,
            DragPos.WorldRay.Origin, DragPos.WorldRay.Direction,
            bIntersects, IntersectionPoint);

        if (bIntersects == false)
        {
            return;
        }

        InteractionCurPoint = IntersectionPoint;

        InteractionCurAngle = GizmoMath::ComputeAngleInPlane(InteractionCurPoint,
            RotationOrigin, RotationAxis, RotationPlaneX, RotationPlaneY);

        DeltaAngle = InteractionCurAngle - InteractionStartAngle;
    }

    float NewAngle = InitialTargetAngle + DeltaAngle;
    AngleSource->SetParameter(NewAngle);
}
```

**Algorithm**:
1. Intersect mouse ray with **rotation plane** (normal = axis)
2. Compute **angle** of intersection point in plane (atan2 of tangent coordinates)
3. Delta = CurrentAngle - StartAngle
4. Apply: `NewRotation = InitialRotation + DeltaAngle`

### C.2 Angle Computation Details

**Function**: `GizmoMath::ComputeAngleInPlane()`

**File**: `GizmoMath.h:127-130`

```cpp
float ComputeAngleInPlane(
    const FVector& Point,
    const FVector& PlaneOrigin, const FVector& PlaneNormal,
    const FVector& PlaneAxis1, const FVector& PlaneAxis2);
```

**Process**:
1. Project point onto plane
2. Express in 2D using PlaneAxis1 and PlaneAxis2
3. Compute `atan2(Y, X)` in plane coordinates
4. Returns signed angle in **radians**

### C.3 Angle Snapping (Optional)

**File**: `AxisAngleGizmo.cpp:123-126`

```cpp
float StartAngleAbs = FMath::Abs(InteractionStartAngle);
ClosestAxisStartAngle = StartAngleAbs <= PI / 4 ? 0
    : StartAngleAbs >= PI * 3 / 4 ? PI
    : InteractionStartAngle > 0 ? PI / 2 : - PI / 2;
```

**Behavior**:
- On press: determines **closest cardinal axis** (0°, 90°, 180°, 270°)
- During drag with **custom destination**: aligns nearest axis to destination
- **NOT automatic snapping** — only when custom alignment enabled

### C.4 No Built-In Rotation Arc Preview

**Finding**: UE5 gizmo code does NOT render rotation arc during drag.

- Preview/feedback is **external to interaction system**
- Rendering handled by gizmo components (UGizmoElementArc, etc.)
- Interaction only computes angle, doesn't visualize arc

---

## D. Scale Drag Behavior

### D.1 Axis Scale (1D)

**Uses**: `UAxisPositionGizmo` with different parameter source

**File**: `AxisPositionGizmo.cpp:74-128`

```cpp
bool UAxisPositionGizmo::InitializeAsScaleGizmo(
    const UE::GizmoUtil::FTransformSubGizmoCommonParams& Params,
    bool bDisallowNegativeScaling, UE::GizmoUtil::FTransformSubGizmoSharedState* SharedState)
{
    // ...
    bEnableSignedAxis = true;

    // Scale along UNROTATED cardinal axes, not rotated local axes
    UGizmoComponentAxisSource* UnitCardinalAxisSource = nullptr;
    // ...

    UGizmoAxisScaleParameterSource* CastParameterSource = UGizmoAxisScaleParameterSource::Construct(
        UnitCardinalAxisSource, TransformSource, Owner);
    ParameterSource = CastParameterSource;
    CastParameterSource->bClampToZero = bDisallowNegativeScaling;

    return true;
}
```

**Algorithm**:
- Uses **cardinal axes** (not local axes) for scale direction
- Same as translate, but parameter source is `UGizmoAxisScaleParameterSource`
- Parameter = **scale factor**, not distance
- Signed axis: supports **negative scaling** (flip direction)
- Optional clamping: `bClampToZero` prevents scale < 0

### D.2 Plane Scale (2D)

**File**: `PlanePositionGizmo.cpp:77-132`

Similar to axis scale, but:
- Uses **plane** handles (2D)
- Returns **2D scale factors** for both axes in plane
- Optional **uniform scaling** flag

```cpp
bool UPlanePositionGizmo::InitializeAsScaleGizmo(
    const UE::GizmoUtil::FTransformSubGizmoCommonParams& Params, bool bDisallowNegativeScaling,
    UE::GizmoUtil::FTransformSubGizmoSharedState* SharedState)
{
    // ...
    UGizmoPlaneScaleParameterSource* CastParameterSource = UGizmoPlaneScaleParameterSource::Construct(
        UnitCardinalAxisSource, TransformSource, Owner);
    ParameterSource = CastParameterSource;
    CastParameterSource->bClampToZero = bDisallowNegativeScaling;
    CastParameterSource->bUseEqualScaling = true;  // Uniform or non-uniform
    // ...
}
```

### D.3 Uniform Scale

**Special Case**: Scale plane aligned to camera

**File**: `PlanePositionGizmo.cpp:134-195`

```cpp
bool UPlanePositionGizmo::InitializeAsUniformScaleGizmo(
    const UE::GizmoUtil::FTransformSubGizmoCommonParams& Params,
    bool bDisallowNegativeScaling,
    UE::GizmoUtil::FTransformSubGizmoSharedState* SharedState)
{
    // Uses CameraAxisSource (plane always faces camera)
    // CustomTickFunction updates axis source each frame
    // Parameter source = UGizmoUniformScaleParameterSource (1D scale factor)
}
```

**Behavior**:
- Plane rotates to face camera each frame
- Single scale factor applied uniformly
- Useful for uniform scale handle on gizmo center

### D.4 Scale Limits

**Range**: No built-in min/max in gizmo framework

- Parameter sources can implement clamping
- Typical: `bClampToZero` prevents negative scale
- No default min/max scale factor

---

## E. Visual Feedback During Drag

### E.1 Interaction State Management

**File**: `GizmoElementHitTargets.cpp:23-57`

```cpp
void UGizmoElementHitTarget::UpdateHoverState(bool bHovering)
{
    if (!GizmoElement)
    {
        return;
    }

    if (bHovering)
    {
        GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::Hovering);
    }
    else if (GizmoElement->GetElementInteractionState() != EGizmoElementInteractionState::Interacting)
    {
        GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::None);
    }
}

void UGizmoElementHitTarget::UpdateInteractingState(bool bInteracting)
{
    if (!GizmoElement)
    {
        return;
    }

    if (bInteracting)
    {
        GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::Interacting);
    }
    else
    {
        GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::None);
    }
}
```

**States**:
- `None` — not hovered, not interacting
- `Hovering` — mouse over, not pressed
- `Interacting` — mouse pressed and dragging

### E.2 Material Switching During Interaction

**File**: `GizmoElementBase.h:73-90` (render state management)

```cpp
// Returns the mesh material based on the current interaction state.
const UMaterialInterface* GetCurrentMaterial()
{
    return MeshRenderState.GetMaterial(InteractionState);
}

// Returns the mesh vertex color based on the current interaction state.
FLinearColor GetCurrentVertexColor()
{
    return MeshRenderState.GetVertexColor(InteractionState);
}
```

**Behavior**:
- Each gizmo element has **3 materials**: None, Hovering, Interacting
- Material changes automatically based on state
- Color/thickness also changes via MeshRenderState

### E.3 Inactive Axes During Single-Axis Drag

**Implementation**: Not in base gizmo framework directly

- Gizmo builder sets `SetVisibleState()` on non-active axes
- Or uses **interaction state** to dim materials
- Base framework: no automatic hiding of perpendicular axes

**File**: `GizmoElementBase.h:122-128`

```cpp
INTERACTIVETOOLSFRAMEWORK_API virtual void SetVisibleState(bool bVisible);
INTERACTIVETOOLSFRAMEWORK_API virtual bool GetVisibleState() const;

INTERACTIVETOOLSFRAMEWORK_API virtual void SetHittableState(bool bHittable);
INTERACTIVETOOLSFRAMEWORK_API virtual bool GetHittableState() const;
```

### E.4 No Built-In Guide Plane Visualization

**Finding**: Base gizmo framework does NOT render guide planes during drag

- Visualization is external (handled by gizmo components)
- Interaction system only computes values
- Editor can render guide plane via separate render pass

---

## F. Coordinate Space Handling

### F.1 World vs Local Space

**File**: `GizmoElementBase.h:48-71` (FRenderTraversalState)

```cpp
struct FRenderTraversalState
{
    // LocalToWorld transform
    // Note: non-uniform scale is not supported and the X scale element will be used for uniform scaling.
    FTransform LocalToWorldTransform;

    // Pixel to world scale
    double PixelToWorldScale = 1.0;

    // Initialize state
    void Initialize(const FSceneView* InSceneView, FTransform InTransform)
    {
        LocalToWorldTransform = InTransform;
        PixelToWorldScale = UE::GizmoRenderingUtil::CalculateLocalPixelToWorldScale(InSceneView, InTransform.GetLocation());
    }
};
```

**Application**:
1. **Local axes** stored in gizmo element properties
2. **Transformed to world** via `LocalToWorldTransform`
3. Hit tests and drag all occur in **world space**
4. Parameter sources convert back to **local space** for application

### F.2 Local Axis Computation

**File**: `AxisPositionGizmo.cpp:147-148`

```cpp
InteractionAxis = AxisSource->GetDirection();  // Returns world-space unit vector
```

**Direction Determination**:
- `IGizmoAxisSource` provides **world-space axis direction**
- Typically: X-axis, Y-axis, Z-axis (in local or world space)
- Can be **camera-aligned** for free rotation

### F.3 Local Rotation Application

**Key Detail**: Scale is applied BEFORE rotation

**File**: `AxisPositionGizmo.cpp:99-128`

```cpp
// For scale gizmo, use UNROTATED cardinal axes
UGizmoComponentAxisSource* UnitCardinalAxisSource = nullptr;
// ...
UGizmoComponentAxisSource* CastAxisSource = UGizmoComponentAxisSource::Construct(RootComponent, AxisIndex,
    // bUseLocalAxes, has to be false to always use cardinal axes
    false,
    Owner);
```

**Why**: Transform order is `Scale * Rotation * Translation`

- Scale must be along **unrotated axes**
- Otherwise rotating before scaling produces incorrect result

### F.4 Pixel-to-World Scale Factor

**Computation**: `PixelToWorldScale = CalculateLocalPixelToWorldScale(ViewContext, Position)`

**Purpose**: Convert pixel distances to world distances based on:
- Camera distance to gizmo
- Perspective/orthographic projection
- Field of view

**Usage**: `PixelHitThreshold_World = PixelToWorldScale * 7.0`

---

## G. Summary: Exact Specifications

### G.1 Hit Testing Constants

| Property | Value | Unit | File |
|----------|-------|------|------|
| Default Pixel Hit Threshold | 7.0 | pixels | GizmoElementBase.h:428 |
| Rotation Circle Default Radius | 1.0 | world units | GizmoElementCircle (varies) |
| Axis Stem Default Radius | 0.5–0.75 | world units | GizmoElementArrow |
| Plane Handle Default Radius | 0.6–0.8 | world units | varies |
| Center Handle Size | 0.4–0.5 | world units | varies |
| View-Dependent Angle Tolerance | 0.052 rad | ~3 degrees | GizmoElementBase.h:30 |
| Rotation Circle Line Thickness | 1–2 | pixels | varies |

### G.2 Ray-Geometry Algorithms

| Geometry | Algorithm | Function |
|----------|-----------|----------|
| Axis Stem | Ray-Cylinder Intersection | GizmoMath::RayCylinderIntersection() |
| Plane Handle | Ray-Circle Intersection (filled disk) | IntersectionUtil::RayCircleIntersection() |
| Rotation Circle | Ray-Plane + Circle Distance | GizmoMath::RayPlaneIntersectionPoint() + ClosetPointOnCircle() |
| All Axes | Line-Ray Nearest Point | GizmoMath::NearestPointOnLineToRay() |

### G.3 Translate Drag

| Aspect | Behavior |
|--------|----------|
| **Algorithm** | Project mouse ray to axis/plane (3D, not screen-space) |
| **Plane Selection** | Automatic—uses perpendicular plane to non-active axes |
| **Guide Plane** | No built-in visualization; external to gizmo framework |
| **Coordinate System** | World-space ray, converted to local space at end |
| **Screen Space?** | NO—fully 3D world-space algorithm |

### G.4 Rotate Drag

| Aspect | Behavior |
|--------|----------|
| **Angle Computation** | atan2(Y, X) in plane defined by rotation axis |
| **Space** | World-space plane intersection + angle in plane |
| **Snapping** | Optional per-gizmo; finds nearest 45° axis on press |
| **Arc Preview** | No built-in—external to gizmo framework |
| **Continuous** | Full 360° rotation, no clamping by default |

### G.5 Scale Drag

| Aspect | Behavior |
|--------|----------|
| **Axis Scale** | Same algorithm as translate, but parameter = scale factor |
| **Plane Scale** | 2D scale factors, one per axis in plane |
| **Uniform Scale** | Plane perpendicular to camera, 1D scale |
| **Negative Scale** | Optional clamping via bClampToZero; supported if enabled |
| **Limits** | No default min/max; implementable in parameter source |

### G.6 Interaction State Transitions

```
None ←→ Hovering ←→ Interacting

- User moves over gizmo: None → Hovering
- User presses mouse: Hovering → Interacting
- User moves away while hovering: Hovering → None
- User releases while interacting: Interacting → None
- User moves away while interacting: Interacting remains until release
```

**Visual Feedback**:
- Material swaps based on state
- Color/thickness change per state
- Inactive axes can be dimmed (external logic)

---

## H. Implementation Checklist for UzEngine

### H.1 Essential Components

- [ ] Ray-cylinder intersection (axis hit test)
- [ ] Ray-circle intersection (plane/rotation hit test)
- [ ] Ray-plane intersection (rotation plane)
- [ ] Line-ray nearest point (translate drag)
- [ ] Plane tangent basis computation
- [ ] Angle-in-plane computation
- [ ] Pixel-to-world scale calculation
- [ ] Interaction state machine (None/Hovering/Interacting)
- [ ] Material/color swapping based on state

### H.2 Critical Behavioral Rules

1. **Hit threshold is 7 pixels** (expandable per gizmo)
2. **Translate uses line-ray nearest point, NOT ray-plane projection**
3. **Rotate computes angles in plane normal to rotation axis**
4. **Scale uses unrotated cardinal axes** (before rotation applied)
5. **All math is 3D world-space** — no screen-space projection
6. **Closest hit depth wins** — no manual priority system
7. **Interaction state drives visual feedback** — material swaps
8. **View-dependent culling** — hide axes at grazing angles (< 3°)

### H.3 Non-Essential (Can Implement Later)

- Arc preview rendering during rotation
- Guide plane visualization during translate
- Automatic axis dimming during drag
- Angle snapping (can be per-gizmo)
- Custom destination alignment
- Negative scale handling
- Uniform scale camera-aligned planes
