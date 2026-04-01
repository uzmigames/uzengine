# Behavior Trees — Specification

## ADDED Requirements

### Requirement: BT Node Types
The behavior tree system SHALL implement standard composite, decorator, and leaf node types.

#### Scenario: Sequence Node
Given a Sequence with children [MoveTo, PlayAnimation, Wait]
When evaluated
Then children SHALL execute left-to-right
And if MoveTo returns Success, PlayAnimation SHALL execute
And if any child returns Failure, the Sequence SHALL return Failure immediately
And if all children return Success, the Sequence SHALL return Success

#### Scenario: Selector Node
Given a Selector with children [AttackIfClose, ChaseIfVisible, Patrol]
When evaluated
Then children SHALL execute left-to-right
And if AttackIfClose returns Failure, ChaseIfVisible SHALL execute
And if any child returns Success, the Selector SHALL return Success immediately
And if all children return Failure, the Selector SHALL return Failure

#### Scenario: Parallel Node
Given a Parallel with children [MoveTo, LookAround] and policy=RequireOne
When evaluated
Then both children SHALL tick simultaneously
And if either returns Success, the Parallel SHALL return Success

### Requirement: Decorators
The behavior tree system SHALL provide decorator nodes that modify child behavior.

#### Scenario: Cooldown Decorator
Given a Cooldown(5.0s) wrapping an Attack action
When Attack returns Success
Then the decorator SHALL block re-execution for 5 seconds
And during cooldown, the decorator SHALL return Failure without ticking the child

#### Scenario: Conditional Decorator
Given a Conditional("health > 50") wrapping a child
When the blackboard "health" value is 30
Then the decorator SHALL return Failure without ticking the child

### Requirement: Blackboard
The behavior tree system SHALL provide a per-entity key-value store for sharing data between nodes.

#### Scenario: Set/Get Value
Given a Blackboard on entity E
When SetFloat("patrol_radius", 10.0) is called
Then GetFloat("patrol_radius") SHALL return 10.0
And GetFloat("nonexistent") SHALL return default (0.0)

#### Scenario: Blackboard Types
Given a Blackboard
When values are set
Then it SHALL support: Float, Int, Bool, Vec3, EntityHandle, String

### Requirement: Built-in Action Nodes
The behavior tree system SHALL provide common action nodes out of the box.

#### Scenario: MoveTo Action
Given a MoveTo node with blackboard key "target_position"
When ticked
Then it SHALL call CNavMeshAgent::SetDestination(blackboard["target_position"])
And return Running while the agent is moving
And return Success when the agent reaches the destination
And return Failure if no path exists

#### Scenario: Wait Action
Given a Wait(duration=3.0) node
When ticked
Then it SHALL return Running for 3.0 seconds
And return Success after the duration elapses

### Requirement: .uzbt Format
The behavior tree system SHALL load tree definitions from JSON files.

#### Scenario: Load Tree
Given a .uzbt file: { "root": { "type": "Selector", "children": [ { "type": "Sequence", "children": [...] } ] } }
When BehaviorTree::Load(path) is called
Then the tree SHALL be constructed from the JSON definition
And hot-reload SHALL work (file change → reload tree without restart)

### Requirement: Lua Custom Nodes
The scripting system SHALL allow registering custom BT nodes from Lua.

#### Scenario: Lua Action Node
Given a Lua script registering: uz.bt.register_action("CustomAttack", function(entity, bb) ... return "success" end)
When the BT evaluates a node of type "CustomAttack"
Then the Lua function SHALL be called
And the return value ("success"/"failure"/"running") SHALL determine the node result
