#pragma once

// Core Layer — umbrella include for all engine primitive types.
//
// Including this single header brings in the complete core type library:
//   - Arena allocator (arena.h)
//   - Generational handles (handle.h)
//   - String interning (string_id.h)
//   - Result type (result.h)
//   - Math types (math.h)
//
// Every engine subsystem should include "engine/core/core.h" rather than
// cherry-picking individual headers, unless compile-time is a concern.

#include "arena.h"
#include "handle.h"
#include "string_id.h"
#include "result.h"
#include "math.h"
