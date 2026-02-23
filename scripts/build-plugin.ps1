<#
.SYNOPSIS
    Builds the LervikMCP plugin for a specified UE version using RunUAT BuildPlugin.

.PARAMETER Version
    UE version string, e.g. "5.4", "5.7". Required unless -EnginePath is provided.

.PARAMETER EnginePath
    Override the engine root path (auto-resolved by version if omitted).

.PARAMETER PluginPath
    Override plugin root path (auto-resolved from project layout if omitted).

.EXAMPLE
    .\scripts\build-plugin.ps1 -Version 5.4
    .\scripts\build-plugin.ps1 -Version 5.7
    .\scripts\build-plugin.ps1 -EnginePath "D:\UE_5.7"
#>

param(
    [string]$Version,
    [string]$EnginePath,
    [string]$PluginPath
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\Resolve-UEPath.ps1"

if (-not $Version -and -not $EnginePath) { throw "Must provide -Version or -EnginePath" }

# --- Resolve engine path ---
$Engine = Resolve-UEPath -Version $Version -EnginePath $EnginePath

$RunUAT = Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $RunUAT)) {
    Write-Error "RunUAT.bat not found at: $RunUAT"
    exit 1
}

# --- Resolve plugin path ---
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DefaultPluginRoot = Join-Path $ProjectRoot "Plugins\LervikMCP"
$PluginRoot = if ($PluginPath) { $PluginPath } else { $DefaultPluginRoot }

$UPluginFile = Join-Path $PluginRoot "LervikMCP.uplugin"
if (-not (Test-Path $UPluginFile)) {
    Write-Error "LervikMCP.uplugin not found at: $UPluginFile"
    exit 1
}

# --- Output directory ---
$BuildDir = Join-Path $PluginRoot ("Build_" + ($Version -replace '\.', '_'))

if (Test-Path $BuildDir) {
    Write-Host "Removing existing build output: $BuildDir" -ForegroundColor DarkYellow
    Remove-Item -Recurse -Force $BuildDir
}

# --- Build ---
Write-Host ""
Write-Host "=== LervikMCP Plugin Build ===" -ForegroundColor Cyan
Write-Host "  Version:   UE $Version" -ForegroundColor Gray
Write-Host "  Engine:    $Engine" -ForegroundColor Gray
Write-Host "  Plugin:    $UPluginFile" -ForegroundColor Gray
Write-Host "  Output:    $BuildDir" -ForegroundColor Gray
Write-Host ""

& $RunUAT BuildPlugin `
    "-Plugin=$UPluginFile" `
    "-Package=$BuildDir" `
    "-targetplatforms=Win64" `
    "-Rocket"

$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host ""
    Write-Host "=== Build SUCCEEDED ===" -ForegroundColor Green
    Write-Host "  Output: $BuildDir" -ForegroundColor Gray
} else {
    Write-Host ""
    Write-Host "=== Build FAILED (exit code: $exitCode) ===" -ForegroundColor Red
}

exit $exitCode
