# Animation Runtime — Specification

## ADDED Requirements

### Requirement: AnimationClip
The animation system SHALL load and sample keyframe animation data.

#### Scenario: Clip Sampling
Given an AnimationClip with 60 frames at 30fps (2 seconds duration)
When SamplePose(time=0.5) is called
Then the result SHALL be a Pose with interpolated bone transforms at t=0.5s
And position/scale SHALL use linear interpolation
And rotation SHALL use spherical linear interpolation (SLERP)

#### Scenario: Looping Clip
Given a looping AnimationClip with duration=2.0
When SamplePose(time=3.5) is called
Then time SHALL wrap to 1.5 (3.5 mod 2.0)
And the pose SHALL be sampled at the wrapped time

### Requirement: AnimStateMachine
The animation system SHALL evaluate state transitions based on named parameters.

#### Scenario: State Transition
Given states [Idle, Walk, Run] with transition Idle→Walk when "speed" > 0.1
And current state is Idle with parameter "speed" = 0.5
When the state machine evaluates
Then it SHALL transition to Walk
And a crossfade blend SHALL occur over the transition's blend_time (e.g., 0.2s)

#### Scenario: Multiple Conditions
Given transition Walk→Run when "speed" > 5.0 AND "is_grounded" == true
When speed=6.0 and is_grounded=true
Then the transition SHALL fire

### Requirement: Blend Space 1D
The animation system SHALL blend between clips based on a continuous parameter.

#### Scenario: Walk-Run Blend
Given BlendSpace1D with clips at thresholds [0.0=idle, 3.0=walk, 8.0=run]
And parameter value = 5.5
When the blend space evaluates
Then it SHALL blend between walk and run: t = (5.5-3.0)/(8.0-3.0) = 0.5
And the output pose SHALL be 50% walk + 50% run

### Requirement: Root Motion
The animation system SHALL extract root bone translation and apply it to the character controller.

#### Scenario: Root Motion Extraction
Given an animation with root bone moving 2.0 units forward per cycle
When root_motion=true
Then root bone translation SHALL be extracted as delta_movement per frame
And the root bone in the output pose SHALL be zeroed (no visual double-movement)
And delta_movement SHALL be applied to CCharacterController::Move()

### Requirement: AnimNotify Events
The animation system SHALL fire events at specific keyframe times.

#### Scenario: Footstep Notify
Given an AnimNotify at time=0.4s with event="footstep_left"
When the clip playback crosses time=0.4s
Then an AnimNotifyEvent SHALL be fired via EventBus
And the event SHALL contain: entity, event_name, params (optional JSON)

### Requirement: Lua Animation API
The scripting system SHALL expose animation control to Lua.

#### Scenario: Lua Play Animation
Given a Lua script calling uz.animation.play(entity, "walk")
Then the entity's AnimationPlayer SHALL crossfade to the "walk" clip
