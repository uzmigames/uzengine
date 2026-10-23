# setup.ps1 — UzEngine dependency setup script
# Downloads and configures all thirdparty dependencies for the runtime.
# Run from project root: .\scripts\setup.ps1
# Legacy vendor-refresh installer. For reproducible Windows baseline builds,
# use scripts/setup-native.ps1; it preserves tracked sources and verifies a lock.

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ThirdPartyDir = Join-Path $RootDir "runtime\thirdparty"
$TempDir = Join-Path $RootDir ".setup_temp"
# Shared with scripts/setup-native.ps1: prebuilt archives that both setup paths
# fetch are pinned (URL + SHA-256) in this one place.
$NativeLockPath = Join-Path $ScriptDir "native-dependencies.lock.json"

if (-not (Test-Path $ThirdPartyDir)) { New-Item -ItemType Directory -Path $ThirdPartyDir | Out-Null }
if (-not (Test-Path $TempDir)) { New-Item -ItemType Directory -Path $TempDir | Out-Null }

Write-Host "=== UzEngine Setup ===" -ForegroundColor Cyan
Write-Host "Thirdparty: $ThirdPartyDir"
Write-Host ""

# ─── Helpers ────────────────────────────────────────────────────────────────

function Invoke-Git {
    # Local "Continue": under the script-wide "Stop", Windows PowerShell 5.1
    # (what setup.bat launches) turns every git stderr line into a terminating
    # NativeCommandError, so exit codes could never be inspected.
    # Callers quote the pathspec separator ('--'): PowerShell consumes a bare
    # -- as its own end-of-parameters marker before it reaches $args.
    $ErrorActionPreference = "Continue"
    $output = & git @args 2>&1 | ForEach-Object { "$_" }
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = (($output | Out-String).Trim()) }
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-GitFileState {
    # "clean" when the file is tracked by this repo and identical to HEAD,
    # "modified" when tracked with uncommitted changes, otherwise "untracked".
    param([string]$Path)
    $tracked = Invoke-Git -C $RootDir ls-files --error-unmatch '--' $Path
    if ($tracked.ExitCode -ne 0) { return "untracked" }
    $diff = Invoke-Git -C $RootDir diff --quiet HEAD '--' $Path
    if ($diff.ExitCode -eq 0) { return "clean" }
    return "modified"
}

function Test-PinnedFile {
    # Decides whether an existing file may be kept instead of trusting it
    # blindly. $true: it matches the pinned SHA-256, or it is tracked by this
    # repo and identical to HEAD (committed local patches and autocrlf line
    # endings legitimately change the bytes; git history covers those).
    # $false: it is missing, or an untracked file that fails the pin, so the
    # caller downloads the pinned artifact over it. A tracked file with
    # uncommitted changes is never overwritten: that is someone's work.
    param([string]$Label, [string]$Path, [string]$Sha256)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $leaf = Split-Path -Leaf $Path
    $expected = $Sha256.ToLowerInvariant()
    $actual = Get-Sha256 -Path $Path
    if ($actual -eq $expected) {
        Write-Host "  [$Label] $leaf verified (sha256 $($expected.Substring(0, 12))), skipping." -ForegroundColor Green
        return $true
    }
    switch (Get-GitFileState -Path $Path) {
        "clean" {
            Write-Host "  [$Label] WARN: $leaf differs from pinned upstream sha256 $($expected.Substring(0, 12)) but matches the committed copy (local patch or line endings), skipping." -ForegroundColor DarkYellow
            return $true
        }
        "modified" {
            throw "[$Label] $Path has uncommitted changes and does not match pinned SHA-256 $expected. Commit or discard them, then rerun setup."
        }
        default {
            Write-Host "  [$Label] $leaf does not match pinned SHA-256 (expected $expected, found $actual), replacing with the pinned artifact..." -ForegroundColor Yellow
            return $false
        }
    }
}

function Download-File {
    # Every download is pinned. The bytes land in "<OutFile>.download" and are
    # moved into place only once the hash matches, so a failed or tampered
    # fetch never leaves a file that a later run would find and keep.
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$OutFile,
        [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$Sha256)
    $partial = "$OutFile.download"
    if (Test-Path -LiteralPath $partial) { Remove-Item -Force -LiteralPath $partial }
    Write-Host "  Downloading $Url..." -ForegroundColor Gray
    # -f makes curl exit non-zero on HTTP >= 400 instead of writing the error
    # body to disk, which would surface later as a corrupt-archive failure.
    curl.exe -fsSL -o $partial $Url
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -Force -LiteralPath $partial -ErrorAction SilentlyContinue
        throw "Failed to download $Url (curl exit $LASTEXITCODE)"
    }
    $expected = $Sha256.ToLowerInvariant()
    $actual = Get-Sha256 -Path $partial
    if ($actual -ne $expected) {
        Remove-Item -Force -LiteralPath $partial
        throw "SHA-256 mismatch for $Url (expected $expected, got $actual). The download was deleted."
    }
    Move-Item -Force -LiteralPath $partial -Destination $OutFile
}

function Get-LockedPackage {
    # Reads one package (url + sha256) from scripts/native-dependencies.lock.json.
    param([string]$Name)
    $lock = Get-Content -Raw -LiteralPath $NativeLockPath | ConvertFrom-Json
    $matched = @($lock.packages | Where-Object { $_.name -eq $Name })
    if ($matched.Count -ne 1) { throw "Expected exactly one '$Name' package in $NativeLockPath, found $($matched.Count)" }
    $pkg = $matched[0]
    if ($pkg.sha256 -notmatch '^[0-9a-f]{64}$' -or -not $pkg.url) {
        throw "Package '$Name' in $NativeLockPath lacks a url or a valid sha256"
    }
    return $pkg
}

function Set-PinnedCommit {
    # Moves a fresh clone to its pinned commit and proves HEAD is exactly it.
    # Branches move and tags can be re-pointed; the commit SHA cannot.
    param([string]$Name, [string]$Path, [string]$Ref, [string]$Commit)
    $short = $Commit.Substring(0, 12)
    $head = Invoke-Git -C $Path rev-parse HEAD
    if ($head.ExitCode -eq 0 -and $head.Output -ne $Commit) {
        Write-Host "  [$Name] $Ref is now at $($head.Output.Substring(0, 12)); checking out pinned $short..." -ForegroundColor Gray
    }
    # A --depth 1 clone only holds the ref's tip; fetch the pinned commit by SHA.
    $present = Invoke-Git -C $Path cat-file -e "$Commit^{commit}"
    if ($present.ExitCode -ne 0) {
        $fetch = Invoke-Git -C $Path fetch --quiet --depth 1 origin $Commit
        if ($fetch.ExitCode -ne 0) {
            Write-Host $fetch.Output -ForegroundColor DarkGray
            throw "[$Name] pinned commit $Commit could not be fetched from origin"
        }
    }
    $checkout = Invoke-Git -C $Path -c advice.detachedHead=false checkout --quiet --detach $Commit
    if ($checkout.ExitCode -ne 0) {
        Write-Host $checkout.Output -ForegroundColor DarkGray
        throw "[$Name] checkout of pinned commit $Commit failed"
    }
    $verify = Invoke-Git -C $Path rev-parse HEAD
    if ($verify.ExitCode -ne 0 -or $verify.Output -ne $Commit) {
        throw "[$Name] HEAD is '$($verify.Output)' after checkout, expected pinned commit $Commit"
    }
    Write-Host "  [$Name] verified HEAD $short" -ForegroundColor Green
}

