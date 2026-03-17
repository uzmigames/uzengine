# ALWAYS Research Before Implementing — NEVER Guess

## Rule (applies to ALL agents)

**ABSOLUTELY FORBIDDEN:**
- Guessing at the cause of a bug
- "Trying things" to see if they work
- Hypothesizing without reading code
- Assuming how an API works without checking docs or UE5 source

## What To Do Instead

### For Rendering/RHI/Shader Bugs
1. **Read UE5 source FIRST** at `W:\UE_5.6_Source` — find the EXACT equivalent code
2. Compare UE5 implementation with UzEngine implementation line-by-line
3. Find the EXACT difference
4. Fix UzEngine to match UE5

### For API Questions (DX11, DX12, Vulkan)
1. **Search the UE5 codebase** for how they handle the same scenario
2. Read the actual API documentation (MSDN for DX11/DX12, Vulkan spec)
3. Look at the D3D11 Debug Layer output for specific error messages
4. Fix based on the documentation, not guesses

### For Unknown Issues
1. **Add diagnostic logging** to narrow down WHERE the failure occurs
2. **Read the actual data** — GPU readback, buffer contents, texture contents
3. **Use RenderDoc/PIX** to capture and inspect the exact GPU state
4. **Never iterate blindly** — each attempt must be informed by new data

## Why

The team has wasted HOURS guessing at the DX11 black screen cause:
- Guessed "reversed-Z depth test" — was already correct
- Guessed "cubemap UAV dimension" — was already correct
- Guessed "shader compilation" — was already correct
- Guessed "input layout" — was already correct

The actual cause (write-combined memory read-back) was only found by someone who UNDERSTOOD DX11 DYNAMIC buffer semantics, not by guessing.

## How to Apply

Before writing ANY fix:
1. State what you KNOW (from logs, debug output, code reading)
2. State what you DON'T KNOW
3. Research the unknown via UE5 source, docs, or diagnostic tools
4. Only THEN implement the fix

"I think this might be the problem" is NOT acceptable. "UE5 does X at file:line, we do Y, the difference causes Z" IS acceptable.
