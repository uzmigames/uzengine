/// UzEngine tinyexr Implementation
///
/// Compiles tinyexr implementation into the runtime library.
/// This file MUST be compiled exactly once — do NOT define
/// TINYEXR_IMPLEMENTATION in any other translation unit within
/// the runtime library.
///
/// tinyexr depends on miniz for zlib decompression. The miniz.c
/// translation unit is compiled separately in build.zig from
/// runtime/thirdparty/tinyexr/miniz.c.

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