function Confirm-ExistingCheckout {
    # Verifies an existing dependency directory instead of trusting it.
    param([string]$Name, [string]$Path, [string]$Commit)
    $short = $Commit.Substring(0, 12)
    if (Test-Path -LiteralPath (Join-Path $Path ".git")) {
        $head = Invoke-Git -C $Path rev-parse HEAD
        if ($head.ExitCode -ne 0 -or $head.Output -ne $Commit) {
            throw "[$Name] existing checkout $Path is at '$($head.Output)', expected pinned commit $Commit. Delete the directory and rerun setup."
        }
        Write-Host "  [$Name] already exists at pinned commit $short, skipping." -ForegroundColor Green
        return
    }
    $index = Invoke-Git -C $RootDir ls-files -s '--' $Path
    $entries = @($index.Output -split "`r?`n" | Where-Object { $_ })
    if ($index.ExitCode -ne 0 -or $entries.Count -eq 0) {
        Write-Host "  [$Name] WARN: exists without .git and is not tracked by this repo, so it cannot be verified against $short. Delete it to re-clone the pinned commit." -ForegroundColor DarkYellow
        return
    }
    if ($entries.Count -eq 1 -and $entries[0] -match '^160000 ([0-9a-f]{40}) ') {
        if ($Matches[1] -ne $Commit) {
            throw "[$Name] the git index records gitlink commit $($Matches[1]) but setup pins $Commit"
        }
        Write-Host "  [$Name] WARN: gitlink matches pinned $short, but the checkout has no .git so its files cannot be verified; skipping." -ForegroundColor DarkYellow
        return
    }
    $status = Invoke-Git -C $RootDir status --porcelain '--' $Path
    if ($status.ExitCode -eq 0 -and $status.Output) {
        $changed = @($status.Output -split "`r?`n").Count
        Write-Host "  [$Name] WARN: vendored tree has $changed uncommitted change(s) relative to this repo's git history (upstream $short); skipping." -ForegroundColor DarkYellow
        return
    }
    Write-Host "  [$Name] already exists (vendored tree without .git, covered by this repo's git history; upstream $short), skipping." -ForegroundColor DarkYellow
}

function Clone-Repo {
    param(
        [string]$Name, [string]$Url, [string]$Branch,
        [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$Commit,
        [string[]]$KeepDirs, [string[]]$RemoveDirs)
    $Commit = $Commit.ToLowerInvariant()
    $path = Join-Path $ThirdPartyDir $Name
    if ((Test-Path $path) -and (Get-ChildItem $path | Measure-Object).Count -gt 0) {
        Confirm-ExistingCheckout -Name $Name -Path $path -Commit $Commit
        return
    }
    if (Test-Path $path) { Remove-Item -Recurse -Force $path }
    Write-Host "  [$Name] cloning $Branch @ $($Commit.Substring(0, 12))..." -ForegroundColor Yellow
    # $path is quoted: the repo may live under a directory containing spaces
    # (e.g. C:\Users\First Last\...), which git would otherwise parse as extra
    # positional arguments and reject with "Too many arguments".
    $cloneOutput = cmd /c "git clone --depth 1 -b ""$Branch"" ""$Url"" ""$path"" 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host $cloneOutput -ForegroundColor DarkGray
        Write-Error "Failed to clone $Name"
    }
    try {
        Set-PinnedCommit -Name $Name -Path $path -Ref $Branch -Commit $Commit
    } catch {
        # Never leave an unverified tree behind for the next run to find.
        Remove-Item -Recurse -Force $path -ErrorAction SilentlyContinue
        throw
    }

    # Remove .git
    $gitDir = Join-Path $path ".git"
    if (Test-Path $gitDir) { Remove-Item -Recurse -Force $gitDir }

    # Remove specific dirs if specified
    if ($RemoveDirs) {
        foreach ($dir in $RemoveDirs) {
            $p = Join-Path $path $dir
            if (Test-Path $p) { Remove-Item -Recurse -Force $p }
        }
    }
}

function Download-Extract-Zip {
    # -VerifyFile: relative path of an artifact that proves the package is
    # complete. Source trees are committed to git but *.dll/*.lib/*.exe are
    # gitignored, so a fresh clone leaves these directories present but
    # missing every binary. Testing the directory alone would skip forever.
    # -Sha256 pins the archive; -VerifyFileSha256 pins that artifact, so an
    # existing extraction is verified rather than trusted.
    param(
        [string]$Name, [string]$Url,
        [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$Sha256,
        [string]$SubPath, [string]$VerifyFile,
        [ValidatePattern('^[0-9a-fA-F]{64}$')][string]$VerifyFileSha256)
    if ($VerifyFile -and -not $VerifyFileSha256) { throw "[$Name] -VerifyFile requires -VerifyFileSha256" }
    $path = Join-Path $ThirdPartyDir $Name
    if ($VerifyFile) {
        $probe = Join-Path $path $VerifyFile
        if (Test-PinnedFile -Label $Name -Path $probe -Sha256 $VerifyFileSha256) { return }
        if ((Test-Path $path) -and -not (Test-Path $probe)) {
            Write-Host "  [$Name] incomplete (missing $VerifyFile), re-extracting..." -ForegroundColor Yellow
        }
    } elseif (Test-Path $path) {
        Write-Host "  [$Name] WARN: already exists and has no -VerifyFile to check, skipping." -ForegroundColor DarkYellow
        return
    }
    $zip = Join-Path $TempDir "$Name.zip"
    Write-Host "  [$Name] downloading..." -ForegroundColor Yellow
    Download-File -Url $Url -OutFile $zip -Sha256 $Sha256

    if ($SubPath) {
        $tempExtract = Join-Path $TempDir "${Name}_temp"
        Expand-Archive -Path $zip -DestinationPath $tempExtract -Force
        New-Item -ItemType Directory -Path $path -Force | Out-Null
        $src = Join-Path $tempExtract $SubPath
        Copy-Item -Path "$src\*" -Destination $path -Recurse -Force
        Remove-Item -Recurse -Force $tempExtract
    } else {
        Expand-Archive -Path $zip -DestinationPath $path -Force
    }
    Remove-Item $zip
    if ($VerifyFile) {
        $actual = Get-Sha256 -Path $probe
        if ($actual -ne $VerifyFileSha256.ToLowerInvariant()) {
            throw "[$Name] extracted $VerifyFile has SHA-256 $actual, expected $($VerifyFileSha256.ToLowerInvariant())"
        }
    }
}

function Apply-Patches {
    param([string]$Name)
    $patchDir = Join-Path $ThirdPartyDir "$Name\patches"
    if (-not (Test-Path $patchDir)) { return }
    $patches = Get-ChildItem $patchDir -Filter "*.patch" | Sort-Object Name
    if ($patches.Count -eq 0) { return }
    # Apply from repo root — git apply works on the working tree even for non-git subdirs
    Push-Location $RootDir
    try {
        foreach ($patch in $patches) {
            # Probe: if reverse-apply succeeds, the patch is already in the working tree
            # (typically because upstream merged the same fix). Skip silently.
            & git apply --reverse --check $patch.FullName 2>$null
            if ($LASTEXITCODE -eq 0) {
                Write-Host "  [$Name] already applied: $($patch.Name)" -ForegroundColor DarkGray
                continue
            }
            Write-Host "  [$Name] applying $($patch.Name)..." -ForegroundColor Magenta
            & git apply --ignore-whitespace $patch.FullName 2>$null
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  [$Name] WARN: patch did not apply cleanly: $($patch.Name)" -ForegroundColor Yellow
            }
        }
    } finally {
        Pop-Location
    }
}

