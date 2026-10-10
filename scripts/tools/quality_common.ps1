# Shared script-relative discovery and fail-closed process execution.
$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not (Test-Path -LiteralPath (Join-Path $Root 'runtime/build.zig'))) {
    throw "Invalid repository root: $Root"
}

function Invoke-QualityCommand {
    param([string]$FilePath, [string[]]$Arguments = @())
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Get-QualityTool {
    param([string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) { throw "Required quality tool is missing: $Name" }
    return $command.Source
}

function Get-QualityZig {
    $localZig = Join-Path $Root '.tools/zig-x86_64-windows-0.15.2/zig.exe'
    $zig = if (Test-Path -LiteralPath $localZig) { $localZig } else { Get-QualityTool 'zig' }
    $version = & $zig version
    if ($LASTEXITCODE -ne 0 -or $version -ne '0.15.2') {
        throw "Windows baseline requires Zig 0.15.2; found $version"
    }
    return $zig
}

function Get-QualitySources {
    param([string]$Module)
    $directories = if ($Module -eq 'runtime') {
        @('engine', 'editor', 'platform', 'tools', 'tests')
    } else { @('src') }
    $sources = @(foreach ($directory in $directories) {
        $path = Join-Path $Root "$Module/$directory"
        if (Test-Path -LiteralPath $path) {
            Get-ChildItem -LiteralPath $path -Recurse -File |
                Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp', '.c', '.mm') } |
                Select-Object -ExpandProperty FullName
        }
    })
    if ($sources.Count -eq 0) { throw "No native sources discovered in $Module" }
    return $sources
}

function Get-QualityCompilationDatabase {
    param([string]$Module, [string]$ExplicitPath)
    $candidates = if ($ExplicitPath) { @($ExplicitPath) } else {
        @("$Module/compile_commands.json", "$Module/build/compile_commands.json",
          "$Module/build/clang-debug/compile_commands.json")
    }
    foreach ($candidate in $candidates) {
        $path = if ([IO.Path]::IsPathRooted($candidate)) { $candidate } else { Join-Path $Root $candidate }
        if (Test-Path -LiteralPath $path -PathType Leaf) { return (Resolve-Path -LiteralPath $path).Path }
    }
    throw "Required compilation database missing for $Module. Configure CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON and pass its path."
}

function Invoke-TypeScriptQuality {
    param([string]$Module, [string]$Tool, [string[]]$Arguments)
    $entry = Join-Path $Root "$Module/node_modules/$Tool"
    if (-not (Test-Path -LiteralPath $entry)) { throw "Missing $Module dependencies; run npm ci in $Module" }
    Push-Location (Join-Path $Root $Module)
    try { Invoke-QualityCommand (Get-QualityTool 'node') (@($entry) + $Arguments) }
    finally { Pop-Location }
}
