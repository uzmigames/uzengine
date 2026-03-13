# Editor Visual Tools — Spec Delta

## ADDED Requirements

### Requirement: Node Picking via Pick Buffer
The editor MUST render a 1/4 resolution pick buffer encoding each node's unique ID as an RGB color, supporting single-click and box (marquee) selection via CPU readback.

#### Scenario: Single Click Selection
Given a scene with 3 nodes rendered to the pick buffer
When the user clicks on node B's pixel
Then the readback SHALL return node B's ID
And node B SHALL become the active selection

#### Scenario: Box Selection
Given a scene with 5 visible nodes
When the user drags a rectangle covering 3 of them
Then readback SHALL return the 3 covered node IDs
And all 3 SHALL be added to the selection set

### Requirement: Transform Gizmo
The editor MUST render a 3-axis transform gizmo (move/rotate/scale) on the selected node, with axis-colored handles (red=X, green=Y, blue=Z) and mouse drag interaction.

#### Scenario: Move Along X Axis
Given a node is selected and the move gizmo is active
When the user drags the red (X) arrow handle
Then the node's position SHALL change only along the X axis
And the gizmo SHALL follow the node in real-time

#### Scenario: No Selection
Given no node is selected
When the viewport is rendered
Then no gizmo SHALL be visible

### Requirement: Selection Outline
The editor MUST render a screen-space outline around selected nodes using stencil-based masking to distinguish selected from unselected geometry.

#### Scenario: Outline Visibility
Given node A is selected
When the scene is rendered
Then node A SHALL have a visible colored outline
And unselected nodes SHALL have no outline

### Requirement: Debug Draw API
The engine MUST provide an immediate-mode debug draw API (DrawLine, DrawSphere, DrawBox, DrawFrustum) that renders wireframe primitives for one frame, flushed at the end of each frame.

#### Scenario: Draw Debug Line
Given code calls DebugDraw::Line(start, end, color)
When the current frame renders
Then a colored line SHALL appear between start and end
And the line SHALL NOT persist to the next frame

### Requirement: Node-Type Gizmos
The editor MUST render type-specific visual helpers for camera nodes (frustum wireframe), directional lights (arrow), point lights (radius sphere), and spot lights (cone).

#### Scenario: Point Light Gizmo
Given a point light node with range=10 is visible
When the editor renders gizmos
Then a wireframe sphere of radius 10 SHALL be drawn at the light's position
