# Audio 3D & Advanced — Specification

## ADDED Requirements

### Requirement: 3D Spatial Audio
The audio system SHALL position sounds in 3D space relative to the listener using X3DAudio.

#### Scenario: 3D Sound Playback
Given a CAudioSource with spatial=true at position (10, 0, 0)
And a CAudioListener at position (0, 0, 0) facing forward (1, 0, 0)
When the sound plays
Then X3DAudio SHALL calculate DSP settings (panning, volume, LPF)
And the sound SHALL appear to come from the right speaker/headphone

#### Scenario: Distance Attenuation
Given a CAudioSource at distance 50m from listener with attenuation_min=1.0, attenuation_max=100.0
When X3DAudio calculates DSP
Then volume SHALL be attenuated by inverse-square law
And at distance >= attenuation_max, volume SHALL be 0 (culled)

#### Scenario: Doppler Effect
Given a CAudioSource moving at velocity (20, 0, 0) relative to listener
When X3DAudio calculates DSP
Then pitch SHALL be shifted based on relative velocity
And approach → higher pitch, retreat → lower pitch

### Requirement: Audio Streaming
The audio system SHALL stream large audio files from disk instead of loading entire PCM into memory.

#### Scenario: Stream Large File
Given a .uzaudio file > 2MB (e.g., 3-minute music track)
When loaded with streaming=true
Then only a ring buffer of 64KB SHALL be allocated in memory
And PCM data SHALL be decoded in chunks on a background thread
And XAudio2 SHALL use double-buffered XAUDIO2_BUFFER submissions

### Requirement: Audio Occlusion
The audio system SHALL attenuate sounds behind walls using physics raycasts.

#### Scenario: Occluded Sound
Given a CAudioSource behind a wall (physics raycast from listener to source is blocked)
When the audio system updates
Then a low-pass filter SHALL be applied to the voice
And volume SHALL be reduced by the occlusion factor

### Requirement: Audio Events
The audio system SHALL support data-driven sound events for variation.

#### Scenario: Random Clip Selection
Given an AudioEvent "footstep" with clips [step1.uzaudio, step2.uzaudio, step3.uzaudio] and mode=random
When the event is played
Then one clip SHALL be selected randomly (avoiding immediate repeat)
And pitch SHALL be varied within the event's pitch_range (e.g., 0.9-1.1)
