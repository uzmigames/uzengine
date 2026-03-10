# Math System Architectural Decisions

## Row-Major vs Column-Major (Mat4)
- **Decision**: Row-major m[row][col]. Translation in m[3][0..2].
- **Rationale**: DX12/HLSL row_major convention. Upload Mat4 directly as 16 root constants without transposing.
- **Breaking change from backup**: The backup math.h used column-major m[col][row]. The new math.h is row-major.
- **Convention**: v' = v * M (row vector on the left). A * B = "transform by A first, then B".

## Left-Handed Coordinate System
- **Decision**: +X right, +Y up, +Z forward.
- **Rationale**: DX12 native. Matches the existing fly camera code and look_at convention.
- **Perspective**: Left-handed, depth [0,1]. Mat4::perspective() uses LH formula.
- **look_at**: forward = normalize(target - eye), then cross(worldUp, fwd) for right.

## Vec3 is 12 Bytes (No Padding)
- **Decision**: Vec3 has NO _pad field, sizeof(Vec3) == 12.
- **Rationale**: Matches GPU vertex layout (DXGI_FORMAT_R32G32B32_FLOAT = 12 bytes). Avoids wasted memory in large vertex/point arrays. The backup had 16-byte padded Vec3 for SIMD alignment, but that complicates GPU data layout.
- **Trade-off**: SIMD vectorization of Vec3 arrays needs explicit handling (load 3 floats + mask). Worth it for correct GPU stride.

## AABB Field Names
- **Decision**: AABB uses `min_pt` and `max_pt` (not `min`/`max`).
- **Rationale**: Avoids collision with std::min/std::max and the uz::min/uz::max free functions. The backup used just `.min`/`.max` which can cause ambiguity.

## No SIMD Dispatching in math.h
- **Decision**: Pure scalar implementation. No #ifdef UZ_SIMD blocks.
- **Rationale**: Keep the canonical math header simple and portable. SIMD acceleration lives in a separate simd_math.h and is opt-in. The backup had interleaved SIMD/scalar paths which made the code harder to audit.

## Frustum Plane Extraction
- **Decision**: Row-major Griggs-Hartmann method. Planes extracted from row combinations.
- **Reference**: "Fast Extraction of Viewing Frustum Planes from the World-View-Projection Matrix" (Griggs & Hartmann, 2001).

## Quat::from_euler Convention
- **Rotation order**: Z * Y * X (roll, then yaw, then pitch). Intrinsic rotations.
- **Arguments**: from_euler(pitch, yaw, roll) — pitch=X, yaw=Y, roll=Z.

## Arena Class vs Free Functions
- **Decision**: Class-based API (arena.alloc(), arena.reset()) instead of free functions (arena_alloc(&arena, ...)).
- **Rationale**: The user spec explicitly requested a class with unique_ptr. Cleaner RAII. The backup used C-style free functions with a POD struct — that pattern is preserved in the backup but the new engine uses C++ idioms.
- **Move semantics**: Arena is move-only (non-copyable). Transfer ownership cleanly.
