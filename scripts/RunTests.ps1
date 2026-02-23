# RunTests.ps1 - Build and run LervikMCP automation tests
# Usage: .\RunTests.ps1 [-NoBuild] [-ShowResults] [-TestFilter <filter>]
param(
    [switch]$NoBuild,
    [switch]$ShowResults,
    [string]$TestFilter = "Plugins.LervikMCP"
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\Resolve-UEPath.ps1"

$ProjectRoot  = (Resolve-Path "$PSScriptRoot\..").Path
$ProjectFile  = Get-ChildItem -Path $ProjectRoot -Filter "*.uproject" | Select-Object -First 1 -ExpandProperty FullName
if (-not $ProjectFile) {
    Write-Host "[ERROR] No .uproject file found in $ProjectRoot" -ForegroundColor Red
    exit 1
}
$UEPath       = Resolve-UEPath -Version "5.4"
$EditorCmd    = "$UEPath\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$TestResults  = "$ProjectRoot\TestResults"

# Ensure TestResults folder exists
if (-not (Test-Path $TestResults)) {
    New-Item -ItemType Directory -Path $TestResults | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile   = "$TestResults\LervikMCP_TestLog_$timestamp.log"

Write-Host ""
Write-Host "========================================"
Write-Host " LervikMCP Test Runner"
Write-Host "========================================"
Write-Host " Project:  $ProjectFile"
Write-Host " Filter:   $TestFilter"
Write-Host " NoBuild:  $NoBuild"
Write-Host " Results:  $TestResults"
Write-Host "========================================"
Write-Host ""

# --- Step 1: Build (unless -NoBuild) ---
if (-not $NoBuild) {
    Write-Host "[BUILD] Compiling Editor (Development)..." -ForegroundColor Cyan
    & "$PSScriptRoot\build.ps1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Build failed with exit code $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host "[BUILD] Complete" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "[SKIP] Build skipped via -NoBuild flag" -ForegroundColor Yellow
    Write-Host ""
}

# --- Step 2: Run Tests ---
Write-Host "[TEST] Running automation tests: $TestFilter" -ForegroundColor Cyan
Write-Host ""

& "$EditorCmd" "$ProjectFile" `
    -ExecCmds="Automation RunTests $TestFilter;Quit" `
    -unattended -nopause -nosplash -nosound `
    -stdout -fullstdoutlogoutput `
    -ReportOutputPath="$TestResults" `
    -TestExit="Automation Test Queue Empty" 2>&1 |
    Tee-Object -Append -FilePath $logFile |
    Select-String -Pattern "LogAutomation.*(Pass|Fail|Error|Success|Complete|Found \d+)" |
    ForEach-Object { $_.Line -replace '^\[.+?\]\[.+?\]', '' }

$testExit = $LASTEXITCODE

# --- Step 3: Report ---
Write-Host ""
Write-Host "========================================"
if ($testExit -eq 0) {
    Write-Host " [PASS] All LervikMCP tests passed" -ForegroundColor Green
} else {
    Write-Host " [FAIL] Tests failed with exit code $testExit" -ForegroundColor Red
}
Write-Host " Log: $logFile"
Write-Host "========================================"
Write-Host ""

# --- Step 4: Show detailed results (optional) ---
if ($ShowResults) {
    $resultsJson = "$TestResults\index.json"
    if (Test-Path $resultsJson) {
        & "$PSScriptRoot\ShowTestResults.ps1" -ResultsPath $resultsJson
    } else {
        Write-Host "Results file not found: $resultsJson" -ForegroundColor Yellow
    }
}

exit $testExit
