# Shader entry point is ALWAYS `main` — never PSMain/VSMain/CSMain

UzEngine's ShaderManager compiles all HLSL with `entryPoint = "main"`.

## Rule

Every shader function that serves as the entry point MUST be named `main`:

```hlsl
// CORRECT
float4 main(FSInput input) : SV_Target { ... }
[numthreads(8,8,1)] void main(uint3 DTid : SV_DispatchThreadID) { ... }

// WRONG — will fail with "error: missing entry point definition"
float4 PSMain(FSInput input) : SV_Target { ... }
void VSMain(uint vid : SV_VertexID, ...) { ... }
[numthreads(8,8,1)] void CSMain(...) { ... }
```

## Why

ShaderManager::CreateShader() passes `"main"` to the DXC compiler. If the shader uses any other name, DXC cannot find the entry point and compilation fails silently at runtime, causing pipeline creation failure and GPU crashes.

## Applies to ALL agents writing shaders

- shader-engineer
- render-graph-engineer
- rhi-engineer
- cpp-core-expert (when writing inline HLSL)

Tell them explicitly: **entry point = `main`**.
