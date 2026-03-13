# Audio System — 3D Spatialization

> **Status**: Planned (Phase 3)
> **Source**: `engine/audio/spatial_audio.h/.cpp`, `engine/audio/attenuation.h`
> **Depends on**: Audio engine, physics (for occlusion raycasts)

## Purpose

3D audio spatialization makes sounds feel immersive — a gunshot to your left plays louder in the left speaker, distant explosions fade and muffle, occluded voices are muffled. This requires:

- **Distance attenuation** — inverse square law, logarithmic, or linear falloff
- **Panning** — left/right speaker balance based on source position relative to listener
- **Air absorption** — high frequencies fade over distance (LPF cutoff decreases)
- **Occlusion** — physics raycast detects walls, applies additional muffling
- **Reverb zones** — indoor/outdoor reverb send amount based on listener position

## Listener Setup

Only one listener per world (usually the camera). The listener defines the reference frame for all spatialization:

```cpp
/// Audio listener — the viewpoint for 3D sound.
class AudioListenerComponent : public SceneComponent {
public:
    /// Activate this listener (only one active per world).
    void SetActive();

    /// Get listener world position and velocity for Doppler shift (future feature).
    Vec3 GetPosition() const { return GetWorldPosition(); }
    Vec3 GetVelocity() const { return velocity; }

private:
    Vec3 velocity = {0, 0, 0};  // Updated each frame for Doppler
};
```

Usage:

```cpp
// In player camera entity
entity.AddComponent<AudioListenerComponent>()->SetActive();
```

When a game loads a scene, it automatically finds the first `AudioListenerComponent` marked active. If none exists, spatialization is disabled (audio still plays 2D).

## Distance Attenuation

### Falloff Models

Three attenuation curves determine how volume decreases with distance:

```cpp
enum class AttenuationFalloff {
    /// v = 1 − (distance − min_distance) / (max_distance − min_distance)
    /// Linear fade from 100% (min) to 0% (max). Cheap, predictable.
    Linear,

    /// v = max(1, (min_distance / distance)²)
    /// Inverse square law — physically accurate for point sources.
    /// Volume drops sharply near source, gradually far away.
    InverseSquare,

    /// v = max(0, 1 − log10(distance / min_distance) / log10(max_distance / min_distance))
    /// Exponential curve — perceptually matches human hearing decay.
    /// Between inverse square and linear.
    Logarithmic,
};

struct AttenuationSettings {
    bool enable_distance = true;
    float min_distance = 100.0f;      // full volume inside this radius
    float max_distance = 5000.0f;     // silent beyond this radius
    AttenuationFalloff falloff = AttenuationFalloff::InverseSquare;

    // Spatialization (panning)
    bool enable_spatialization = true;
    float non_spatialized_radius = 50.0f;  // within this, play centered (mono→stereo blend)

    // Air absorption (high-freq muffling with distance)
    bool enable_air_absorption = false;
    float lpf_min_distance = 500.0f;
    float lpf_max_distance = 5000.0f;
    float lpf_cutoff_near = 20000.0f;  // Hz (full bandwidth)
    float lpf_cutoff_far = 2000.0f;    // Hz (heavily muffled)

    // Occlusion (wall blocking)
    bool enable_occlusion = false;
    float occlusion_check_interval = 0.1f;  // raycast every 100ms
    float occlusion_lpf_cutoff = 1000.0f;   // Hz when fully occluded
    float occlusion_volume_scale = 0.3f;    // volume multiplier when blocked
    float occlusion_interp_time = 0.2f;     // fade-in time for unblocking
};
```

### Attenuation Asset

Attenuation settings are stored as reusable assets (`.uzatten` JSON):

```json
{
  "format": "uzatten",
  "version": 1,
  "name": "footstep_outdoor",
  "falloff_model": "inverse_square",
  "min_distance": 50.0,
  "max_distance": 800.0,
  "spatialization": {
    "enabled": true,
    "non_spatialized_radius": 30.0
  },
  "air_absorption": {
    "enabled": true,
    "lpf_min_distance": 200.0,
    "lpf_max_distance": 800.0,
    "lpf_cutoff_near": 20000.0,
    "lpf_cutoff_far": 3000.0
  },
  "occlusion": {
    "enabled": true,
    "check_interval": 0.15,
    "lpf_cutoff": 1200.0,
    "volume_scale": 0.4,
    "interp_time": 0.25
  }
}
```

Game code references the asset:

```cpp
auto footstep = sound->attenuation;  // loaded from .uzatten
distance_from_listener = (source_position - listener_position).Length();

// Calculate attenuation
float attenuation_volume = CalculateAttenuation(
    distance_from_listener,
    footstep->min_distance,
    footstep->max_distance,
    footstep->falloff
);  // returns 0.0-1.0
```

## Panning — Left/Right Localization

### Stereo Panning

Given listener and source position, compute left/right speaker balance:

