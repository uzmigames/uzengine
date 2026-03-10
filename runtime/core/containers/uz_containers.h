#pragma once

/// UzEngine Containers — Collection of zero-copy, allocation-friendly data structures.
///
/// # Module Overview
/// Specialized container types optimized for game engine use:
/// - **RingBuffer**: Fixed circular FIFO queue (stream processing)
/// - **FreeList**: Intrusive free list (element-stable allocation)
/// - **HandleMap**: Generation-based handle → T mapping (safe indices)
/// - **FrameArray**: Temporary allocations (frame lifetime scoped)
///
/// All containers use stack or provided allocators (no std::allocator magic).
/// Each header is independent — include only what you need.

#include "uz_frame_array.h"
#include "uz_free_list.h"
#include "uz_handle_map.h"
#include "uz_ring_buffer.h"
