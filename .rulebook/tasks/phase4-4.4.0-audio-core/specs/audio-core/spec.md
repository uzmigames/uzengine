# Audio Core — Specification

## ADDED Requirements

### Requirement: XAudio2 Engine Lifecycle
The audio system SHALL manage an XAudio2 engine with a voice pool for concurrent sound playback.

#### Scenario: Engine Initialization
Given the engine is starting up
When AudioEngine::Init() is called
Then IXAudio2 SHALL be created via XAudio2Create()
And a mastering voice SHALL be created (system default format)
And 4 submix voices SHALL be created: SFX (bus 0), Music (bus 1), Voice (bus 2), Master (bus 3)
And SFX/Music/Voice submix voices SHALL route to Master
And a voice pool of 64 IXAudio2SourceVoice instances SHALL be pre-allocated

#### Scenario: Engine Shutdown
Given the audio engine is running
When AudioEngine::Shutdown() is called
Then all active source voices SHALL be stopped and destroyed
And all submix voices SHALL be destroyed
And the mastering voice SHALL be destroyed
And IXAudio2 SHALL be released

#### Scenario: Voice Pool Exhaustion
Given all 64 voice pool slots are in use
When a new sound requests playback
Then the lowest-priority voice SHALL be stolen (stopped and reassigned)
And if all voices have equal priority, the oldest voice SHALL be stolen

### Requirement: CAudioSource Component
The audio system SHALL provide an ECS component for sound playback attached to entities.

#### Scenario: Play Sound
Given a CAudioSource with clip="footstep.uzaudio", volume=0.8, bus=SFX
When Play() is called
Then a voice SHALL be acquired from the pool
And the AudioClip PCM data SHALL be submitted to the voice
And the voice SHALL route to the SFX submix
And volume SHALL be set to 0.8

#### Scenario: Looping Sound
Given a CAudioSource with loop=true
When playback reaches the end of the clip
Then the voice SHALL restart from the beginning
And playback SHALL continue seamlessly

#### Scenario: Stop Sound
Given a playing CAudioSource
When Stop() is called
Then the voice SHALL be stopped
And the voice SHALL be returned to the pool

### Requirement: AudioClip Asset
The audio system SHALL support a binary .uzaudio format for decoded PCM audio data.

#### Scenario: WAV Import
Given a WAV file "explosion.wav" (44100Hz, 16-bit, stereo)
When imported via AudioImporter
Then a .uzaudio file SHALL be created with PCM frames + metadata header
And metadata SHALL contain: sample_rate, channels, bit_depth, duration_seconds, loop_start, loop_end

#### Scenario: OGG Import
Given an OGG Vorbis file "music.ogg"
When imported via AudioImporter
Then stb_vorbis SHALL decode to PCM
And a .uzaudio file SHALL be created with the decoded PCM data

### Requirement: Bus Mixing
The audio system SHALL provide independent volume control per audio bus.

#### Scenario: Set Bus Volume
Given the SFX bus with volume=1.0
When SetBusVolume(SFX, 0.5) is called
Then the SFX submix voice volume SHALL be set to 0.5
And all sounds routed to SFX SHALL play at half volume
And Music/Voice buses SHALL be unaffected

#### Scenario: Mute Bus
Given the Music bus playing background music
When SetBusVolume(Music, 0.0) is called
Then all music voices SHALL become silent
And voices SHALL NOT be stopped (ready to unmute)

### Requirement: Lua Audio API
The scripting system SHALL expose basic audio playback to Lua.

#### Scenario: Lua Play Sound
Given a Lua script calling uz.audio.play("content/audio/explosion.uzaudio", { volume = 0.9, bus = "sfx" })
Then the audio system SHALL load the clip and play it as a one-shot fire-and-forget sound

#### Scenario: Lua Set Bus Volume
Given a Lua script calling uz.audio.set_bus_volume("music", 0.3)
Then the Music bus volume SHALL be set to 0.3
