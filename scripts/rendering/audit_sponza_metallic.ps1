# Audits what feeds `PBROutput.Metallic` in every Sponza material
# (phase0_sponza-followup-05).
#
# The graphs follow the glTF metallicRoughness convention correctly —
# `Metallic <- <tex>.B`, `Roughness <- <tex>.G` — so the finding is about the
# texture data, not the wiring: the stone/brick metallicRoughness maps carry a
# small but non-zero blue channel, which makes rough stone read as partially
# metallic (F0 lerps toward the base colour and diffuse energy drops).
#
#   -Apply   also writes `params.metallicMultiplier = 0` for every material
#            flagged DIRTY. `gbuffer_ps.hlsl` does `metallic *= metallicMultiplier`
#            when a metallic texture is bound, so this zeroes metallic without
#            touching the graph topology.
#
# Classification of the mean sampled channel (0..1):
#   < 0.02          CLEAN  — already non-metal
#   0.02 .. 0.35    DIRTY  — a dielectric with leftover metallic data
#   >= 0.35         METAL  — treated as intentionally metallic, left alone

param(
    [switch]$Apply,
    [double]$DirtyFloor = 0.02,
    [double]$MetalFloor = 0.35
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root      = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$matDir    = Join-Path $root 'sample/content/materials/sponza'
$contentRt = Join-Path $root 'sample/content'

function Get-ChannelStats([string]$path, [int]$channel) {
    # channel: 0 = R, 1 = G, 2 = B, 3 = A (A falls back to R when absent)
    $bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $path))
    try {
        $w = $bmp.Width; $h = $bmp.Height
        $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
        $data = $bmp.LockBits($rect, 'ReadOnly', 'Format24bppRgb')
        $bytes = New-Object byte[] ($data.Stride * $h)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        $bmp.UnlockBits($data)
        # Format24bppRgb is BGR in memory: R=+2, G=+1, B=+0
        $offset = switch ($channel) { 0 { 2 } 1 { 1 } 2 { 0 } default { 2 } }
        $sum = 0.0; $n = 0; $above = 0
        for ($y = 0; $y -lt $h; $y += 4) {
            $row = $y * $data.Stride
            for ($x = 0; $x -lt $w; $x += 4) {
                $v = $bytes[$row + $x * 3 + $offset]
                $sum += $v; $n++
                if ($v -gt 25) { $above++ }
            }
        }
        return [pscustomobject]@{ Mean = ($sum / $n / 255.0); AboveTenth = ($above / $n) }
    } finally { $bmp.Dispose() }
}

$pinChannel = @{ 'R' = 0; 'G' = 1; 'B' = 2; 'A' = 3; 'RGBA' = 0 }
$rows = @()

foreach ($file in Get-ChildItem -LiteralPath $matDir -Filter '*.uzmat' | Sort-Object Name) {
    $graph = Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json
    $out = $graph.expressions | Where-Object { $_.type -eq 'PBROutput' } | Select-Object -First 1
    $metallicInput = $null
    if ($out) { $metallicInput = $out.inputs | Where-Object { $_.dstPin -eq 'Metallic' } | Select-Object -First 1 }

    $mult = 1.0
    if ($null -ne $graph.params -and $null -ne $graph.params.metallicMultiplier) {
        $mult = [double]$graph.params.metallicMultiplier
    }

    if (-not $metallicInput) {
        # No Metallic input at all. gbuffer_ps.hlsl:188 then takes the
        # multiplier *directly* (`metallic = metallicMultiplier`), so a
        # material left at the neutral 1.0 renders fully metallic. The
        # pre-port hand calibration had 0.0 on exactly these.
        $rows += [pscustomobject]@{ Material = $file.BaseName; Source = '(none)'; Pin = ''
                                    Mean = $mult; AboveTenth = 0.0; Multiplier = $mult
                                    Verdict = $(if ($mult -lt $DirtyFloor) { 'CLEAN' } else { 'DIRTY' }) }
        continue
    }

    $src = $graph.expressions | Where-Object { $_.id -eq $metallicInput.srcExpr } | Select-Object -First 1
    if (-not $src -or $src.type -ne 'TextureSample') {
        $rows += [pscustomobject]@{ Material = $file.BaseName; Source = "expr$($metallicInput.srcExpr)"
                                    Pin = $metallicInput.srcPin; Mean = [double]::NaN; AboveTenth = 0.0
                                    Multiplier = $mult; Verdict = 'NON-TEXTURE' }
        continue
    }

    $texRel = $src.stringParams.texturePath
    $texAbs = Join-Path $contentRt $texRel
    if (-not (Test-Path -LiteralPath $texAbs)) {
        $rows += [pscustomobject]@{ Material = $file.BaseName; Source = $texRel; Pin = $metallicInput.srcPin
                                    Mean = [double]::NaN; AboveTenth = 0.0; Multiplier = $mult; Verdict = 'MISSING-TEX' }
        continue
    }

    $stats = Get-ChannelStats $texAbs $pinChannel[$metallicInput.srcPin]
    $effective = $stats.Mean * $mult
    $verdict = if ($effective -lt $DirtyFloor) { 'CLEAN' }
               elseif ($effective -ge $MetalFloor) { 'METAL' }
               else { 'DIRTY' }

    $rows += [pscustomobject]@{ Material = $file.BaseName; Source = (Split-Path $texRel -Leaf)
                                Pin = $metallicInput.srcPin; Mean = $stats.Mean
                                AboveTenth = $stats.AboveTenth; Multiplier = $mult; Verdict = $verdict }
}

$rows | Format-Table -AutoSize @{L='Material';E={$_.Material}}, @{L='MetallicSource';E={$_.Source}},
    @{L='Pin';E={$_.Pin}}, @{L='Mean';E={ if ([double]::IsNaN($_.Mean)) { 'n/a' } else { '{0:N3}' -f $_.Mean }}},
    @{L='>0.1';E={ '{0:P0}' -f $_.AboveTenth }}, @{L='Mult';E={ '{0:N2}' -f $_.Multiplier }}, @{L='Verdict';E={$_.Verdict}}

$dirty = @($rows | Where-Object { $_.Verdict -eq 'DIRTY' })
Write-Output ("Sponza metallic audit: {0} materials; {1} DIRTY, {2} CLEAN, {3} METAL." -f `
    $rows.Count, $dirty.Count, @($rows | Where-Object { $_.Verdict -eq 'CLEAN' }).Count,
    @($rows | Where-Object { $_.Verdict -eq 'METAL' }).Count)

if ($Apply) {
    foreach ($row in $dirty) {
        $path = Join-Path $matDir ($row.Material + '.uzmat')
        $text = Get-Content -Raw -LiteralPath $path
        $patched = [regex]::Replace($text, '("metallicMultiplier"\s*:\s*)[0-9.]+', '${1}0.0')
        if ($patched -eq $text) { throw "metallicMultiplier not found in $($row.Material).uzmat" }
        [IO.File]::WriteAllText($path, $patched)
        if ($row.Source -eq '(none)') {
            Write-Output ("  zeroed metallicMultiplier: {0} (no Metallic input - rendered fully metallic at {1:N2})" -f `
                $row.Material, $row.Multiplier)
        } else {
            Write-Output ("  zeroed metallicMultiplier: {0} (was reading {1:N3} from {2}.{3})" -f `
                $row.Material, $row.Mean, $row.Source, $row.Pin)
        }
    }
    Write-Output ("Applied to {0} material(s)." -f $dirty.Count)
} elseif ($dirty.Count -gt 0) {
    Write-Output 'Re-run with -Apply to zero metallicMultiplier on the DIRTY materials.'
}

exit 0
