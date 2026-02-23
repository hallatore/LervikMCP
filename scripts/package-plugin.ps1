<#
.SYNOPSIS
    Packages the LervikMCP plugin into a zip for Fab/Marketplace distribution.

.DESCRIPTION
    Temporarily injects EngineVersion into LervikMCP.uplugin, zips the plugin
    using 7-Zip with a file list, then reverts the uplugin change.

.PARAMETER Version
    UE version string, e.g. "5.4". Used in zip name and EngineVersion field. Default: 5.4

.PARAMETER FileList
    Path to 7-Zip include file list. Default: .\scripts\7zip_listfile_LervikMCP.txt

.PARAMETER SevenZipPath
    Path to 7z.exe. Default: C:\Program Files\7-Zip\7z.exe

.PARAMETER PluginPath
    Override plugin root path (auto-resolved from project layout if omitted).

.EXAMPLE
    .\scripts\package-plugin.ps1 -Version 5.4
    .\scripts\package-plugin.ps1 -Version 5.6 -SevenZipPath "D:\Tools\7-Zip\7z.exe"
#>

param(
    [string]$Version,
    [string]$FileList,
    [string]$SevenZipPath = "C:\Program Files\7-Zip\7z.exe",
    [string]$PluginPath
)

$ErrorActionPreference = "Stop"

if (-not $Version) { throw "Must provide -Version" }

# --- Resolve paths ---
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DefaultPluginRoot = Join-Path $ProjectRoot "Plugins\LervikMCP"
$PluginRoot = if ($PluginPath) { $PluginPath } else { $DefaultPluginRoot }

$UPluginFile = Join-Path $PluginRoot "LervikMCP.uplugin"
if (-not (Test-Path $UPluginFile)) {
    Write-Error "LervikMCP.uplugin not found at: $UPluginFile"
    exit 1
}

if (-not (Test-Path $SevenZipPath)) {
    Write-Error "7z.exe not found at: $SevenZipPath"
    exit 1
}

$DefaultFileList = Join-Path $PSScriptRoot "7zip_listfile_LervikMCP.txt"
$ResolvedFileList = if ($FileList) { $FileList } else { $DefaultFileList }
if (-not (Test-Path $ResolvedFileList)) {
    Write-Error "7-Zip file list not found: $ResolvedFileList"
    exit 1
}

# --- Zip output ---
$FabDir = Join-Path $PluginRoot "Fab"
if (-not (Test-Path $FabDir)) {
    New-Item -ItemType Directory -Path $FabDir | Out-Null
}

# Version string for zip name: "5.4" -> "54"
$VersionTag = $Version -replace '\.', ''
$ZipPath = Join-Path $FabDir "LervikMCP_$VersionTag.zip"

Write-Host ""
Write-Host "=== LervikMCP Plugin Package ===" -ForegroundColor Cyan
Write-Host "  Version:   $Version" -ForegroundColor Gray
Write-Host "  Plugin:    $UPluginFile" -ForegroundColor Gray
Write-Host "  FileList:  $ResolvedFileList" -ForegroundColor Gray
Write-Host "  Output:    $ZipPath" -ForegroundColor Gray
Write-Host ""

# Remove existing zip
if (Test-Path $ZipPath) {
    Write-Host "Removing existing zip: $ZipPath" -ForegroundColor DarkYellow
    Remove-Item -Force $ZipPath
}

# --- Patch uplugin: inject EngineVersion after SupportURL line ---
$original = Get-Content $UPluginFile -Raw
$engineVersionLine = "  `"EngineVersion`": `"$Version.0`","
$patched = $original -replace '"SupportURL": "",', ('"SupportURL": "",' + [Environment]::NewLine + $engineVersionLine)

if ($patched -eq $original) {
    Write-Warning "SupportURL line not found in uplugin -- EngineVersion NOT injected."
} else {
    Set-Content $UPluginFile -Value $patched -NoNewline
    Write-Host "Injected EngineVersion $Version.0 into uplugin" -ForegroundColor DarkGray
}

try {
    # --- Zip (run from plugin root so relative paths in list file are correct) ---
    Push-Location $PluginRoot
    & $SevenZipPath a -tzip $ZipPath "@$ResolvedFileList"
    $exitCode = $LASTEXITCODE
    Pop-Location

    if ($exitCode -ne 0) {
        Write-Host ""
        Write-Host "=== Package FAILED (7z exit code: $exitCode) ===" -ForegroundColor Red
        exit $exitCode
    }
} finally {
    # --- Revert uplugin regardless of success/failure ---
    Set-Content $UPluginFile -Value $original -NoNewline
    Write-Host "Reverted uplugin to original" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "=== Package SUCCEEDED ===" -ForegroundColor Green
Write-Host "  Zip: $ZipPath" -ForegroundColor Gray
