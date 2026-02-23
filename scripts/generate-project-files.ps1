<#
.SYNOPSIS
    Regenerates Unreal Engine project files for the LervikMCPTestProject project.

.DESCRIPTION
    Regenerates .sln, .vcxproj, and other project files using UnrealBuildTool.
    Automatically finds UE5 installation or accepts a manual engine path.

.PARAMETER Version
    UE version string, e.g. "5.4". Required unless -EnginePath is provided.

.PARAMETER EnginePath
    Path to UE engine installation root. Required unless -Version is provided.

.EXAMPLE
    .\scripts\generate-project-files.ps1 -Version 5.4
    .\scripts\generate-project-files.ps1 -EnginePath "D:\UE_5.4"
#>

param(
    [string]$Version,
    [string]$EnginePath
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\Resolve-UEPath.ps1"

if (-not $Version -and -not $EnginePath) { throw "Must provide -Version or -EnginePath" }

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# If script is run from scripts/ directly, adjust
if (-not (Test-Path (Join-Path $ProjectRoot "LervikMCPTestProject.uproject"))) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
}
if (-not (Test-Path (Join-Path $ProjectRoot "LervikMCPTestProject.uproject"))) {
    $ProjectRoot = $PSScriptRoot | Split-Path -Parent
}

$UProjectPath = Join-Path $ProjectRoot "LervikMCPTestProject.uproject"
if (-not (Test-Path $UProjectPath)) {
    Write-Error "Cannot find LervikMCPTestProject.uproject. Run this script from the project root or scripts/ directory."
    exit 1
}

# --- Find UE5 Engine Path ---
$Engine = Resolve-UEPath -Version $Version -EnginePath $EnginePath

$UBTPath = Join-Path $Engine "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"
if (-not (Test-Path $UBTPath)) {
    Write-Error "UnrealBuildTool.exe not found at: $UBTPath"
    exit 1
}

# --- Generate Project Files ---
Write-Host ""
Write-Host "=== Generate Project Files ===" -ForegroundColor Cyan
Write-Host "  Engine:  $Engine" -ForegroundColor Gray
Write-Host "  Project: $UProjectPath" -ForegroundColor Gray
Write-Host ""

$ubtArgs = @(
    "-projectfiles",
    "-project=`"$UProjectPath`"",
    "-game",
    "-rocket",
    "-progress"
)

Write-Host "Running: UnrealBuildTool.exe $($ubtArgs -join ' ')" -ForegroundColor DarkGray
Write-Host ""

& $UBTPath @ubtArgs
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host ""
    Write-Host "=== Project Files Generated ===" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "=== Generation FAILED (exit code: $exitCode) ===" -ForegroundColor Red
}

exit $exitCode
