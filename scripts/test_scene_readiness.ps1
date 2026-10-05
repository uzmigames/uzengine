param([string]$Label = 'after')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/engine/renderer/scene/scene_mesh_assets.cpp')
$body = [regex]::Match($source, 'bool SceneRenderer::IsSceneFullyLoaded\(\) const \{([\s\S]*?)\n\}').Groups[1].Value
if (-not $body) { throw 'Production readiness function not found.' }
$artifactDir = Join-Path $PSScriptRoot 'test_scene_readiness_artifacts'
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$code = @'
struct Queue { unsigned count = 0; bool empty() const { return count == 0; } };
struct Decoder { unsigned count = 0; unsigned PendingCount() const { return count; } };
struct BakeQueue { unsigned count = 0; unsigned InFlight() const { return count; } };
struct SceneRenderer {
    Queue m_pendingMeshPaths, m_pendingProceduralMeshes, m_pendingTexturePaths;
    Queue m_deferredDecoded, m_pendingCopyUploads, m_asyncPendingSlots;
    Queue m_deferredMeshUploads;
    unsigned m_textureBatchCount = 0, m_copyFenceValue = 0, m_budgetDeferredMeshCount = 0;
    Decoder* m_asyncDecoder = nullptr;
    BakeQueue* m_sdfBakeQueue = nullptr;
    bool IsSceneFullyLoaded() const { PRODUCTION_BODY }
};
int main() {
    int failures = 0;
    SceneRenderer scene;
    if (!scene.IsSceneFullyLoaded()) ++failures;
    Queue SceneRenderer::* queues[] = {
        &SceneRenderer::m_pendingMeshPaths, &SceneRenderer::m_pendingProceduralMeshes,
        &SceneRenderer::m_pendingTexturePaths, &SceneRenderer::m_deferredDecoded,
        &SceneRenderer::m_pendingCopyUploads, &SceneRenderer::m_asyncPendingSlots,
        &SceneRenderer::m_deferredMeshUploads };
    for (auto queue : queues) {
        (scene.*queue).count = 1;
        if (scene.IsSceneFullyLoaded()) ++failures;
        (scene.*queue).count = 0;
    }
    unsigned SceneRenderer::* counts[] = { &SceneRenderer::m_textureBatchCount,
        &SceneRenderer::m_copyFenceValue, &SceneRenderer::m_budgetDeferredMeshCount };
    for (auto count : counts) {
        scene.*count = 1;
        if (scene.IsSceneFullyLoaded()) ++failures;
        scene.*count = 0;
    }
    Decoder decoder{1};
    scene.m_asyncDecoder = &decoder;
    if (scene.IsSceneFullyLoaded()) ++failures;
    decoder.count = 0;
    if (!scene.IsSceneFullyLoaded()) ++failures;
    BakeQueue bakes{1};
    scene.m_sdfBakeQueue = &bakes;
    if (scene.IsSceneFullyLoaded()) ++failures;
    bakes.count = 0;
    if (!scene.IsSceneFullyLoaded()) ++failures;
    return failures;
}
'@
$cppPath = Join-Path $artifactDir ($Label + '.cpp')
$exePath = Join-Path $artifactDir ($Label + '.exe')
[IO.File]::WriteAllText($cppPath, $code.Replace('PRODUCTION_BODY', $body))
$zig = (Get-Command zig -ErrorAction SilentlyContinue).Source
if (-not $zig) {
    $zig = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $zig) { throw 'Zig compiler not found.' }
& $zig c++ -std=c++23 -O0 $cppPath -o $exePath
if ($LASTEXITCODE -ne 0) { throw 'Readiness harness compilation failed.' }
& $exePath
$failures = $LASTEXITCODE
$result = "Scene readiness: $((14 - $failures))/14 cases passed; extracted production function."
$result | Set-Content -LiteralPath (Join-Path $artifactDir ($Label + '.log'))
Write-Output $result
exit $failures
