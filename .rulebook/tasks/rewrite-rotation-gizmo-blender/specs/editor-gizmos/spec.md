# Delta for Editor Gizmos — Rotation Rewrite (Blender Reproduction)

## MODIFIED Requirements

### Requirement: Axis-Constrained Rotation Drag
The rotation gizmo SHALL compute rotation by projecting the mouse ray onto the rotation plane (whose normal is the constrained world-space axis) and computing the signed angle between the initial direction vector and the current direction vector in that plane.

The signed angle SHALL be computed as `atan2(dot(axis, cross(v0, v1)), dot(v0, v1))` where v0 and v1 are normalized vectors from the pivot to the plane intersection points.

The system SHALL use incremental delta rotation (vPrev to vCurrent each frame) to support continuous rotation beyond 360 degrees without drift or jumps.

When the mouse ray is nearly parallel to the rotation plane (|dot(rayDir, planeNormal)| < 0.01), the system SHALL freeze rotation for that frame rather than producing unstable values.

The rotation axis SHALL always be the world-space axis corresponding to the constrained axis when in World orientation mode, or the object's local axis when in Local orientation mode.

#### Scenario: 90-degree rotation via plane projection
Given a node is selected and the rotation gizmo Y-axis ring is active
When the user clicks at a point on the rotation plane and sweeps 90 degrees around the pivot
Then the node rotates exactly 90 degrees around the world Y axis
And the rotation is computed from the signed angle between start and current vectors on the plane

#### Scenario: Continuous rotation beyond 360 degrees
Given a node is selected and the rotation gizmo X-axis ring is active
When the user sweeps the mouse two full circles around the pivot without releasing
Then the total accumulated rotation is approximately 720 degrees
And no jumps or discontinuities occur during the sweep

#### Scenario: Near-parallel ray freeze
Given a node is selected and the rotation gizmo Z-axis ring is active
When the camera is oriented such that the view direction is nearly parallel to the Z axis
And the mouse ray barely grazes the rotation plane
Then the rotation freezes (no delta applied) rather than producing wild angle jumps

#### Scenario: Rotation continues when cursor leaves ring area
Given a node is selected and the Y-axis ring is being dragged
When the cursor moves far from the ring but stays on the rotation plane
Then rotation continues smoothly without canceling the drag

### Requirement: Rotation Snap with Modifiers
The system SHALL support three snapping modes during rotation drag:

- **Ctrl held**: snap the total accumulated angle to 5-degree increments (Blender default).
- **Shift held**: reduce rotation sensitivity by a factor of 0.1 (precision mode).
- **Ctrl+Shift held**: snap to 1-degree increments with precision sensitivity.

Snapping SHALL be applied to the total angle relative to drag start, not to per-frame deltas, to prevent error accumulation.

#### Scenario: Ctrl-snap to 5 degrees
Given a node is being rotated with the Y-axis ring
When the user holds Ctrl and sweeps to produce a raw total angle of 17 degrees
Then the applied rotation snaps to 15 degrees (nearest 5-degree increment)

#### Scenario: Shift precision mode
Given a node is being rotated with the X-axis ring
When the user holds Shift and moves the mouse the same distance as a normal 90-degree sweep
Then the applied rotation is approximately 9 degrees (10x reduction)

#### Scenario: Ctrl+Shift for 1-degree snap
Given a node is being rotated with the Z-axis ring
When the user holds Ctrl and Shift and sweeps to produce a raw total angle of 3.7 degrees
Then the applied rotation snaps to 4 degrees (nearest 1-degree increment)

## ADDED Requirements

### Requirement: Free Rotation (Trackball / Arcball)
The system SHALL provide a free rotation handle (outer white circle, billboarded to face the camera) that initiates Shoemake arcball rotation.

The arcball SHALL map cursor position to a virtual sphere centered on the projected pivot in screen space. When the cursor is inside the sphere (x^2 + y^2 <= 1), z = sqrt(1 - x^2 - y^2). When outside, the point SHALL be projected to the sphere rim with z = 0.

The rotation quaternion SHALL be computed as the rotation from the start sphere vector to the current sphere vector, then transformed from camera/screen space to world space using the camera basis vectors.

