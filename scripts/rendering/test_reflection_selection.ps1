param([string]$Label = 'after')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/engine/renderer/frame/frame_lighting_pass.cpp')
$assignment = [regex]::Match($source, 'lightDesc\.envMisSrvIndex\s*=\s*[\s\S]*?;').Value
if (-not $assignment) { throw 'Production environment reflection selector not found.' }
$artifactDir = Join-Path $PSScriptRoot 'test_reflection_selection_artifacts'
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$code = @'
using uint32_t = unsigned;
constexpr uint32_t UINT32_MAX = 0xffffffffu;
struct EnvMIS { bool ready; bool IsInitialized() const { return ready; } uint32_t GetAccumulatedSrv() const { return 17; } };
uint32_t Select(bool present, bool ready, bool local) {
    EnvMIS pass{ready};
    EnvMIS* m_envMISPass = present ? &pass : nullptr;
    struct { struct { unsigned sdfReflectionsEnabled; } perFrame; } ctx{{local ? 1u : 0u}};
    struct { uint32_t envMisSrvIndex; } lightDesc{};
    PRODUCTION_ASSIGNMENT
    return lightDesc.envMisSrvIndex;
}
int main() {
    int failures = 0;
    for (unsigned present = 0; present < 2; ++present)
        for (unsigned ready = 0; ready < 2; ++ready)
            for (unsigned local = 0; local < 2; ++local) {
                const auto expected = present && ready && !local ? 17u : UINT32_MAX;
                if (Select(present != 0, ready != 0, local != 0) != expected) ++failures;
            }
    return failures;
}
'@
$cppPath = Join-Path $artifactDir ($Label + '.cpp')
$exePath = Join-Path $artifactDir ($Label + '.exe')
[IO.File]::WriteAllText($cppPath, $code.Replace('PRODUCTION_ASSIGNMENT', $assignment))
$zig = (Get-Command zig -ErrorAction SilentlyContinue).Source
if (-not $zig) {
    $zig = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $zig) { throw 'Zig compiler not found.' }
& $zig c++ -std=c++23 -O0 $cppPath -o $exePath
if ($LASTEXITCODE -ne 0) { throw 'Reflection selector harness compilation failed.' }
& $exePath
$failures = $LASTEXITCODE
$result = "Reflection selection: $((8 - $failures))/8 cases passed; production assignment extracted from frame_lighting_pass.cpp."
$result | Set-Content -LiteralPath (Join-Path $artifactDir ($Label + '.log'))
Write-Output $result
exit $failures
