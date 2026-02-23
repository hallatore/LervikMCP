<#
.SYNOPSIS
    Deploys a built LervikMCP plugin to the engine's Marketplace plugins folder.

.DESCRIPTION
    Copies the build output (from build-plugin.ps1) into the UE engine installation
    so the plugin is available engine-wide. Run build-plugin.ps1 first.

.PARAMETER Version
    UE version string, e.g. "5.4", "5.7". Required unless -EnginePath is provided.

.PARAMETER EnginePath
    Override the engine root path (auto-resolved by version if omitted).

.PARAMETER PluginPath
    Override plugin root path (auto-resolved from project layout if omitted).

.EXAMPLE
    .\scripts\deploy-plugin.ps1 -Version 5.4
    .\scripts\deploy-plugin.ps1 -Version 5.7
    .\scripts\deploy-plugin.ps1 -EnginePath "D:\UE_5.7"
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

# --- Resolve plugin path ---
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DefaultPluginRoot = Join-Path $ProjectRoot "Plugins\LervikMCP"
$PluginRoot = if ($PluginPath) { $PluginPath } else { $DefaultPluginRoot }

# --- Source (build output) ---
$BuildDir = Join-Path $PluginRoot ("Build_" + ($Version -replace '\.', '_'))
if (-not (Test-Path $BuildDir)) {
    Write-Error "Build output not found: $BuildDir`nRun build-plugin.ps1 -Version $Version first."
    exit 1
}

# --- Destination ---
$Dest = Join-Path $Engine "Engine\Plugins\Marketplace\LervikMCP"

Write-Host ""
Write-Host "=== LervikMCP Plugin Deploy ===" -ForegroundColor Cyan
Write-Host "  Version:   UE $Version" -ForegroundColor Gray
Write-Host "  Source:    $BuildDir" -ForegroundColor Gray
Write-Host "  Dest:      $Dest" -ForegroundColor Gray
Write-Host ""

if (Test-Path $Dest) {
    Write-Host "Removing existing install: $Dest" -ForegroundColor DarkYellow
    Remove-Item -Recurse -Force $Dest
}

Copy-Item -Recurse -Force $BuildDir $Dest

Write-Host ""
Write-Host "=== Deploy SUCCEEDED ===" -ForegroundColor Green
Write-Host "  Installed to: $Dest" -ForegroundColor Gray
