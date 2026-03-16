# Architecture Rules

## Coordinate System
- Z-up, X-forward, left-handed (same as UE5)
- NEVER use Y-up when porting from external sources

## Units
| Category | Convention |
|----------|-----------|
| Distances | Centimeters (UE5 parity) |
| Angles (ECS) | Degrees |
| Angles (GPU) | Radians |
| Atmosphere | Kilometers |
| Color space | Linear RGB |
| Temperature | Kelvin |
| Time | Seconds |

## Engine APIs (MANDATORY)

| Use This | NOT This |
|----------|----------|
| `UZ_LOG(Category, Level, ...)` | `spdlog::info/error/warn` |
| `UZ_CHECK` / `UZ_ENSURE` | raw `assert()` |
| Engine allocators | raw `new`/`malloc` |
| `uz::GetExeDir()` | hardcoded paths |
| `UZ_PROFILE_SCOPE` | raw Tracy macros |

## Clean Code & SOLID

- Meaningful names, small functions (~40 lines max), no magic numbers
- Single Responsibility, Open/Closed, Liskov, Interface Segregation, Dependency Inversion
- Log at boundaries, profile critical paths, assert invariants
- Fail early, fail loud — never silently swallow errors

## Documentation Style

- Triple-slash `///` for public items with full parameter/return/error docs
- Section headers with `// ───` separators
- Inline comments ONLY for "why", never "what"