```cpp
/// Calculate stereo pan based on source position relative to listener.
/// Returns -1.0 (far left) to 1.0 (far right), 0.0 (center).
float CalculatePan(Vec3 listener_pos, Vec3 source_pos, Vec3 listener_forward) {
    Vec3 to_source = (source_pos - listener_pos).Normalized();

    // Listener right vector (perpendicular to forward)
    Vec3 listener_right = Cross(listener_forward, {0, 1, 0}).Normalized();

    // Project source direction onto right axis
    float pan = Dot(to_source, listener_right);
    return Clamp(pan, -1.0f, 1.0f);
}

// At mix time:
float pan = CalculatePan(listener.pos, source.pos, listener.forward);
float left_gain = Sqrt(0.5f * (1.0f - pan));   // (1 - pan) / 2, then sqrt for energy conservation
float right_gain = Sqrt(0.5f * (1.0f + pan));

out_left  = sample * left_gain;
out_right = sample * right_gain;
```

### Non-Spatialized Radius

Close-range sounds shouldn't pan aggressively (unnatural). Within `non_spatialized_radius`, pan is blended toward center:

```cpp
float distance = (source_pos - listener_pos).Length();
float spat_blend = Clamp(
    (distance - 0.0f) / attenuation.non_spatialized_radius,
    0.0f, 1.0f
);
float final_pan = pan * spat_blend;  // 0 at close range, full pan at distance
```

## Air Absorption

### High-Frequency Muffling

As sound travels through air, high frequencies attenuate more than lows. Distant sounds sound muffled. Simulate with a low-pass filter (LPF) whose cutoff decreases with distance:

```cpp
/// Calculate LPF cutoff frequency based on distance.
float CalculateAirAbsorptionLPF(float distance,
                                float lpf_min_distance,
                                float lpf_max_distance,
                                float lpf_cutoff_near,
                                float lpf_cutoff_far) {
    float t = (distance - lpf_min_distance) / (lpf_max_distance - lpf_min_distance);
    t = Clamp(t, 0.0f, 1.0f);

    // Logarithmic interpolation (perceptually uniform)
    float ln_near = std::log(lpf_cutoff_near);
    float ln_far = std::log(lpf_cutoff_far);
    float ln_cutoff = ln_near + t * (ln_far - ln_near);
    return std::exp(ln_cutoff);
}

// Example: distant gunshot
// at 100m: cutoff = 20000 Hz (crisp)
// at 2000m: cutoff = 2000 Hz (heavily muffled)
```

### Filter Implementation

A simple 1st-order LPF (cheap, real-time):

```cpp
struct LowPassFilter {
    float cutoff_hz = 20000.0f;
    float sample_rate = 48000.0f;
    float state = 0.0f;  // previous output (for smoothing)

    float Process(float sample) {
        // Normalized cutoff frequency
        float fc = cutoff_hz / sample_rate;
        float a = 2.0f * fc / (1.0f + fc);  // alpha coefficient

        // One-pole filter: output = a*input + (1-a)*previous_output
        state = a * sample + (1.0f - a) * state;
        return state;
    }
};

// Per-voice LPF for air absorption:
for (size_t frame = 0; frame < frame_count; ++frame) {
    float sample = voice.GetSample(frame);
    float filtered = voice.lpf_air_absorption.Process(sample);
    out_buffer[frame] = filtered;
}
```

## Occlusion

### Physics Raycast Detection

When a source is inside an enclosed space, muffleit and reduce volume:

```cpp
/// Check if source is occluded from listener (blocked by walls).
/// Returns occlusion amount (0.0 = clear, 1.0 = fully blocked).
float CalculateOcclusion(Vec3 listener_pos, Vec3 source_pos, World& world) {
    // Simple raycast from listener → source
    PhysicsRayResult result = world.GetPhysics().Raycast(
        listener_pos, source_pos, COLLISION_LAYER_GEOMETRY);

    if (result.hit && result.distance < (source_pos - listener_pos).Length()) {
        // Raycast hit something between listener and source
        return 1.0f;  // fully occluded
    }
    return 0.0f;  // no obstruction
}
```

### Slow Update (Batching)

Raycasts are expensive. Check occlusion once every 100ms, not every frame:

```cpp
struct OcclusionState {
    float occlusion_amount = 0.0f;  // 0.0-1.0, updated every check_interval
    float last_check_time = 0.0f;
    float check_interval = 0.1f;    // seconds
};

void UpdateOcclusion(AudioVoice& voice, float current_time, World& world) {
    if (current_time - voice.occlusion_state.last_check_time < voice.occlusion_state.check_interval) {
        return;  // skip this frame
    }

    float new_occlusion = CalculateOcclusion(
        GetListener().GetPosition(),
        voice.position,
        world
    );

    // Smooth transition to avoid clicking
    float max_change = voice.attenuation.occlusion_interp_time * 1.0f;  // 0.2s transition
    voice.occlusion_state.occlusion_amount =
        Clamp(new_occlusion,
              voice.occlusion_state.occlusion_amount - max_change,
              voice.occlusion_state.occlusion_amount + max_change);

    voice.occlusion_state.last_check_time = current_time;

    // Apply effects
    float occlusion_lpf = Lerp(20000.0f, voice.attenuation.occlusion_lpf_cutoff,
                                voice.occlusion_state.occlusion_amount);
    float occlusion_volume = Lerp(1.0f, voice.attenuation.occlusion_volume_scale,
                                   voice.occlusion_state.occlusion_amount);

    voice.lpf_occlusion.cutoff_hz = occlusion_lpf;
    voice.volume_occlusion_scale = occlusion_volume;
}
```

