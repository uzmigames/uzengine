# Navigation (Recast/Detour) — Specification

## ADDED Requirements

### Requirement: Navmesh Baking
The navigation system SHALL generate navigation meshes from scene geometry.

#### Scenario: Offline Bake
Given a scene with static mesh geometry (floor, walls, ramps)
When NavBaker::Bake(scene, config) is called with cell_size=0.3, agent_radius=0.6, agent_height=2.0
Then Recast SHALL voxelize the geometry
And generate walkable polygons
And output a .uznavmesh binary file

#### Scenario: Runtime Bake
Given a scene loaded with runtime_bake=true in .uzproj
When the scene finishes loading
Then navmesh SHALL be baked on a background thread
And NavSystem SHALL be notified when baking completes

### Requirement: Pathfinding
The navigation system SHALL find paths on the navmesh.

#### Scenario: Find Path
Given a navmesh and start=(0,0,0), end=(10,0,5)
When NavSystem::FindPath(start, end) is called
Then the result SHALL be a vector of waypoints along the navmesh surface
And the path SHALL avoid non-walkable areas

#### Scenario: No Path
Given start and end on disconnected navmesh islands
When NavSystem::FindPath(start, end) is called
Then the result SHALL indicate failure (no path found)

### Requirement: NavMeshAgent
The navigation system SHALL provide an agent component for automated movement.

#### Scenario: Move To Destination
Given a CNavMeshAgent with speed=5.0 and destination=(10,0,5)
When the navigation system ticks
Then the agent SHALL move along the computed path at speed 5.0
And position SHALL be updated each frame
And the agent SHALL stop when within stopping_distance of destination

### Requirement: Crowd Steering
The navigation system SHALL support multi-agent local avoidance.

#### Scenario: Agents Avoid Each Other
Given 10 CNavMeshAgents moving through a narrow corridor
When DetourCrowd updates
Then agents SHALL steer around each other using RVO2
And no two agents SHALL overlap (within their radius)
