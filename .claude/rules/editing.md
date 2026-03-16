# File Editing Rules

## Sequential Editing (MANDATORY)

Edit files ONE AT A TIME. Never call multiple Edit tools in parallel for different files.

```
CORRECT:  Edit A → confirm → Edit B → confirm → Edit C → confirm
WRONG:    Edit A, B, C simultaneously
```

## File Size

- Max 1500 lines per file (source, tests, headers, configs)
- Single responsibility per file
- Split proactively at ~1000 lines
- Tests mirror source structure

## Test Rules

- Complete, production-quality tests only
- No placeholder assertions (`expect(true).toBe(true)`)
- No skipped tests (`it.skip(...)`)
- 95%+ coverage on all new code
