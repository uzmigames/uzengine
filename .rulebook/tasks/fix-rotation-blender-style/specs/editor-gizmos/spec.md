# Delta for Editor Gizmos

## MODIFIED Requirements

### Requirement: Rotation Drag Behavior
The rotation gizmo SHALL use arc-sweep rotation around the screen-projected gizmo center. The system SHALL project the gizmo world-space origin to screen coordinates, then compute the angular sweep of the mouse position around this projected center using atan2. Dragging clockwise on screen SHALL rotate in the positive direction when looking along the rotation axis, and counter-clockwise SHALL rotate in the negative direction.

The sign of the rotation SHALL be determined by the dot product of the constrained world-space axis with the camera forward vector. When the dot product is negative (axis points away from camera), the angular delta SHALL be negated to maintain consistent visual behavior.

The rotation axis SHALL always be the world-space axis (X, Y, or Z) corresponding to the constrained axis, never the object's local axis.

#### Scenario: Clockwise sweep adds rotation
Given a node is selected and the rotation gizmo is active on the Y axis
When the camera is looking along the negative Y axis (top-down view)
And the user clicks and sweeps the mouse 90 degrees clockwise around the projected gizmo center
Then the node rotates +90 degrees around the world Y axis

#### Scenario: Counter-clockwise sweep subtracts rotation
Given a node is selected and the rotation gizmo is active on the Y axis
When the camera is looking along the negative Y axis (top-down view)
And the user clicks and sweeps the mouse 90 degrees counter-clockwise around the projected gizmo center
Then the node rotates -90 degrees around the world Y axis

#### Scenario: Consistent direction from opposite camera angle
Given a node is selected and the rotation gizmo is active on the Y axis
When the camera is looking along the positive Y axis (bottom-up view)
And the user clicks and sweeps the mouse 90 degrees clockwise around the projected gizmo center
Then the node still rotates +90 degrees around the world Y axis (sign flip applied)

#### Scenario: Arc sweep follows mouse around gizmo ring
Given a node is selected and the rotation gizmo is active on the Z axis
When the user clicks at the 3 o'clock position on the gizmo ring and drags to the 12 o'clock position
Then the node rotates by the angular difference between those two screen positions around world Z

#### Scenario: Back-and-forth drag returns to original rotation
Given a node is selected and the rotation gizmo is active on the X axis
When the user sweeps 45 degrees clockwise then 45 degrees counter-clockwise back to the start angle
Then the final rotation equals the original rotation before the drag

### Requirement: Rotation Snap with Ctrl
The system SHALL snap rotation angles to 15-degree increments when the Ctrl key is held during an arc-sweep rotation drag.

#### Scenario: Ctrl-snap rotation
Given a node is selected and the rotation gizmo is active
When the user holds Ctrl and sweeps the mouse to produce a raw arc angle of 37 degrees
Then the applied rotation snaps to 30 degrees (nearest 15-degree increment)

### Requirement: Multi-Select Arc Rotation
When multiple nodes are selected, an arc-sweep rotation drag SHALL apply the same angular delta around the world-space axis to each selected node individually. Each node's rotation is updated independently using its own starting rotation quaternion.

#### Scenario: Multi-select arc-sweep rotation
Given three nodes are selected via box-select
When the user sweeps the rotation gizmo 45 degrees clockwise on the Y axis
Then all three nodes rotate by the same +45-degree delta around world Y
And each node's final rotation is computed from its own original rotation