function Download-Header {
    param(
        [string]$Name, [string]$Url, [string]$FileName,
        [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$Sha256)
    $dir = Join-Path $ThirdPartyDir $Name
    $file = Join-Path $dir $FileName
    if (Test-PinnedFile -Label $Name -Path $file -Sha256 $Sha256) { return }
    Write-Host "  [$Name] downloading header..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Download-File -Url $Url -OutFile $file -Sha256 $Sha256
}

# ─── Prerequisites ──────────────────────────────────────────────────────────

Write-Host "[1/8] Checking prerequisites..." -ForegroundColor Cyan

# Refresh PATH from registry (picks up winget/installer changes without restarting terminal)
function Refresh-Path {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machinePath;$userPath"
}

# Zig — setup installs 0.15.x. build.zig also builds with 0.16: it selects the
# std.fs (0.15) or std.Io.Dir (0.16) directory API at compile time.
$ZigPinnedVersion = "0.15.2"
$ZigToolsDir = Join-Path $RootDir ".tools\zig-x86_64-windows-$ZigPinnedVersion"
# Archive hash = the "shasum" published in https://ziglang.org/download/index.json;
# zig.exe hash taken from that verified archive.
$ZigZipSha256 = "3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c"
$ZigExeSha256 = "d408dd38eed3e5204af841bcebf70502a4dbbb8399a3a3262be55059370bc018"

function Test-ZigVersionOk {
    param([string]$Cmd)
    try {
        $v = & $Cmd version 2>$null
        if ($LASTEXITCODE -eq 0 -and $v -match "^0\.15\.") { return $v }
    } catch {}
    return $null
}

# 1. Check existing zig in PATH
$systemZigVer = $null
if (Get-Command zig -ErrorAction SilentlyContinue) {
    $systemZigVer = Test-ZigVersionOk "zig"
}
if (-not $systemZigVer) { Refresh-Path; if (Get-Command zig -ErrorAction SilentlyContinue) { $systemZigVer = Test-ZigVersionOk "zig" } }

# 2. Check pinned local zig (verified by hash, not just presence)
$localZigExe = Join-Path $ZigToolsDir "zig.exe"

if ($systemZigVer) {
    Write-Host "  Zig: $systemZigVer (system)" -ForegroundColor Green
} elseif (Test-PinnedFile -Label "zig" -Path $localZigExe -Sha256 $ZigExeSha256) {
    $localVer = & $localZigExe version
    Write-Host "  Zig: $localVer (.tools/)" -ForegroundColor Green
} else {
    Write-Host "  Zig 0.15.x not found. Downloading $ZigPinnedVersion to .tools\..." -ForegroundColor Yellow
    $zigZip = Join-Path $TempDir "zig-$ZigPinnedVersion.zip"
    $zigUrl = "https://ziglang.org/download/$ZigPinnedVersion/zig-x86_64-windows-$ZigPinnedVersion.zip"
    Download-File -Url $zigUrl -OutFile $zigZip -Sha256 $ZigZipSha256
    $toolsRoot = Join-Path $RootDir ".tools"
    if (-not (Test-Path $toolsRoot)) { New-Item -ItemType Directory -Path $toolsRoot | Out-Null }
    Expand-Archive -Path $zigZip -DestinationPath $toolsRoot -Force
    Remove-Item $zigZip
    if (-not (Test-Path $localZigExe)) {
        Write-Error "Zig extraction failed: $ZigToolsDir\zig.exe not found"
    }
    if ((Get-Sha256 -Path $localZigExe) -ne $ZigExeSha256) {
        throw "Zig extraction produced a zig.exe that does not match pinned SHA-256 $ZigExeSha256"
    }
    $localVer = & (Join-Path $ZigToolsDir "zig.exe") version
    Write-Host "  Zig: $localVer (.tools/)" -ForegroundColor Green
}

# CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "  CMake not found. Installing via winget..." -ForegroundColor Yellow
    winget install Kitware.CMake --accept-package-agreements --accept-source-agreements
} else {
    Write-Host "  CMake: found" -ForegroundColor Green
}

# Node.js
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "  Node.js not found. Installing via winget..." -ForegroundColor Yellow
    winget install OpenJS.NodeJS.LTS --accept-package-agreements --accept-source-agreements
} else {
    $nodeVer = node --version
    Write-Host "  Node.js: $nodeVer" -ForegroundColor Green
}

# ─── vcpkg ──────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[2/8] Setting up vcpkg..." -ForegroundColor Cyan

$VcpkgDir = Join-Path $ThirdPartyDir "vcpkg"
$VcpkgExe = Join-Path $VcpkgDir "vcpkg.exe"
# The vcpkg commit pins the port versions (zlib/zstd sources carry SHA512s in
# their portfiles) and the vcpkg tool that bootstrap-vcpkg.bat downloads.
$VcpkgCommit = "f6735d5aa1537081e8d425372ea7e585b0ad65e8"

if (-not (Test-Path $VcpkgExe)) {
    Write-Host "  Cloning vcpkg..." -ForegroundColor Yellow
    git clone --depth 1 https://github.com/Microsoft/vcpkg.git $VcpkgDir
    if ($LASTEXITCODE -ne 0) { Write-Error "Failed to clone vcpkg (git exit $LASTEXITCODE)" }
    # vcpkg keeps its .git (it resolves port versions through git), so the
    # pinned checkout stays verifiable on later runs.
    Set-PinnedCommit -Name "vcpkg" -Path $VcpkgDir -Ref "master" -Commit $VcpkgCommit
    & (Join-Path $VcpkgDir "bootstrap-vcpkg.bat") -disableMetrics
    if (-not (Test-Path $VcpkgExe)) { Write-Error "vcpkg bootstrap failed!" }
} else {
    Confirm-ExistingCheckout -Name "vcpkg" -Path $VcpkgDir -Commit $VcpkgCommit
}

$VcpkgPackages = @("zlib", "zstd")
foreach ($pkg in $VcpkgPackages) {
    Write-Host "  vcpkg: installing $pkg..." -ForegroundColor Yellow
    & $VcpkgExe install "${pkg}:x64-windows" --vcpkg-root $VcpkgDir
    if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg install $pkg failed (exit $LASTEXITCODE)" }
}

# ─── Git Clones ─────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[3/8] Cloning thirdparty repos..." -ForegroundColor Cyan

# DirectX Headers
Clone-Repo -Name "directx_headers" `
    -Url "https://github.com/microsoft/DirectX-Headers.git" `
    -Branch "v1.619.1" `
    -Commit "9e393d6d8a3b30dcc6f2806ef604ec16a27b0d7e" `
    -RemoveDirs @("test", "googletest")

# D3D12 Memory Allocator
Clone-Repo -Name "d3d12ma" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git" `
    -Branch "v3.1.0" `
    -Commit "0fa62ed3a0a69b73230a8ec1faa752d4061c8dc8" `
    -RemoveDirs @("docs", "tools")

# spdlog (logging)
Clone-Repo -Name "spdlog" `
    -Url "https://github.com/gabime/spdlog.git" `
    -Branch "v1.17.0" `
    -Commit "79524ddd08a4ec981b7fea76afd08ee05f83755d" `
    -RemoveDirs @("bench", "example", "logos", "scripts", "tests")

# Tracy (profiling) — keep only LICENSE + public/ (client lib + headers)
Clone-Repo -Name "tracy" `
    -Url "https://github.com/wolfpld/tracy.git" `
    -Branch "v0.13.1" `
    -Commit "05cceee0df3b8d7c6fa87e9638af311dbabc63cb" `
    -RemoveDirs @("capture", "cmake", "csvexport", "doc", "dtl", "examples", "extra",
                  "getopt", "icon", "imgui", "import", "manual", "nfd", "profiler",
                  "python", "server", "test", "update", "zstd")

# Remove build files and Fortran binding (keep only LICENSE + public/)
$tracyPath = Join-Path $ThirdPartyDir "tracy"
if (Test-Path $tracyPath) {
    @("CMakeLists.txt", "Config.cmake.in", "NEWS", "README.md",
      "meson.build", "meson.options") | ForEach-Object {
        $p = Join-Path $tracyPath $_
        if (Test-Path $p) { Remove-Item -Force $p }
    }
    $f90 = Join-Path $tracyPath "public\TracyClient.F90"
    if (Test-Path $f90) { Remove-Item -Force $f90 }
}

# mimalloc (global allocator)
Clone-Repo -Name "mimalloc" `
    -Url "https://github.com/microsoft/mimalloc.git" `
    -Branch "v2.2.4" `
    -Commit "fbd8b99c2b828428947d70fdc046bb55609be93e" `
    -RemoveDirs @("docs", "test", "bench", "ide", "out")

# jemalloc (alternative allocator)
Clone-Repo -Name "jemalloc" `
    -Url "https://github.com/jemalloc/jemalloc.git" `
    -Branch "5.3.0" `
    -Commit "54eaed1d8b56b1aa528be3bdd1877e59c56fa90c" `
    -RemoveDirs @("doc", "test", "scripts", "msvc")