### Optimization: Spatial Partitioning

With many sources, raycasts become slow. Use spatial broadphase:

```cpp
// Pre-frame: collect all voices within occlusion check distance
std::vector<AudioVoice*> voices_to_check;
for (auto& voice : voice_pool) {
    float dist = (voice.position - listener.pos).Length();
    if (dist < max_occlusion_check_distance && voice.attenuation.enable_occlusion) {
        voices_to_check.push_back(&voice);
    }
}

// Batch raycasts in order of distance (nearest first, in case we budget-limit)
std::sort(voices_to_check.begin(), voices_to_check.end(),
          [](AudioVoice* a, AudioVoice* b) {
              return a->distance < b->distance;
          });

// Check only top N voices per frame (e.g., max 4 raycasts/frame)
const int max_raycasts_per_frame = 4;
for (int i = 0; i < std::min((int)voices_to_check.size(), max_raycasts_per_frame); ++i) {
    UpdateOcclusion(*voices_to_check[i], current_time, world);
}
```

## Reverb Zones

### Zone-Based Reverb Send

Indoors, sounds get reverb ambience. Outdoors, none. Transition smoothly at zone boundaries:

```cpp
class AudioZone : public SceneComponent {
public:
    /// Volume multiplier when listener is INSIDE this zone (default=1.0).
    float interior_volume = 1.0f;

    /// LPF cutoff when inside (default=20000, no muffling).
    float interior_lpf_cutoff = 20000.0f;

    /// Volume multiplier when listener is OUTSIDE.
    float exterior_volume = 0.3f;

    /// LPF cutoff when outside (muffled wall).
    float exterior_lpf_cutoff = 2000.0f;

    /// How long to crossfade when entering/exiting zone.
    float crossfade_time = 0.5f;

    /// Reverb send amount (0.0-1.0) when listener is inside.
    float interior_reverb_send = 0.6f;
    float exterior_reverb_send = 0.0f;
};

/// Check if listener is inside the zone (AABB, sphere, or OBB).
bool IsListenerInside(const AudioZone& zone, Vec3 listener_pos) {
    if (zone.bounds_type == BoundsType::AABB) {
        return zone.bounds.Contains(listener_pos);
    }
    // ... handle other types
    return false;
}
```

### Per-Frame Zone Update

```cpp
void AudioSubsystem::UpdateReverbZones(float delta_time) {
    Vec3 listener_pos = GetListener().GetPosition();

    // Find most dominant zone (largest enclosing volume)
    AudioZone* dominant_zone = nullptr;
    float max_volume = 0.0f;
    for (auto& zone : zones) {
        if (IsListenerInside(zone, listener_pos)) {
            float zone_volume = zone.bounds.Volume();  // or priority
            if (zone_volume > max_volume) {
                max_volume = zone_volume;
                dominant_zone = &zone;
            }
        }
    }

    // Update reverb target (with smooth crossfade)
    bool was_in_zone = current_zone != nullptr;
    bool is_in_zone = dominant_zone != nullptr;

    if (is_in_zone != was_in_zone || dominant_zone != current_zone) {
        zone_crossfade_timer = 0.0f;
        current_zone = dominant_zone;
    }

    zone_crossfade_timer = std::min(zone_crossfade_timer + delta_time,
                                     current_zone ? current_zone->crossfade_time : 0.5f);
    zone_blend = zone_crossfade_timer / (current_zone ? current_zone->crossfade_time : 0.5f);

    // Apply to master bus reverb send
    float reverb_send = 0.0f;
    if (current_zone) {
        reverb_send = Lerp(0.0f, current_zone->interior_reverb_send, zone_blend);
    }
    GetBus("Master")->SetReverbSend(reverb_send);
}
```

## HRTF (Head-Related Transfer Function)

Head-related impulse responses (HRTF) provide realistic 3D audio for headphone users. Optional plugin (Phase 4):

```cpp
// Future: HRTF library (Microsoft Spatial Sound)
class HRTFProcessor {
public:
    /// Process mono source through HRTF to produce binaural stereo output.
    /// azimuth: 0° = front, 90° = right, 270° = left
    /// elevation: 0° = ear level, 90° = top, -90° = bottom
    void ProcessBinaural(float mono_sample, float azimuth, float elevation,
                         float& out_left, float& out_right);
};
```

For now, simple LCRS (Left-Center-Right-Surround) panning is sufficient.

## Rules

1. **ALWAYS** validate listener exists before spatializing — silent fallback if missing
2. **NEVER** raycast every frame — use interval-based occlusion checks (batch/budget)
3. **ALWAYS** smooth filter cutoff transitions — avoid clicking/pops at envelope edges
4. **NEVER** exceed physics query budget — track occlusion check count per frame
5. **ALWAYS** prioritize occlusion checks by distance — nearest voices first
