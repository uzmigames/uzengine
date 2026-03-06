# Delta for Editor Gizmos

## MODIFIED Requirements

### Requirement: Rotation Drag Behavior
The rotation gizmo SHALL use screen-space horizontal mouse delta to compute the rotation angle, behaving like a linear slider. Dragging in the positive screen-X direction SHALL always add angle, and dragging in the negative screen-X direction SHALL always subtract angle, regardless of camera orientation or gizmo position.

The sensitivity SHALL be approximately 0.5 degrees per pixel (Math.PI / 360 radians per pixel).

The rotation axis SHALL always be the world-space axis (X, Y, or Z) corresponding to the constrained axis, never the object's local axis.

#### Scenario: Drag right adds rotation
Given a node is selected and the rotation gizmo is active on the Y axis
When the user clicks and drags 180 pixels to the right on screen
Then the node rotates +90 degrees around the world Y axis

#### Scenario: Drag left subtracts rotation
Given a node is selected and the rotation gizmo is active on the Y axis
When the user clicks and drags 180 pixels to the left on screen
Then the node rotates -90 degrees around the world Y axis

#### Scenario: Consistent direction regardless of drag path
Given a node is selected and the rotation gizmo is active on the X axis
When the user drags right then left then right ending 100 pixels right of start
Then the final rotation is +50 degrees from the original rotation around world X

#### Scenario: Rotation is independent of camera angle
Given a node is selected and the rotation gizmo is active on the Z axis
When the user drags 180 pixels right from any camera viewing angle
Then the node always rotates +90 degrees around the world Z axis

### Requirement: Rotation Snap with Ctrl
The system SHALL snap rotation angles to 15-degree increments when the Ctrl key is held during a rotation drag.

#### Scenario: Ctrl-snap rotation
Given a node is selected and the rotation gizmo is active
When the user holds Ctrl and drags to produce a raw angle of 37 degrees
Then the applied rotation snaps to 30 degrees (nearest 15-degree increment)

### Requirement: World-Space Rotation Axes
The rotation gizmo rings SHALL always be aligned to world-space axes. The gizmo orientation SHALL NOT change based on the selected node's local rotation. This ensures predictable rotation behavior and avoids gimbal-lock confusion.

#### Scenario: Gizmo axes after node rotation
Given a node has been rotated 45 degrees around world Y
When the user selects the node and activates the rotation gizmo
Then the gizmo rings are still aligned to world X, Y, and Z axes

### Requirement: Multi-Select Rotation
When multiple nodes are selected, a rotation drag SHALL apply the same angular delta around the world-space axis to each selected node individually. Each node's rotation is updated independently using its own starting rotation quaternion.

#### Scenario: Multi-select rotation drag
Given three nodes are selected via box-select
When the user drags the rotation gizmo 90 pixels to the right on the Y axis
Then all three nodes rotate by the same +45-degree delta around world Y
And each node's final rotation is computed from its own original rotation
