# Camera System — Specification

## ADDED Requirements

### Requirement: SpringArm
The camera system SHALL provide a spring arm for third-person cameras with collision avoidance.

#### Scenario: Normal Operation
Given a SpringArm with arm_length=3.0 and socket_offset=(0, 0.5, 0)
When the target entity is at position (0, 0, 0)
Then the camera SHALL be positioned 3.0 units behind the target plus socket offset
And the camera SHALL look at the target position

#### Scenario: Collision Avoidance
Given a SpringArm with arm_length=5.0 and a wall 2.0 units behind the target
When a sphere cast (radius=probe_radius) hits the wall at distance 2.0
Then arm_length SHALL be shortened to 2.0 (wall distance)
And when the wall is no longer blocking, arm_length SHALL smoothly return to 5.0

#### Scenario: Lag Smoothing
Given a SpringArm with enable_lag=true and lag_speed=8.0
When the target moves suddenly
Then the camera SHALL follow with exponential lag (lerp at lag_speed*dt)

### Requirement: Camera Modes
The camera system SHALL support multiple camera behavior modes.

#### Scenario: Third Person Mode
Given camera mode = ThirdPerson
When the player moves and looks around
Then the camera SHALL follow via SpringArm with input-driven yaw/pitch

#### Scenario: Orbit Mode
Given camera mode = Orbit with orbit_target entity
When mouse/gamepad input provides delta yaw/pitch
Then the camera SHALL orbit around the target at fixed distance
And scroll wheel SHALL adjust orbit distance

#### Scenario: Cinematic Mode
Given camera mode = Cinematic with a spline path
When the cinematic plays
Then the camera SHALL follow the spline at specified speed
And optional look_at target SHALL keep the camera aimed at a point

### Requirement: Camera Shake
The camera system SHALL provide camera shake effects.

#### Scenario: Explosion Shake
Given CameraShake(magnitude=5.0, frequency=20.0, duration=0.5)
When AddShake() is called
Then the camera SHALL be offset by Perlin noise scaled by magnitude
And the shake SHALL decay exponentially over 0.5 seconds
And multiple shakes SHALL accumulate additively

### Requirement: FOV Interpolation
The camera system SHALL smoothly interpolate field-of-view changes.

#### Scenario: ADS Zoom
Given default_fov=75, ads_fov=45, interp_speed=10.0
When aim-down-sights is activated
Then FOV SHALL smoothly interpolate from 75 to 45 over ~0.3s
When ADS is released
Then FOV SHALL return to 75
