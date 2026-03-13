# Physics Queries — Raycast, Sweep, Overlap

> **Status**: Planned (Phase 2)
> **Source**: `engine/physics/hit_result.h`, `engine/subsystem/physics_world.h`

## HitResult

```cpp
/// Complete result of a physics query. Contains both swept-shape and surface contact info.
struct HitResult {
    bool  blocking_hit;        // false = overlap only
    bool  start_penetrating;   // trace started inside geometry
    float time;                // 0..1 along the trace segment
    float distance;            // world units from start

    /// Where the swept shape CENTER ends up at contact.
    /// For a sphere sweep, this is the sphere center at hit time.
    Vec3  location;

    /// Actual surface contact point.
    /// For VFX spawning (sparks, decals) — always use this, not location.
    Vec3  impact_point;

    /// Normal of the swept shape surface (inward-facing for spheres).
    Vec3  normal;

    /// Normal of the HIT surface (outward-facing from geometry).
    /// Use this for floor angle checks, wall sliding, and surface alignment.
    Vec3  impact_normal;

    Vec3  trace_start;
    Vec3  trace_end;
    float penetration_depth;   // if start_penetrating, distance to depenetrate

    Entity          entity;           // which game entity was hit
    PhysBodyHandle  body;             // Jolt body ID
    PhysMaterialId  physical_material;// surface type for footsteps/decals
    uint32_t        face_index;       // triangle index (complex trace only)
};
```

**Critical distinction**: `location` vs `impact_point`. Character movement uses `impact_normal` for floor walkability. VFX uses `impact_point` for spark placement. Never conflate them.

## Query API

```cpp
/// Query parameters — per-query filtering.
struct QueryParams {
    bool trace_complex = false;         // hit triangle mesh, not simplified hull
    bool find_initial_overlaps = true;  // detect if start is already inside
    bool return_physical_material = false;

    /// Entities to skip (inline, no heap allocation).
    Entity ignore_entities[8];
    uint32_t ignore_entity_count = 0;
};

class PhysicsWorld : public WorldSubsystem {
public:
    // ─── Line Trace ────────────────────────────────────────
    /// Cast a ray. Returns true if a blocking hit was found.
    bool LineTraceSingle(HitResult& out, Vec3 start, Vec3 end,
                         CollisionChannel channel,
                         const QueryParams& params = {}) const;

    /// Cast a ray. Returns all hits (blocking + overlapping), sorted by distance.
    int32_t LineTraceMulti(HitResult* out, int32_t max_results,
                           Vec3 start, Vec3 end,
                           CollisionChannel channel,
                           const QueryParams& params = {}) const;

    // ─── Sweep (Swept Shape) ───────────────────────────────
    /// Sweep a shape (sphere, box, capsule) along a line. Returns first blocking hit.
    bool SweepSingle(HitResult& out, Vec3 start, Vec3 end, Quat rotation,
                     const CollisionShape& shape, CollisionChannel channel,
                     const QueryParams& params = {}) const;

    int32_t SweepMulti(HitResult* out, int32_t max_results,
                       Vec3 start, Vec3 end, Quat rotation,
                       const CollisionShape& shape, CollisionChannel channel,
                       const QueryParams& params = {}) const;

    // ─── Overlap (Static Test) ─────────────────────────────
    /// Test if a shape at a position overlaps anything. Returns overlap count.
    int32_t OverlapMulti(HitResult* out, int32_t max_results,
                         Vec3 position, Quat rotation,
                         const CollisionShape& shape, CollisionChannel channel,
                         const QueryParams& params = {}) const;

    /// Quick boolean test — any overlap at all?
    bool OverlapAny(Vec3 position, Quat rotation,
                    const CollisionShape& shape, CollisionChannel channel,
                    const QueryParams& params = {}) const;
};

/// Shape for sweeps and overlaps.
struct CollisionShape {
    enum Type { Sphere, Box, Capsule };
    Type type;
    union {
        struct { float radius; } sphere;
        struct { Vec3 half_extents; } box;
        struct { float radius; float half_height; } capsule;
    };

    static CollisionShape MakeSphere(float radius);
    static CollisionShape MakeBox(Vec3 half_extents);
    static CollisionShape MakeCapsule(float radius, float half_height);
};
```

## Usage Examples

```cpp
// Raycast from camera for aiming
HitResult hit;
Vec3 start = camera.GetWorldPosition();
Vec3 end = start + camera.GetForward() * 1000.0f;

if (GetWorld().GetSubsystem<PhysicsWorld>().LineTraceSingle(
        hit, start, end, CollisionChannel::Visibility)) {
    SpawnDecal(hit.impact_point, hit.impact_normal);
}

// Overlap sphere for area damage
HitResult overlaps[32];
auto shape = CollisionShape::MakeSphere(5.0f);
int32_t count = physics.OverlapMulti(overlaps, 32, explosion_pos,
                                      Quat::Identity(), shape, CollisionChannel::Pawn);
for (int32_t i = 0; i < count; ++i) {
    float dist = Vec3::Distance(explosion_pos, overlaps[i].impact_point);
    float damage = Lerp(100.0f, 10.0f, dist / 5.0f);
    ApplyDamage(overlaps[i].entity, damage);
}
```