Free rotation MUST NOT exhibit gimbal lock and SHALL feel stable near the center and controlled near the edge of the sphere.

#### Scenario: Arcball rotation via outer ring
Given a node is selected and the rotation gizmo is visible
When the user clicks on the outer white circle and drags to the right
Then the node rotates around the camera's vertical axis proportionally to the sweep
And no gimbal lock occurs regardless of the accumulated rotation

#### Scenario: Arcball edge behavior
Given a node is selected and the user is dragging the outer free ring
When the cursor moves beyond the arcball sphere radius
Then the rotation continues smoothly along the sphere rim (z = 0 mapping)
And no sudden jumps or discontinuities occur

#### Scenario: Free rotation snap
Given a node is selected and the user is dragging the outer free ring with Ctrl held
When the accumulated rotation angle reaches 17 degrees
Then the rotation snaps to 15 degrees around the dominant rotation axis

### Requirement: Hover Hysteresis and Priority Picking
The system SHALL determine the hovered rotation ring by selecting the candidate with the smallest ray-t (closest along the mouse ray) among all rings within the pixel tolerance.

The system SHALL add hysteresis: if a ring is currently hovered, it SHALL remain hovered unless another ring's hit distance is at least 20% smaller than the current one.

The pixel tolerance for ring hover detection SHALL be computed in world space as `pixelsToWorldRadius(pivot, tolerancePixels)` where tolerancePixels is approximately 10 pixels for hover and 6 pixels for click.

The free rotation ring (outer white circle) SHALL have lower picking priority than axis rings when both are within tolerance.

#### Scenario: Stable hover without flickering
Given the cursor is near the intersection of the X-axis and Y-axis rings
When the cursor moves slightly back and forth between the two rings
Then the currently hovered ring remains stable (does not flicker between X and Y)
And the hovered ring only changes when the new candidate is significantly closer

#### Scenario: Free ring lower priority
Given the cursor is near both the Z-axis ring and the outer free ring
When both rings are within the hover tolerance
Then the Z-axis ring is selected as the hovered ring (axis rings have priority)

### Requirement: Rotation Gizmo State Machine
The rotation gizmo interaction SHALL follow an explicit state machine with the following states: Idle, HoverAxisX, HoverAxisY, HoverAxisZ, HoverFree, DragAxis, DragFree.

Transitions:
- Idle -> Hover*: based on picking result each frame.
- Hover* + MouseDown -> DragAxis or DragFree: locks the active handle until mouseup.
- DragAxis/DragFree + MouseUp -> Idle.
- DragAxis/DragFree + Escape/RightClick -> Idle with rotation reverted to dragStartRotation.

While in DragAxis, rotation SHALL be computed only around the locked axis regardless of where the cursor moves.

#### Scenario: Axis lock during drag
Given the Y-axis ring is hovered and the user presses mousedown
When the user drags the cursor to a position that would be closer to the X-axis ring
Then the rotation continues around the Y axis only (axis is locked at mousedown)

#### Scenario: Cancel drag reverts rotation
Given the Y-axis ring is being dragged and the node has rotated 45 degrees
When the user presses Escape
Then the node's rotation reverts to its value before the drag started
And the gizmo returns to Idle state

### Requirement: Multi-Select Rotation
When multiple nodes are selected, axis-constrained rotation SHALL apply the same incremental delta angle to each selected node individually around the world-space axis, using each node's own starting rotation quaternion.

If the rotation pivot is not at a node's origin, the node's position SHALL orbit around the pivot by the delta angle around the rotation axis.

Free rotation multi-select SHALL apply the same world-space delta quaternion to each node.

#### Scenario: Multi-select axis rotation
Given three nodes are selected with the gizmo pivot at the median position
When the user sweeps the Y-axis ring by 45 degrees
Then all three nodes rotate by 45 degrees around world Y using their individual start rotations

#### Scenario: Multi-select position orbiting
Given two nodes are selected and the pivot is at their median point (not at either node's origin)
When the user rotates 90 degrees around the Y axis
Then each node's position orbits 90 degrees around the pivot point on the XZ plane
And each node's rotation is also updated by the 90-degree delta