# dlmalloc (lightweight allocator), 2.8.6. The author's server has no
# versioned URL, so the hash (identical to the vendored file) is the pin.
Download-Header -Name "dlmalloc" `
    -Url "https://gee.cs.oswego.edu/pub/misc/malloc.c" `
    -FileName "dlmalloc.c" `
    -Sha256 "be7be56b40b4b31417e74e77687f9b96127ea6cae534cf236aee40a4c739508d"

# atomic_queue (lock-free queue)
# master pinned to the commit whose tree matches the vendored files exactly.
Clone-Repo -Name "atomic_queue" `
    -Url "https://github.com/max0x7ba/atomic_queue.git" `
    -Branch "master" `
    -Commit "bce850fdce9010db3111e79bfe45040d42c678b1" `
    -RemoveDirs @("benchmarks", "doc", "html", "scripts", "src")

# DirectXMath
Clone-Repo -Name "directxmath" `
    -Url "https://github.com/microsoft/DirectXMath.git" `
    -Branch "apr2025" `
    -Commit "56206b1916646b29b37e1eac56e96e2008d6c71f" `
    -RemoveDirs @("SHMath", "XDSP", "Spherical", "Extensions", "MatrixStack", "Tests", "Utilities")

# SAL stub for non-MSVC platforms (DirectXMath requires sal.h)
# Pinned to dotnet/runtime 5c4e2a30, the last commit to change sal.h; its blob
# is identical to the vendored copy.
$salPath = Join-Path $ThirdPartyDir "directxmath\Inc\sal.h"
$salSha256 = "e26ee611231a859b5a800eb2e1606e7ca03b8e3981d7c98cf1ca20c89b658b68"
if (-not (Test-PinnedFile -Label "sal.h" -Path $salPath -Sha256 $salSha256)) {
    Write-Host "  [sal.h] downloading cross-platform stub..." -ForegroundColor Yellow
    Download-File `
        -Url "https://raw.githubusercontent.com/dotnet/runtime/5c4e2a301ec1dd8ef75a3ba98e5be1ff2cea0130/src/coreclr/pal/inc/rt/sal.h" `
        -OutFile $salPath `
        -Sha256 $salSha256
}

# glm (math, header-only)
Clone-Repo -Name "glm" `
    -Url "https://github.com/g-truc/glm.git" `
    -Branch "1.0.3" `
    -Commit "8d1fd52e5ab5590e2c81768ace50c72bae28f2ed" `
    -RemoveDirs @("doc", "test", "util")

# EnTT (ECS — header-only, engine/ecs/core/world.h includes <entt/entt.hpp>)
# Pinned to the commit recorded in the git index for this path (tag v3.16.0).
Clone-Repo -Name "entt" `
    -Url "https://github.com/skypjack/entt.git" `
    -Branch "v3.16.0" `
    -Commit "b4e58bdd364ad72246c123a0c28538eab3252672" `
    -RemoveDirs @("docs", "test", "docker")

# nlohmann/json (config parsing)
Download-Header -Name "nlohmann" `
    -Url "https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp" `
    -FileName "json.hpp" `
    -Sha256 "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63"

# GoogleTest (testing framework)
Clone-Repo -Name "googletest" `
    -Url "https://github.com/google/googletest.git" `
    -Branch "v1.17.0" `
    -Commit "52eb8108c5bdec04579160ae17225d66034bd723" `
    -RemoveDirs @("ci", "docs", "googlemock", "fake_fuchsia_sdk", "test", "samples")

# SPIRV-Reflect (shader reflection)
Clone-Repo -Name "spirv-reflect" `
    -Url "https://github.com/KhronosGroup/SPIRV-Reflect.git" `
    -Branch "vulkan-sdk-1.4.309.0" `
    -Commit "c637858562fbce1b6f5dc7ca48d4e8a5bd117b70" `
    -RemoveDirs @("examples", "tests", "docs", "scripts", "cmake")

# xxhash (fast hashing)
Clone-Repo -Name "xxhash" `
    -Url "https://github.com/Cyan4973/xxHash.git" `
    -Branch "v0.8.3" `
    -Commit "e626a72bc2321cd320e953a0ccf1584cad60f363" `
    -RemoveDirs @("tests", "doc", "cmake_unofficial", "contrib")

# lz4 (fast compression for .uzpak asset archives)
Clone-Repo -Name "lz4" `
    -Url "https://github.com/lz4/lz4.git" `
    -Branch "v1.10.0" `
    -Commit "ebb370ca83af193212df4dcbadcc5d87bc0de2f0" `
    -KeepDirs @("lib") `
    -RemoveDirs @("tests", "doc", "programs", "contrib", "examples", "ossfuzz", "build")

# SSE Math Fun (SIMD math functions)
# master pinned to its tip, which matches the vendored files exactly.
Clone-Repo -Name "sse_mathfun" `
    -Url "https://github.com/RJVB/sse_mathfun.git" `
    -Branch "master" `
    -Commit "0f462645698320643decdb5f79d211733b2a97d6" `
    -RemoveDirs @()

# MikkTSpace (tangent space generation)
# master pinned to its tip (vendored mikktspace.c carries a committed local fix).
Clone-Repo -Name "mikktspace" `
    -Url "https://github.com/mmikk/MikkTSpace.git" `
    -Branch "master" `
    -Commit "3e895b49d05ea07e4c2133156cfa94369e19e409" `
    -RemoveDirs @()

# ufbx (FBX file parser — single-file C library, MIT license)
Clone-Repo -Name "ufbx" `
    -Url "https://github.com/ufbx/ufbx.git" `
    -Branch "v0.14.3" `
    -Commit "19bdb7e7ef02eb914d5e7211a3685f50ee6d27e3" `
    -RemoveDirs @("test", "data", "examples", "misc", "extra", "bindings")

# stb (image loading/writing/resizing — header-only, keep only needed headers)
# master pinned to the last commit before stb_image_resize 2.18, the version vendored here.
Clone-Repo -Name "stb" `
    -Url "https://github.com/nothings/stb.git" `
    -Branch "master" `
    -Commit "2fb8c5a3deb2110c89669f8d6f36e5833b556b44" `
    -RemoveDirs @("docs", "data", "tests", "tools", "deprecated")

# Keep only the headers we use
$stbKeep = @("stb_image.h", "stb_image_write.h", "stb_image_resize2.h", "stb_truetype.h", "LICENSE")
$stbPath = Join-Path $ThirdPartyDir "stb"
if (Test-Path $stbPath) {
    Get-ChildItem $stbPath -File | Where-Object { $stbKeep -notcontains $_.Name } | Remove-Item -Force
    Get-ChildItem $stbPath -Directory | Remove-Item -Recurse -Force
}

# spng (SIMD-accelerated PNG decode — faster than stb_image for PNG)
# Single .c + .h library with SSE2/AVX2/NEON row filter acceleration.
# Used by the texture pipeline for PNG decode; stb_image kept for JPEG/BMP/HDR.
Clone-Repo -Name "spng" `
    -Url "https://github.com/randy408/libspng.git" `
    -Branch "v0.7.4" `
    -Commit "fb768002d4288590083a476af628e51c3f1d47cd" `
    -RemoveDirs @("docs", "tests", "benchmarks", "examples", "fuzzing")

# libjpeg-turbo (fast JPEG decode with SIMD)
Clone-Repo -Name "libjpeg-turbo" `
    -Url "https://github.com/libjpeg-turbo/libjpeg-turbo.git" `
    -Branch "3.1.3" `
    -Commit "af9c1c268520a29adf98cad5138dafe612b3d318" `
    -RemoveDirs @("doc", "java", "release", "win", "fuzz", "cmakescripts", "md5", "testimages", "sharedlib")

