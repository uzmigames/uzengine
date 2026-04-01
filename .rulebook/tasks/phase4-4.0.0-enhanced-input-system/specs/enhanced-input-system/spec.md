# Enhanced Input System — Specification

## ADDED Requirements

### Requirement: InputAction
The input system SHALL define named, typed actions decoupled from physical keys.

#### Scenario: Bool Action
Given InputAction "IA_Jump" with type=Bool
When the bound key (Space) is pressed
Then the action value SHALL be true
When the key is released
Then the action value SHALL be false

#### Scenario: Axis2D Action
Given InputAction "IA_Move" with type=Axis2D
When W is pressed
Then the action value SHALL be (0, 1) (forward)
When W+D are pressed simultaneously
Then the action value SHALL be normalized (0.707, 0.707)

### Requirement: InputMappingContext
The input system SHALL support stackable mapping contexts with priority.

#### Scenario: Context Priority
Given two contexts: "Gameplay" (priority=0) and "Vehicle" (priority=1)
When both are active and both map "W" to different actions
Then the higher-priority "Vehicle" mapping SHALL consume the input
And "Gameplay" SHALL NOT receive the W key

#### Scenario: Context Push/Pop
Given the "UIMenu" context is pushed
When it is active
Then gameplay inputs SHALL be blocked (consume_input=true)
When the menu is closed and context is popped
Then gameplay inputs SHALL resume

### Requirement: InputModifiers
The input system SHALL provide a modifier pipeline that transforms raw input values.

#### Scenario: Dead Zone
Given a gamepad stick with DeadZone(inner=0.15, outer=0.95)
When the stick value is 0.10 (inside dead zone)
Then the output SHALL be 0.0
When the stick value is 0.50
Then the output SHALL be remapped to (0.50-0.15)/(0.95-0.15) ≈ 0.4375

#### Scenario: Negate Modifier
Given InputModifier Negate applied to a key mapping
When the raw value is 1.0
Then the output SHALL be -1.0

### Requirement: InputTriggers
The input system SHALL support multiple trigger types for when an action fires.

#### Scenario: Pressed Trigger
Given IA_Jump with trigger=Pressed
When the key goes from released to pressed (this frame)
Then the action SHALL fire once
And subsequent frames with key held SHALL NOT fire

#### Scenario: Held Trigger
Given IA_Sprint with trigger=Held(hold_time=0.2)
When the key is held for 0.2 seconds
Then the action SHALL start firing continuously
And releasing before 0.2s SHALL NOT trigger

### Requirement: Runtime Rebinding
The input system SHALL allow runtime key rebinding with persistence.

#### Scenario: Rebind Key
Given IA_Jump currently bound to Space
When RebindAction("IA_Jump", Key::Enter) is called
Then IA_Jump SHALL respond to Enter instead of Space
And the rebinding SHALL be saved to saves/bindings.uzinput

### Requirement: XInput Gamepad
The input system SHALL support up to 4 XInput gamepads.

#### Scenario: Gamepad Stick Input
Given a gamepad left stick pushed to (0.8, -0.3)
When IA_Move is mapped to LeftStick
Then the action value SHALL be (0.8, -0.3) after dead zone processing
