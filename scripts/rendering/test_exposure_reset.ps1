$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/engine/renderer/passes/postprocess/auto_exposure.cpp')
$body = [regex]::Match($source, 'void AutoExposure::ResetHistory\(IRHICommandList\* cmdList\) \{([\s\S]*?)\n\}').Groups[1].Value
if (-not $body) { throw 'Exposure history reset is missing.' }
$artifactDir = Join-Path $PSScriptRoot 'test_exposure_reset_artifacts'
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$code = @'
enum class RHIResourceState { UnorderedAccess, CopyDst };
struct RHITextureHandle { static unsigned Invalid() { return ~0u; } };
struct RHIBarrierDesc { unsigned buffer, texture; RHIResourceState stateBefore, stateAfter; };
struct IRHICommandList {
    unsigned calls = 0, failures = 0;
    void ResourceBarrier(const RHIBarrierDesc* b, unsigned count) {
        if (count != 1 || b->buffer != 2 || b->texture != ~0u) ++failures;
        const bool beforeCopy = calls == 0;
        if ((!beforeCopy && calls != 2) ||
            b->stateBefore != (beforeCopy ? RHIResourceState::UnorderedAccess : RHIResourceState::CopyDst) ||
            b->stateAfter != (beforeCopy ? RHIResourceState::CopyDst : RHIResourceState::UnorderedAccess)) ++failures;
        ++calls;
    }
    void CopyBuffer(unsigned src, unsigned srcOffset, unsigned dst, unsigned dstOffset, unsigned bytes) {
        if (calls != 1 || src != 1 || dst != 2 || srcOffset != 0 || dstOffset != 0 || bytes != 8) ++failures;
        ++calls;
    }
};
struct AutoExposure {
    bool m_initialized;
    unsigned m_zeroBuffer = 1, m_exposureBuffer = 2;
    static constexpr unsigned kExposureBytes = 8;
    void ResetHistory(IRHICommandList* cmdList) { PRODUCTION_BODY }
};
int main() {
    unsigned failures = 0;
    for (unsigned ready = 0; ready < 2; ++ready)
        for (unsigned present = 0; present < 2; ++present) {
            AutoExposure exposure{ready != 0};
            IRHICommandList list;
            exposure.ResetHistory(present ? &list : nullptr);
            if (list.failures || list.calls != (ready && present ? 3u : 0u)) ++failures;
        }
    return failures;
}
'@
$cppPath = Join-Path $artifactDir 'reset.cpp'
$exePath = Join-Path $artifactDir 'reset.exe'
[IO.File]::WriteAllText($cppPath, $code.Replace('PRODUCTION_BODY', $body))
$zig = (Get-Command zig -ErrorAction SilentlyContinue).Source
if (-not $zig) {
    $zig = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $zig) { throw 'Zig compiler not found.' }
& $zig c++ -std=c++23 -O0 $cppPath -o $exePath
if ($LASTEXITCODE -ne 0) { throw 'Exposure reset harness compilation failed.' }
& $exePath
$failures = $LASTEXITCODE
$result = "Exposure reset: $((4 - $failures))/4 cases passed; production command ordering and copy bounds."
$result | Set-Content -LiteralPath (Join-Path $artifactDir 'reset.log')
Write-Output $result
exit $failures
