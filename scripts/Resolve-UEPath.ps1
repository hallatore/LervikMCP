<#
.SYNOPSIS
    Shared helper: resolves a UE engine installation path by version.

.DESCRIPTION
    Dot-source this file, then call Resolve-UEPath. Throws on failure.
    Resolution order:
      1. -EnginePath param (if provided and exists)
      2. Version-specific environment variable
      3. Registry
      4. Common installation paths

.EXAMPLE
    . "$PSScriptRoot\Resolve-UEPath.ps1"
    $Engine = Resolve-UEPath -Version "5.4" -EnginePath $EnginePath
#>

function Resolve-UEPath {
    param(
        [string]$Version,           # e.g. 5.4, 5.5, 5.7
        [string]$EnginePath         # optional explicit override
    )

    if (-not $Version -and -not $EnginePath) {
        throw "Must provide -Version or -EnginePath"
    }

    # 1. Explicit override
    if ($EnginePath -and (Test-Path $EnginePath)) {
        return $EnginePath
    }

    # 2. Environment variable (version-specific)
    $versionKey = $Version -replace '\.', ''
    $envVarName = "UE_${versionKey}_ROOT"
    $envVars = @($envVarName)
    if ($Version -like "5.*") { $envVars += "UE5_ROOT" }
    foreach ($varName in $envVars) {
        $val = [System.Environment]::GetEnvironmentVariable($varName)
        if ($val -and (Test-Path $val)) { return $val }
    }

    # 3. Registry
    $regPaths = @(
        "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Version",
        "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$Version"
    )
    foreach ($rp in $regPaths) {
        if (Test-Path $rp) {
            $val = (Get-ItemProperty -Path $rp -Name "InstalledDirectory" -ErrorAction SilentlyContinue).InstalledDirectory
            if ($val -and (Test-Path $val)) { return $val }
        }
    }

    # 4. Common installation paths
    $suffix = "UE_$Version"
    $commonPaths = @(
        "G:\Epic Games\$suffix",
        "C:\Program Files\Epic Games\$suffix",
        "D:\Program Files\Epic Games\$suffix",
        "E:\Program Files\Epic Games\$suffix",
        "F:\Program Files\Epic Games\$suffix",
        "C:\Epic Games\$suffix",
        "D:\Epic Games\$suffix",
        "E:\Epic Games\$suffix",
        "F:\Epic Games\$suffix",
        "C:\$suffix",
        "D:\$suffix",
        "E:\$suffix",
        "F:\$suffix",
        "G:\$suffix"
    )
    foreach ($cp in $commonPaths) {
        if (Test-Path $cp) { return $cp }
    }

    throw "Could not find UE $Version installation. Options:`n  1. Pass -EnginePath `"C:\Path\To\UE_$Version`"`n  2. Set environment variable $envVarName`n  3. Install UE $Version to a standard location"
}
