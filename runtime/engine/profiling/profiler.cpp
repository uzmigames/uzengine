/// UzEngine Profiler Implementation
///
/// Defines built-in stat groups for organizing frame statistics.
/// Each StatGroup is used to categorize related counters (draw calls, allocations, etc.)
/// for easier debugging and performance monitoring.

#include "profiler.h"

namespace uz {

/// Built-in stat groups for categorizing frame statistics.
///
/// Each group can hold multiple StatCounter instances.
/// Groups are displayed together in profiler UI / debug output.

/// Rendering subsystem stats (draw calls, triangles, state changes).
StatGroup StatRendering{"Rendering"};

/// RHI backend stats (GPU fence waits, descriptor heap usage).
StatGroup StatRHI{"RHI"};

/// Physics subsystem stats (collisions, constraint solves, queries).
StatGroup StatPhysics{"Physics"};

/// Scene/ECS stats (entity count, component queries, tree traversals).
StatGroup StatScene{"Scene"};

/// Memory system stats (allocations, deallocations, pool usage).
StatGroup StatMemory{"Memory"};

/// I/O subsystem stats (file reads, asset loads, shader compiles).
StatGroup StatIO{"IO"};

} // namespace uz
