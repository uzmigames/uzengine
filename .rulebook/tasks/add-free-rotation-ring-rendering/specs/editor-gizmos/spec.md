# Delta for Editor Gizmos — Free Rotation Ring Rendering

## ADDED Requirements

### Requirement: Free Rotation Ring Visual
The engine SHALL render a 4th rotation ring (white/grey) that is always billboarded to face the camera. The ring radius SHALL be 2.0 * gizmoScale, slightly larger than the axis rings (1.8 * gizmoScale). The ring color SHALL be #CCCCCC (grey) by default and #FFFFFF (white) when hovered.

#### Scenario: Free ring faces camera
Given the rotation gizmo is visible in the viewport
When the camera orbits around the scene
Then the free rotation ring always faces the camera (billboarded)
And its radius is visually larger than the three axis rings
