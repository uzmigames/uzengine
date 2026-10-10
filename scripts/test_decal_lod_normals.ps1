# Audits (and optionally repairs) per-LOD winding consistency in .uzmesh assets
# — phase0_sponza-followup-04.
#
# The auto-LOD simplifier (`runtime/engine/renderer/mesh/auto_lod.h`) feeds
# meshopt_simplify **positions only**, so a collapse can leave a triangle whose
# winding no longer agrees with its vertices' normals. LOD0/LOD1 of the
# repaired Sponza decal are consistent; LOD2/LOD3 are not, which is what the
# SponzaTest handoff reported ("some LOD2/3 triangles remain inconsistent").
#
# Convention: the engine renders clockwise-front (D3D default), so a triangle
# is consistent when `dot(-cross(b-a, c-a), avg(vertexNormals)) > 0`.
#
#   -Repair   swaps the last two indices of every inconsistent triangle,
#             which flips its winding to match the vertex normals. Vertex data
#             is never touched.
#
# Exits non-zero when any LOD still has inconsistent triangles (audit mode).

#   -All      scans every .uzmesh under sample/content/meshes/sponza and reports
#             (or, with -Repair, fixes) only meshes whose LOD0 is already
#             consistent. A mesh whose LOD0 is itself inconsistent has authored
#             or smoothed normals that legitimately disagree with some faces —
#             that is not a simplifier artifact and is left alone.
param(
    [string]$Mesh = 'sample/content/meshes/sponza/decals_1st_floor1_dirt_decal_0.uzmesh',
    [switch]$Repair,
    [switch]$All
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Test-MeshLodNormals([string]$path, [bool]$doRepair, [bool]$quiet) {
$bytes = [System.IO.File]::ReadAllBytes($path)

# MeshHeader (128 bytes, runtime/engine/renderer/mesh/mesh.h)
$magic       = [BitConverter]::ToUInt32($bytes, 0)
if ($magic -ne 0x484D5A55) { return $null }
$vertexStride = [BitConverter]::ToUInt32($bytes, 12)
$vertexCount  = [BitConverter]::ToUInt32($bytes, 16)
$indexFormat  = [BitConverter]::ToUInt32($bytes, 20)   # 1 = UInt32
$indexCount   = [BitConverter]::ToUInt32($bytes, 24)
$vertexOffset = [BitConverter]::ToUInt64($bytes, 56)
$indexOffset  = [BitConverter]::ToUInt64($bytes, 64)
$lodCount     = [BitConverter]::ToUInt32($bytes, 72)

$indexStride = if ($indexFormat -eq 1) { 4 } else { 2 }

# Vertex positions + normals (Vertex = pos[3], normal[3], uv[2], tangent[4] = 48B)
$px = New-Object double[] $vertexCount; $py = New-Object double[] $vertexCount; $pz = New-Object double[] $vertexCount
$nx = New-Object double[] $vertexCount; $ny = New-Object double[] $vertexCount; $nz = New-Object double[] $vertexCount
for ($i = 0; $i -lt $vertexCount; $i++) {
    $o = [int]$vertexOffset + $i * [int]$vertexStride
    $px[$i] = [BitConverter]::ToSingle($bytes, $o);      $py[$i] = [BitConverter]::ToSingle($bytes, $o + 4);  $pz[$i] = [BitConverter]::ToSingle($bytes, $o + 8)
    $nx[$i] = [BitConverter]::ToSingle($bytes, $o + 12); $ny[$i] = [BitConverter]::ToSingle($bytes, $o + 16); $nz[$i] = [BitConverter]::ToSingle($bytes, $o + 20)
}

function Get-Index([int]$byteOffset) {
    if ($indexStride -eq 4) { return [int][BitConverter]::ToUInt32($bytes, $byteOffset) }
    return [int][BitConverter]::ToUInt16($bytes, $byteOffset)
}
function Set-Index([int]$byteOffset, [int]$value) {
    $raw = if ($indexStride -eq 4) { [BitConverter]::GetBytes([uint32]$value) } else { [BitConverter]::GetBytes([uint16]$value) }
    [Array]::Copy($raw, 0, $bytes, $byteOffset, $indexStride)
}

# LOD ranges: LOD0 is (indexOffset, indexCount); LOD1..3 come from the header table.
$ranges = @([pscustomobject]@{ Level = 0; ByteOffset = [int]$indexOffset; Count = [int]$indexCount })
for ($l = 0; $l -lt 3; $l++) {
    $off = [BitConverter]::ToUInt32($bytes, 76 + $l * 8)
    $cnt = [BitConverter]::ToUInt32($bytes, 80 + $l * 8)
    if ($cnt -gt 0) {
        $ranges += [pscustomobject]@{ Level = $l + 1; ByteOffset = [int]$indexOffset + [int]$off; Count = [int]$cnt }
    }
}

$totalBad = 0
$repaired = 0
$perLod = @()
foreach ($r in $ranges) {
    $tris = 0; $bad = 0; $degen = 0
    for ($t = 0; $t + 2 -lt $r.Count; $t += 3) {
        $base = $r.ByteOffset + $t * $indexStride
        $ia = Get-Index $base
        $ib = Get-Index ($base + $indexStride)
        $ic = Get-Index ($base + 2 * $indexStride)
        if ($ia -ge $vertexCount -or $ib -ge $vertexCount -or $ic -ge $vertexCount) { continue }
        $tris++
        $ux = $px[$ib] - $px[$ia]; $uy = $py[$ib] - $py[$ia]; $uz = $pz[$ib] - $pz[$ia]
        $vx = $px[$ic] - $px[$ia]; $vy = $py[$ic] - $py[$ia]; $vz = $pz[$ic] - $pz[$ia]
        # Clockwise-front: the outward face normal is -(u x v).
        $fx = -($uy * $vz - $uz * $vy); $fy = -($uz * $vx - $ux * $vz); $fz = -($ux * $vy - $uy * $vx)
        $flen = [math]::Sqrt($fx * $fx + $fy * $fy + $fz * $fz)
        if ($flen -lt 1e-12) { $degen++; continue }
        $ax = ($nx[$ia] + $nx[$ib] + $nx[$ic]) / 3.0
        $ay = ($ny[$ia] + $ny[$ib] + $ny[$ic]) / 3.0
        $az = ($nz[$ia] + $nz[$ib] + $nz[$ic]) / 3.0
        $alen = [math]::Sqrt($ax * $ax + $ay * $ay + $az * $az)
        if ($alen -lt 1e-9) { continue }
        $dot = ($fx * $ax + $fy * $ay + $fz * $az) / ($flen * $alen)
        if ($dot -lt 0.0) {
            $bad++
            if ($doRepair) {
                Set-Index ($base + $indexStride) $ic
                Set-Index ($base + 2 * $indexStride) $ib
                $repaired++
            }
        }
    }
    $pct = if ($tris -gt 0) { $bad / $tris } else { 0 }
    if (-not $quiet) {
        '{0,-6} triangles={1,6} inconsistent={2,5} ({3,7:P2}) degenerate={4}' -f ("LOD$($r.Level)"), $tris, $bad, $pct, $degen
    }
    $perLod += $bad
    $totalBad += $bad
}

if ($doRepair -and $repaired -gt 0) {
    [System.IO.File]::WriteAllBytes($path, $bytes)
}
return [pscustomobject]@{ Total = $totalBad; Lod0 = $perLod[0]; Higher = ($totalBad - $perLod[0])
                          Repaired = $repaired; Levels = $ranges.Count }
}

if ($All) {
    $meshes = Get-ChildItem -LiteralPath (Join-Path $root 'sample/content/meshes/sponza') -Filter '*.uzmesh' | Sort-Object Name
    $clean = 0; $authored = 0; $lodOnly = 0; $fixed = 0; $skipped = 0
    foreach ($m in $meshes) {
        $r = Test-MeshLodNormals $m.FullName $false $true
        if ($null -eq $r) { $skipped++; continue }
        if ($r.Total -eq 0) { $clean++; continue }
        if ($r.Lod0 -gt 0) {
            $authored++
            Write-Output ("  skipped (LOD0 itself inconsistent, authored normals): {0} — LOD0={1}" -f $m.Name, $r.Lod0)
            continue
        }
        $lodOnly++
        if ($Repair) {
            $rr = Test-MeshLodNormals $m.FullName $true $true
            $fixed += $rr.Repaired
            Write-Output ("  repaired {0,5} triangle(s): {1}" -f $rr.Repaired, $m.Name)
        } else {
            Write-Output ("  simplifier artifact {0,5} triangle(s): {1}" -f $r.Higher, $m.Name)
        }
    }
    Write-Output ("Sponza LOD normals: {0} clean, {1} simplifier-artifact, {2} authored-inconsistent, {3} unreadable." -f `
        $clean, $lodOnly, $authored, $skipped)
    if ($Repair) { Write-Output ("Repaired {0} triangle(s) across {1} mesh(es)." -f $fixed, $lodOnly); exit 0 }
    if ($lodOnly -gt 0) { exit 1 }
    exit 0
}

$path = Join-Path $root $Mesh
if (-not (Test-Path -LiteralPath $path)) { throw "Mesh not found: $path" }
$result = Test-MeshLodNormals $path ([bool]$Repair) $false
if ($null -eq $result) { throw 'Not a UZMH mesh.' }
if ($Repair -and $result.Repaired -gt 0) {
    Write-Output ("Repaired {0} triangle(s) by swapping winding; vertex data untouched." -f $result.Repaired)
    Write-Output 'Re-run without -Repair to confirm.'
    exit 0
}
Write-Output ("Decal LOD normals: {0} inconsistent triangle(s) across {1} LOD level(s) of {2}." -f `
    $result.Total, $result.Levels, (Split-Path $Mesh -Leaf))
if ($result.Total -gt 0) { exit 1 }
exit 0
