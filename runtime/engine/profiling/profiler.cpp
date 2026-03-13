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

/// GPU memory tracking stats (allocations, frees, pressure events).
StatGroup StatGPU{"GPU"};

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// Built-in Stat Counter Definitions
// ─────────────────────────────────────────────────────────────────────────────

/// Number of draw calls issued per frame.
UZ_DEFINE_STAT(Rendering, DrawCalls);
/// Number of triangles submitted per frame.
UZ_DEFINE_STAT(Rendering, Triangles);
/// Number of Vulkan descriptor set allocations from the global pool.
UZ_DEFINE_STAT(RHI, VkDescriptorAllocations);
/// Number of Vulkan descriptor pool resets.
UZ_DEFINE_STAT(RHI, VkDescriptorPoolResets);
/// Number of individual vkUpdateDescriptorSets calls per frame.
UZ_DEFINE_STAT(RHI, VkDescriptorWrites);
