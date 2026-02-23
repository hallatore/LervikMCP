<#
.SYNOPSIS
    Builds the LervikMCPTestProject UE5 project using UnrealBuildTool.

.DESCRIPTION
    Compiles the LervikMCPTestProject project (or specified target) using Unreal Engine's Build.bat.
    Automatically finds UE5 installation or accepts a manual engine path.

.PARAMETER Configuration
    Build configuration. Default: "Development"

.PARAMETER Platform
    Target platform. Default: "Win64"

.PARAMETER Target
    Build target name. Default: "LervikMCPTestProjectEditor"

.PARAMETER EnginePath
    Path to UE5 installation root. Auto-detected if not provided.

.PARAMETER Clean
    Perform a clean build.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Configuration Shipping -Target LervikMCPTestProject
    .\scripts\build.ps1 -Clean
    .\scripts\build.ps1 -EnginePath "D:\UE_5.4"
#>

param(
    [string]$Configuration = "Development",
    [string]$Platform = "Win64",
    [string]$Target = "LervikMCPTestProjectEditor",
    [string]$EnginePath,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\Resolve-UEPath.ps1"

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
$Engine = Resolve-UEPath -Version "5.4" -EnginePath $EnginePath

$BuildBat = Join-Path $Engine "Engine\Build\BatchFiles\Build.bat"
if (-not (Test-Path $BuildBat)) {
    Write-Error "Build.bat not found at: $BuildBat"
    exit 1
}

# --- Clean ---
if ($Clean) {
    Write-Host "=== CLEAN BUILD ===" -ForegroundColor Yellow
    $intermediatePath = Join-Path $ProjectRoot "Intermediate"
    $binariesPath = Join-Path $ProjectRoot "Binaries"
    if (Test-Path $intermediatePath) {
        Write-Host "Removing Intermediate/..." -ForegroundColor DarkYellow
        Remove-Item -Recurse -Force $intermediatePath
    }
    if (Test-Path $binariesPath) {
        Write-Host "Removing Binaries/..." -ForegroundColor DarkYellow
        Remove-Item -Recurse -Force $binariesPath
    }
}

# --- Build ---
Write-Host ""
Write-Host "=== LervikMCPTestProject Build ===" -ForegroundColor Cyan
Write-Host "  Engine:        $Engine" -ForegroundColor Gray
Write-Host "  Target:        $Target" -ForegroundColor Gray
Write-Host "  Platform:      $Platform" -ForegroundColor Gray
Write-Host "  Configuration: $Configuration" -ForegroundColor Gray
Write-Host "  Project:       $UProjectPath" -ForegroundColor Gray
Write-Host ""

$buildArgs = @(
    $Target,
    $Platform,
    $Configuration,
    "-Project=`"$UProjectPath`"",
    "-WaitMutex",
    "-FromMsBuild"
)

Write-Host "Running: Build.bat $($buildArgs -join ' ')" -ForegroundColor DarkGray
Write-Host ""

& $BuildBat @buildArgs
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host ""
    Write-Host "=== Build SUCCEEDED ===" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "=== Build FAILED (exit code: $exitCode) ===" -ForegroundColor Red
}

exit $exitCode