# Remove CLI tools, test files, build configs (keep only lib source + headers)
$jtPath = Join-Path $ThirdPartyDir "libjpeg-turbo"
if (Test-Path $jtPath) {
    $jtRemove = @("BUILDING.md","ChangeLog.md","README.md","README.ijg","change.log","coderules.txt",
                   "libjpeg.txt","structure.txt","usage.txt","wizard.txt","jconfig.txt","doxygen.config",
                   "doxygen-extra.css","cjpeg.1","djpeg.1","jpegtran.1","rdjpgcom.1","wrjpgcom.1",
                   "cjpeg.c","djpeg.c","jpegtran.c","rdjpgcom.c","wrjpgcom.c","cdjpeg.c","cdjpeg.h",
                   "rdbmp.c","rdcolmap.c","rdgif.c","rdppm.c","rdswitch.c","rdtarga.c",
                   "wrbmp.c","wrgif.c","wrppm.c","wrtarga.c",
                   "tjbench.c","tjexample.c","tjunittest.c","tjutil.c","tjutil.h","strtest.c",
                   "jcstest.c","jpeg_nbits.c","example.c","croptest.in","tjbenchtest.in","tjexampletest.in",
                   "CMakeLists.txt","jconfig.h.in","jconfigint.h.in","jversion.h.in","libjpeg.map.in",
                   "turbojpeg-jni.c","turbojpeg-mapfile","turbojpeg-mapfile.jni","turbojpeg-mp.c")
    foreach ($f in $jtRemove) {
        $p = Join-Path $jtPath $f
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# tinyexr (EXR/HDR image format — single header only)
Clone-Repo -Name "tinyexr" `
    -Url "https://github.com/syoyo/tinyexr.git" `
    -Branch "v1.0.9" `
    -Commit "5fcb4dcb6e3abf96214b67e5c54db1ceec6a455c" `
    -RemoveDirs @("examples", "experimental", "test", "deps", "cmake", "jni")

# Keep only the header + miniz dependency
$texrPath = Join-Path $ThirdPartyDir "tinyexr"
if (Test-Path $texrPath) {
    Get-ChildItem $texrPath -File | Where-Object { $_.Name -notin @("tinyexr.h", "miniz.h", "miniz.c") } | Remove-Item -Force
    # Download miniz if missing (tinyexr requires it for EXR decompression)
    $minizH = Join-Path $texrPath "miniz.h"
    $minizC = Join-Path $texrPath "miniz.c"
    # miniz 3.0.0 from tinyexr deps (with MINIZ_EXPORT stub prepended), pinned
    # to the v1.0.9 tag's commit. Upstream's miniz.h already carries the stub.
    $minizBase = "https://raw.githubusercontent.com/syoyo/tinyexr/5fcb4dcb6e3abf96214b67e5c54db1ceec6a455c/deps/miniz"
    $minizHSha256 = "295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b"
    $minizCSha256 = "44d54a3caedf3eae0adc6b6e5a5024c5ba6543abc81d20e741d0353b801e57d3"
    if (-not (Test-PinnedFile -Label "tinyexr" -Path $minizH -Sha256 $minizHSha256)) {
        Write-Host "  [tinyexr] downloading miniz.h..." -ForegroundColor Yellow
        Download-File -Url "$minizBase/miniz.h" -OutFile $minizH -Sha256 $minizHSha256
        # Prepend MINIZ_EXPORT define if missing (standalone builds need this)
        $content = Get-Content $minizH -Raw
        if ($content -notmatch 'MINIZ_EXPORT') {
            "#ifndef MINIZ_EXPORT`n#define MINIZ_EXPORT`n#endif`n$content" | Set-Content $minizH -NoNewline
        }
    }
    if (-not (Test-PinnedFile -Label "tinyexr" -Path $minizC -Sha256 $minizCSha256)) {
        Write-Host "  [tinyexr] downloading miniz.c..." -ForegroundColor Yellow
        Download-File -Url "$minizBase/miniz.c" -OutFile $minizC -Sha256 $minizCSha256
    }
}

# basis_universal (GPU texture compression — BC7, ASTC, ETC2)
# master pinned to its tip on the vendoring date; the vendored encoder/transcoder match it exactly.
Clone-Repo -Name "basis_universal" `
    -Url "https://github.com/BinomialLLC/basis_universal.git" `
    -Branch "master" `
    -Commit "28e76f6e340e688188c94386ff068e437e1222ca" `
    -RemoveDirs @("webgl", "test_files", "bin", "OpenCL", "build", "cmd_help", "contrib",
                  "example", "example_capi", "example_transcoding", "khronos", "python",
                  "shader_deblocking", "zstd", "encoder_lib")

# Remove build/tool files from root (keep encoder/, transcoder/, LICENSE*)
$basisPath = Join-Path $ThirdPartyDir "basis_universal"
if (Test-Path $basisPath) {
    $basisRemove = @("CMakeLists.txt","CppProperties.json","README.md","readme_wasi.md","NOTICE",
                     "all_builds.py","basisu.manifest","basisu.sln","basisu.vcxproj",
                     "basisu.vcxproj.filters","basisu_tool.cpp","basisu_tool_help.h",
                     "build_python_win.bat","encoder_lvl_vs_perf.png")
    foreach ($f in $basisRemove) {
        $p = Join-Path $basisPath $f
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# astcenc (ASTC texture encoder/decoder)
Clone-Repo -Name "astcenc" `
    -Url "https://github.com/ARM-software/astc-encoder.git" `
    -Branch "5.3.0" `
    -Commit "30aabb3f42406df45a910d8496f9bee17eeba9bb" `
    -RemoveDirs @("Docs", "Test", "Jenkins", "Utils")

# etcpak (GPU texture compressor — BC1/BC3/BC7/ETC1/ETC2, same as Godot)
# master pinned to "Release 2.1", the commit whose tree matches the vendored files exactly.
Clone-Repo -Name "etcpak" `
    -Url "https://github.com/wolfpld/etcpak.git" `
    -Branch "master" `
    -Commit "7f15da4739cc5f8562311bdbb800436986d268ca" `
    -RemoveDirs @("build", ".github")

# Remove build files from root (keep Source/ + LICENSE)
$astcPath = Join-Path $ThirdPartyDir "astcenc"
if (Test-Path $astcPath) {
    @("CMakeLists.txt","README.md") | ForEach-Object {
        $p = Join-Path $astcPath $_
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# LuaJIT 2.1 (Lua scripting runtime — requires MSVC to build)
# v2.1 branch pinned to the commit matching the vendored sources (only line endings differ).
Clone-Repo -Name "luajit" `
    -Url "https://github.com/LuaJIT/LuaJIT.git" `
    -Branch "v2.1" `
    -Commit "659a61693aa3b87661864ad0f12eee14c865cd7f" `
    -RemoveDirs @("doc", "etc")

$luajitLib = Join-Path $ThirdPartyDir "luajit\src\lua51.lib"
$buildScript = Join-Path $PSScriptRoot "build_luajit.bat"
if (-not (Test-Path $luajitLib)) {
    Write-Host "  [luajit] building with MSVC..." -ForegroundColor Yellow
    cmd /c "`"$buildScript`""
    if (Test-Path $luajitLib) {
        Write-Host "  [luajit] build OK" -ForegroundColor Green
    } else {
        Write-Host "  [luajit] WARN: build failed - run scripts\build_luajit.bat manually from a VS Developer Prompt" -ForegroundColor Red
    }
} else {
    Write-Host "  [luajit] already built, skipping." -ForegroundColor Green
}

# ─── Vulkan Dependencies ──────────────────────────────────────────────────

# Vulkan-Headers (Khronos official API headers)
Clone-Repo -Name "vulkan_headers" `
    -Url "https://github.com/KhronosGroup/Vulkan-Headers.git" `
    -Branch "v1.4.342" `
    -Commit "3cfca3829608e778cf59b0dab55d77f4f6c79bee" `
    -RemoveDirs @("tests", "cmake")

# volk (Vulkan meta-loader — dynamic function dispatch, no static vulkan-1.lib needed)
Clone-Repo -Name "volk" `
    -Url "https://github.com/zeux/volk.git" `
    -Branch "vulkan-sdk-1.4.304.1" `
    -Commit "0b17a763ba5643e32da1b2152f8140461b3b7345" `
    -RemoveDirs @("cmake", "test")

# VMA — Vulkan Memory Allocator (AMD, GPU memory sub-allocation)
Clone-Repo -Name "vma" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git" `
    -Branch "v3.2.1" `
    -Commit "c788c52156f3ef7bc7ab769cb03c110a53ac8fcb" `
    -RemoveDirs @("docs", "tools", "cmake")

# Crashpad (crash reporting) — deferred to milestone 2.x when crash reporting is implemented
Write-Host "  [crashpad] skipped (not needed until 2.x crash reporting)" -ForegroundColor DarkYellow

# ─── Post-Processing & Upscaling ──────────────────────────────────────────

# AMD FidelityFX SDK 2.x (FSR4, Radiance Caching, Denoiser)
# main pinned to "AMD FSR SDK 2.2.0", the commit matching all vendored files.
Clone-Repo -Name "amd-fidelityfx" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git" `
    -Branch "main" `
    -Commit "e236f2304dcda35f282fdddd085f41e2ff48c86a" `
    -RemoveDirs @("docs", "samples", "tools", "PrebuiltSignedDLL", "framework", "BuildFidelityFXSolution.bat")

# AMD FidelityFX SDK 1.1.4 (Brixelizer GI — removed in 2.x)
# Provides ffx_brixelizer.h/.cpp, ffx_brixelizer_gi.h/.cpp, and ~20 HLSL compute shaders
# for real-time SDF-based global illumination. MIT license.
Clone-Repo -Name "amd-fidelityfx-1.1" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git" `
    -Branch "v1.1.4" `
    -Commit "c6efa6bf7f2027b3ec94f28578bb5965eabb9e55" `
    -RemoveDirs @("docs", "samples", "tools", "PrebuiltSignedDLL", "framework", "BuildFidelityFXSolution.bat")

# NVIDIA DLSS SDK (Deep Learning Super Sampling — AI temporal upscaling)
# Provides nvsdk_ngx headers + nvngx_dlss.dll runtime. Requires NVIDIA RTX GPU.
# NOTE: The SDK is ~200MB; runtime/thirdparty/dlss/ is .gitignored.
# main pinned to "DLSS 310.5.3 SDK", the commit matching the local (gitignored) SDK.
Clone-Repo -Name "dlss" `
    -Url "https://github.com/NVIDIA/DLSS.git" `
    -Branch "main" `
    -Commit "982b0d19f9e35fef8e1b3109efa6b95470563866" `
    -RemoveDirs @("doc", "samples", "utils", "tests")

# SMAA — Subpixel Morphological Anti-Aliasing (header-only HLSL/GLSL)
# master pinned to its tip (unchanged since 2013), matching the vendored files.
Clone-Repo -Name "smaa" `
    -Url "https://github.com/iryoku/smaa.git" `
    -Branch "master" `
    -Commit "71c806a838bdd7d517df19192a20f0c61b3ca29d" `
    -RemoveDirs @("Demo", "Scripts")

# ─── Mesh & Geometry Tools ────────────────────────────────────────────────

# MikkTSpace — Industry-standard tangent space generation (Morten Mikkelsen)
# Pinned to the same commit as the clone above (master tip since 2020).
Download-Header -Name "mikktspace" `
    -Url "https://raw.githubusercontent.com/mmikk/MikkTSpace/3e895b49d05ea07e4c2133156cfa94369e19e409/mikktspace.h" `
    -FileName "mikktspace.h" `
    -Sha256 "17fc433894f24c73753d548086cc4d8c5c0379f4a6edfb98b5da243e4f0bc3d0"
# Also download the .c implementation
$mikktDir = Join-Path $ThirdPartyDir "mikktspace"
$mikktC = Join-Path $mikktDir "mikktspace.c"
$mikktCSha256 = "de87e74107df766ce68108801262bd8d53899414236b59810509a8fc2a51e288"
if (-not (Test-PinnedFile -Label "mikktspace" -Path $mikktC -Sha256 $mikktCSha256)) {
    Download-File -Url "https://raw.githubusercontent.com/mmikk/MikkTSpace/3e895b49d05ea07e4c2133156cfa94369e19e409/mikktspace.c" `
        -OutFile $mikktC -Sha256 $mikktCSha256
}

# xatlas — UV atlas generation (lightmaps, LOD baking)
# master pinned to its tip, which matches the vendored files exactly.
Clone-Repo -Name "xatlas" `
    -Url "https://github.com/jpcy/xatlas.git" `
    -Branch "master" `
    -Commit "f700c7790aaa030e794b52ba7791a05c085faf0c" `
    -RemoveDirs @("extra", "viewer", "test")

# meshoptimizer — vertex cache / overdraw / fetch optimization, simplification
# Pinned to the commit recorded in the git index for this path (tag v0.21).
Clone-Repo -Name "meshoptimizer" `
    -Url "https://github.com/zeux/meshoptimizer.git" `
    -Branch "v0.21" `
    -Commit "47aafa533b439a78b53cd2854c177db61be7e666" `
    -RemoveDirs @("demo", "js", "gltf", "tools")

# fastgltf — glTF 2.0 parser (bundles simdjson under deps/simdjson)
# Pinned to the commit recorded in the git index for this path (tag v0.8.0).
Clone-Repo -Name "fastgltf" `
    -Url "https://github.com/spnda/fastgltf.git" `
    -Branch "v0.8.0" `
    -Commit "d4734a7e55d8d25155e755d895e018e423845cd0" `
    -RemoveDirs @("tests", "examples", "docs")

# fastgltf pulls the simdjson singleheader at CMake configure time. The Zig
# build compiles deps/simdjson/simdjson.cpp directly, so fetch it here instead.
# Version must match SIMDJSON_TARGET_VERSION in fastgltf/CMakeLists.txt.
$simdjsonVersion = "3.9.4"
$simdjsonCommit = "3620e9d151a1a74ee8132af3770f3e28b4b4b66c"  # tag v3.9.4
$simdjsonSha256 = @{
    "simdjson.h"   = "88a44cd77e0b443724f52bed7a3b86bcd84ed1c29a451ecc94210fc7d6aef069"
    "simdjson.cpp" = "a0410ae49e2e2fd967330fc22f91d7a06f3f6c5988bf53faa4855c2e3dee2805"
}
$simdjsonDir = Join-Path $ThirdPartyDir "fastgltf\deps\simdjson"
foreach ($f in @("simdjson.h", "simdjson.cpp")) {
    $dst = Join-Path $simdjsonDir $f
    if (-not (Test-PinnedFile -Label "simdjson" -Path $dst -Sha256 $simdjsonSha256[$f])) {
        New-Item -ItemType Directory -Path $simdjsonDir -Force | Out-Null
        Write-Host "  [simdjson] downloading $f ($simdjsonVersion)..." -ForegroundColor Yellow
        Download-File `
            -Url "https://raw.githubusercontent.com/simdjson/simdjson/$simdjsonCommit/singleheader/$f" `
            -OutFile $dst `
            -Sha256 $simdjsonSha256[$f]
    }
}

# ─── Image Formats ────────────────────────────────────────────────────────

# libpng — PNG image format (16-bit, interlacing, robust asset pipeline)
Clone-Repo -Name "libpng" `
    -Url "https://github.com/pnggroup/libpng.git" `
    -Branch "v1.6.49" `
    -Commit "640204280f8109d7165f95d2b177f89baf20b253" `
    -RemoveDirs @("contrib", "projects", "tests")

# libpng needs pnglibconf.h — copy the prebuilt version from scripts/
$libpngDir = Join-Path $ThirdPartyDir "libpng"
$prebuilt  = Join-Path $libpngDir "scripts\pnglibconf.h.prebuilt"
$target    = Join-Path $libpngDir "pnglibconf.h"
if ((Test-Path $prebuilt) -and -not (Test-Path $target)) {
    Copy-Item $prebuilt $target
    Write-Host "  [libpng] copied pnglibconf.h.prebuilt → pnglibconf.h" -ForegroundColor Green
}

# ─── Font & Text Rendering ────────────────────────────────────────────────

# msdfgen — Multi-channel signed distance field font atlas generator
Clone-Repo -Name "msdfgen" `
    -Url "https://github.com/Chlumsky/msdfgen.git" `
    -Branch "v1.13" `
    -Commit "1874bcf7d9624ccc85b4bc9a85d78116f690f35b" `
    -RemoveDirs @("cmake", "test")

# FreeType — Font rasterization (hinting, CJK, complex scripts)
Clone-Repo -Name "freetype" `
    -Url "https://github.com/freetype/freetype.git" `
    -Branch "VER-2-13-3" `
    -Commit "42608f77f20749dd6ddc9e0536788eaad70ea4b5" `
    -RemoveDirs @("docs", "tests", "builds")

# ─── Downloads ──────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[4/8] Downloading prebuilt packages..." -ForegroundColor Cyan

# DXC (DirectX Shader Compiler) v1.9.2602: URL + archive hash from the native lock
$dxcPackage = Get-LockedPackage -Name "dxc"
Download-Extract-Zip -Name "dxc" `
    -Url $dxcPackage.url `
    -Sha256 $dxcPackage.sha256 `
    -VerifyFile "bin\x64\dxcompiler.dll" `
    -VerifyFileSha256 "b86a738ece4c05dbe2d9bbb29668a2ccb28a0740773c9b027cdc61e8d07b4d75"

# ─── DirectX 12 Agility SDK ───────────────────────────────────────────────
# D3D12Core.dll must sit next to the exe (see engine/rhi/dx12/dx12_agility.cpp,
# which exports D3D12SDKVersion/D3D12SDKPath). Version must match
# D3D12_SDK_VERSION from thirdparty/directx_headers/include/directx/d3d12.h.
# Shipped as a NuGet package, which is a plain zip with a build/native/ layout.
# URL + package hash come from the native lock; D3D12Core.dll's hash was taken
# from that verified package.
$agilityDir = Join-Path $ThirdPartyDir "agility"
$agilityCore = Join-Path $agilityDir "bin\x64\D3D12Core.dll"
$agilityVersion = "1.619.2"
$agilityCoreSha256 = "90770bc8028ebb9f78051f903343b0aabc12fe7a2b886e007278b5312ba6f101"
if (-not (Test-PinnedFile -Label "agility" -Path $agilityCore -Sha256 $agilityCoreSha256)) {
    $agilityPackage = Get-LockedPackage -Name "agility"
    if ($agilityPackage.url -notmatch [regex]::Escape("/$agilityVersion/")) {
        throw "[agility] native lock URL $($agilityPackage.url) is not version $agilityVersion"
    }
    $agilityZip = Join-Path $TempDir "agility.zip"
    Write-Host "  [agility] downloading D3D12 Agility SDK $agilityVersion..." -ForegroundColor Yellow
    Download-File `
        -Url $agilityPackage.url `
        -OutFile $agilityZip `
        -Sha256 $agilityPackage.sha256

    $agilityTemp = Join-Path $TempDir "agility_temp"
    Expand-Archive -Path $agilityZip -DestinationPath $agilityTemp -Force

    New-Item -ItemType Directory -Path $agilityDir -Force | Out-Null
    Copy-Item -Path (Join-Path $agilityTemp "build\native\include") -Destination $agilityDir -Recurse -Force
    Copy-Item -Path (Join-Path $agilityTemp "build\native\bin")     -Destination $agilityDir -Recurse -Force
    Copy-Item -Path (Join-Path $agilityTemp "LICENSE.txt")          -Destination $agilityDir -Force -ErrorAction SilentlyContinue

    Remove-Item -Recurse -Force $agilityTemp
    Remove-Item $agilityZip

    if (-not (Test-Path $agilityCore)) {
        Write-Error "Agility SDK extraction failed - D3D12Core.dll not found"
    }
    if ((Get-Sha256 -Path $agilityCore) -ne $agilityCoreSha256) {
        throw "[agility] extracted D3D12Core.dll does not match pinned SHA-256 $agilityCoreSha256"
    }
    Write-Host "  [agility] installed $agilityVersion" -ForegroundColor Green
}

# ─── FXC (d3dcompiler_47.dll — SM5.0 fallback path) ───────────────────────
# Not redistributed standalone; sourced from the installed Windows 10/11 SDK.
$fxcDll = Join-Path $ThirdPartyDir "fxc\bin\x64\d3dcompiler_47.dll"
if (-not (Test-Path $fxcDll)) {
    $sdkCandidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\d3dcompiler_47.dll" -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending
    if ($sdkCandidates) {
        $src = $sdkCandidates[0].FullName
        New-Item -ItemType Directory -Path (Split-Path $fxcDll) -Force | Out-Null
        Copy-Item $src $fxcDll -Force
        Write-Host "  [fxc] copied from $($sdkCandidates[0].Directory.Parent.Name)" -ForegroundColor Green
    } else {
        Write-Error "d3dcompiler_47.dll not found. Install the Windows 10/11 SDK (VS Installer > Individual Components)."
    }
} else {
    Write-Host "  [fxc] already exists, skipping." -ForegroundColor Green
}

# ─── Metal Dependencies (macOS build cross-compile support) ──────────────────

# metal-cpp (Apple's C++ wrapper for Metal — macOS only, downloaded for cross-ref)
# Apple's official macOS 15.2 / iOS 18.2 archive. The GitHub mirror previously
# cloned here no longer exists; this archive's 98 files match the vendored tree
# (line endings aside), so the version is unchanged.
Download-Extract-Zip -Name "metal-cpp" `
    -Url "https://developer.apple.com/metal/cpp/files/metal-cpp_macOS15.2_iOS18.2.zip" `
    -Sha256 "3437e4abfbd3d45217f34772ef3502f31ba3358e5fb6ac9d0ca952a047bcfe25" `
    -SubPath "metal-cpp" `
    -VerifyFile "Metal\Metal.hpp" `
    -VerifyFileSha256 "cd1795c2d97fa914c8a37895d394789bd753f921e7bdea60d6c945a5a7687cf1"

# Slang (HLSL→MSL/SPIR-V shader compiler, Windows prebuilt)
# macOS uses libslang.dylib; see setup.sh for the macOS download.
$slangArch = "windows-x86_64"
# Archive hash and bin\slang.dll hash per architecture (v2025.6.3 release assets).
$slangZipSha256 = "3f96b69f775566a748099a4fefa4cfe6a42227192a57857034b8e47bc8919024"
$slangDllSha256 = "0e75de82197e8e40cbf7e9d614d4599529b75b5f88808d7d2176d67ec4a3666d"
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq [System.Runtime.InteropServices.Architecture]::Arm64) {
    $slangArch = "windows-aarch64"
    $slangZipSha256 = "0ae490a77cf54227d1d14d13da65c62b04a6a7f1fd662798bcb8e9b6d4f4b054"
    $slangDllSha256 = "ae9fceeeaff5210bdb52f99e3cb7ecb70c85b5785747496accf290b84a4c3a46"
}
Download-Extract-Zip -Name "slang" `
    -Url "https://github.com/shader-slang/slang/releases/download/v2025.6.3/slang-2025.6.3-$slangArch.zip" `
    -Sha256 $slangZipSha256 `
    -VerifyFile "bin\slang.dll" `
    -VerifyFileSha256 $slangDllSha256

# ─── FFmpeg (H.264 hardware encoding for viewport frame streaming) ────────

# Pre-built LGPL shared libraries from BtbN's automated builds.
# Includes: libavcodec (H.264 encode/decode), libavutil, libswscale (RGBA to NV12).
# LGPL licensed - no GPL codec libraries, safe for proprietary use.
$ffmpegDir = Join-Path $ThirdPartyDir "ffmpeg"

# phase0_fix-vfx-demo-launch-pipeline -- self-healing for incomplete
# extractions. If a prior setup left only lib/+include/ but no bin/,
# the runtime DLLs (swscale-9.dll, avcodec-62.dll, ...) are missing
# and every exe crashes at startup. Detect the gap and force a fresh
# re-download.
$ffmpegBinDir = Join-Path $ffmpegDir "bin"
if ((Test-Path $ffmpegDir) -and -not (Test-Path $ffmpegBinDir)) {
    Write-Host "  [ffmpeg] bin/ missing -- forcing clean re-download..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $ffmpegDir -ErrorAction SilentlyContinue
}

# Probe an import lib, not the directory: the headers and *.dll.a files are
# committed but *.lib and bin/*.dll are gitignored, so a fresh clone has the
# directory without anything the linker needs.
# The build (BtbN autobuild-2026-03-31-13-11, n8.1) and its archive hash come
# from the native lock, never the moving "latest" release; avcodec.lib's hash
# was taken from that verified archive.
$ffmpegLib = Join-Path $ffmpegDir "lib\avcodec.lib"
$ffmpegLibSha256 = "aae17c97ec976acfab98a5b865273274dffcc3bcc59c7ab401a6b33db04b9ee5"
if (-not (Test-PinnedFile -Label "ffmpeg" -Path $ffmpegLib -Sha256 $ffmpegLibSha256)) {
    $ffmpegPackage = Get-LockedPackage -Name "ffmpeg"
    $ffmpegZip = Join-Path $TempDir "ffmpeg.zip"
    Write-Host "  [ffmpeg] downloading LGPL shared build..." -ForegroundColor Yellow
    Download-File -Url $ffmpegPackage.url -OutFile $ffmpegZip -Sha256 $ffmpegPackage.sha256

    # Extract to temp, then merge inner dir into thirdparty/ffmpeg
    $ffmpegTemp = Join-Path $TempDir "ffmpeg_temp"
    Expand-Archive -Path $ffmpegZip -DestinationPath $ffmpegTemp -Force

    # Find the extracted inner directory (name varies with build date)
    $innerDir = Get-ChildItem $ffmpegTemp -Directory | Select-Object -First 1
    if ($innerDir) {
        New-Item -ItemType Directory -Path $ffmpegDir -Force | Out-Null
        Copy-Item -Path (Join-Path $innerDir.FullName "*") -Destination $ffmpegDir -Recurse -Force
        Write-Host "  [ffmpeg] installed: $($innerDir.Name)" -ForegroundColor Green
    } else {
        Write-Error "FFmpeg extraction failed - no inner directory found"
    }

    Remove-Item -Recurse -Force $ffmpegTemp -ErrorAction SilentlyContinue
    Remove-Item $ffmpegZip -ErrorAction SilentlyContinue

    # Remove docs we don't need (keep LICENSE for LGPL compliance)
    $ffmpegRemove = @("doc", "man", "README.txt")
    foreach ($f in $ffmpegRemove) {
        $p = Join-Path $ffmpegDir $f
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }
    # Remove executables (we only need libraries + headers)
    @("ffmpeg.exe", "ffprobe.exe", "ffplay.exe") | ForEach-Object {
        $p = Join-Path $ffmpegDir "bin\$_"
        if (Test-Path $p) { Remove-Item -Force $p }
    }
    if (-not (Test-Path $ffmpegLib) -or (Get-Sha256 -Path $ffmpegLib) -ne $ffmpegLibSha256) {
        throw "[ffmpeg] installed lib\avcodec.lib is missing or does not match pinned SHA-256 $ffmpegLibSha256"
    }
}

# ─── Global Cleanup ────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[5/8] Cleaning up..." -ForegroundColor Cyan

$JunkDirs = @(".github", ".vscode", ".ci", ".circleci")
$JunkFiles = @(".gitignore", ".gitattributes", ".gitmodules",
               ".clang-format", ".clang-tidy", ".editorconfig",
               ".travis.yml", ".pre-commit-config.yaml",
               "docker-compose.yml", "mkdocs.yml",
               "codecov.yml", "CONTRIBUTING.md",
               "CODE_OF_CONDUCT.md", "SECURITY.md",
               "Makefile", "Doxyfile", "appveyor.yml")

Get-ChildItem -Path $ThirdPartyDir -Directory | ForEach-Object {
    $repoPath = $_.FullName
    if ($_.Name -eq "vcpkg") { return } # skip vcpkg

    # Remove .git leftovers
    $gitDir = Join-Path $repoPath ".git"
    if (Test-Path $gitDir) { Remove-Item -Recurse -Force $gitDir }

    foreach ($dir in $JunkDirs) {
        $p = Join-Path $repoPath $dir
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }
    foreach ($file in $JunkFiles) {
        $p = Join-Path $repoPath $file
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# Remove temp dir
if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir }

# ─── Apply Patches ────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[6/9] Applying thirdparty patches..." -ForegroundColor Cyan

Get-ChildItem -Path $ThirdPartyDir -Directory | ForEach-Object {
    if ($_.Name -eq "vcpkg") { return }
    Apply-Patches -Name $_.Name
}

Write-Host "  Patches applied." -ForegroundColor Green

# ─── npm install ───────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[7/9] Installing Node dependencies..." -ForegroundColor Cyan

$npmModules = @("napi", "mcp", "extension")
foreach ($mod in $npmModules) {
    $modPath = Join-Path $RootDir $mod
    if (Test-Path (Join-Path $modPath "package.json")) {
        Write-Host "  [$mod] npm install..." -ForegroundColor Yellow
        Push-Location $modPath
        # No 2>&1: npm writes deprecation notices to stderr, and merging them
        # into the success stream makes each one a terminating NativeCommandError
        # under $ErrorActionPreference = "Stop".
        npm install --no-fund --loglevel=error | Out-Null
        if ($LASTEXITCODE -ne 0) { Pop-Location; Write-Error "npm install failed in $mod" }
        Pop-Location
        Write-Host "  [$mod] OK" -ForegroundColor Green
    }
}

# ─── Verify Image Libraries ────────────────────────────────────────────────

Write-Host ""
Write-Host "[8/9] Verifying image libraries..." -ForegroundColor Cyan

$ImageLibs = @(
    @{ Name = "stb";              File = "stb_image.h" },
    @{ Name = "spng";             File = "spng\spng.h" },
    @{ Name = "libjpeg-turbo";    File = "src\jpeglib.h" },
    @{ Name = "tinyexr";          File = "tinyexr.h" },
    @{ Name = "basis_universal";  File = "transcoder\basisu_transcoder.h" },
    @{ Name = "astcenc";          File = "Source\astcenc.h" },
    @{ Name = "vulkan_headers";   File = "include\vulkan\vulkan.h" },
    @{ Name = "volk";             File = "volk.h" },
    @{ Name = "vma";              File = "include\vk_mem_alloc.h" },
    @{ Name = "metal-cpp";        File = "Metal\Metal.hpp" },
    @{ Name = "slang";            File = "include\slang.h" },
    @{ Name = "luajit";           File = "src\lua.h" },
    @{ Name = "ufbx";             File = "ufbx.h" },
    @{ Name = "ffmpeg";           File = "include\libavcodec\avcodec.h" }
)

$allOk = $true
foreach ($lib in $ImageLibs) {
    $check = Join-Path $ThirdPartyDir "$($lib.Name)\$($lib.File)"
    if (Test-Path $check) {
        Write-Host "  [$($lib.Name)] OK" -ForegroundColor Green
    } else {
        Write-Host "  [$($lib.Name)] MISSING: $check" -ForegroundColor Red
        $allOk = $false
    }
}
if (-not $allOk) { Write-Warning "Some image libraries are incomplete. Check above." }

# ─── RenderDoc API ─────────────────────────────────────────────────────────
# Single-header API for in-application frame capture integration.
# Source: https://github.com/baldurk/renderdoc
# Pinned to the commit tag v1.35 points to.
Download-Header -Name "renderdoc" `
    -Url "https://raw.githubusercontent.com/baldurk/renderdoc/8a394019fdb62ad82be87bfd338a5ec43f4a5d95/renderdoc/api/app/renderdoc_app.h" `
    -FileName "renderdoc_app.h" `
    -Sha256 "0ab27bfa81235ebfc8f4c48111399ea81477eb4f77665e6209c9247daee91b94"

# ─── Summary ───────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[9/9] Summary" -ForegroundColor Cyan

$dirCount = (Get-ChildItem -Path $ThirdPartyDir -Directory | Where-Object { $_.Name -ne "vcpkg" } | Measure-Object).Count
Write-Host "  $dirCount thirdparty libraries installed" -ForegroundColor Green

Write-Host ""
Write-Host "=== Setup Complete ===" -ForegroundColor Green
Write-Host "Thirdparty libraries installed at: $ThirdPartyDir"
Write-Host ""
Write-Host "Next: scripts\build.ps1"
