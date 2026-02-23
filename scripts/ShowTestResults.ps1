# ShowTestResults.ps1 - Display LervikMCP test results with timing info
param(
    [string]$ResultsPath = ".\TestResults\index.json",
    [switch]$Compact
)

if (-not (Test-Path $ResultsPath)) {
    Write-Host "Test results not found: $ResultsPath" -ForegroundColor Red
    exit 1
}

$results = Get-Content $ResultsPath | ConvertFrom-Json

# Header
Write-Host ""
Write-Host "========================================"
Write-Host " LervikMCP Test Results"
Write-Host " $($results.reportCreatedOn)"
Write-Host "========================================"
Write-Host ""

# Summary
$totalTests = $results.succeeded + $results.succeededWithWarnings + $results.failed + $results.notRun
$passColor = if ($results.failed -eq 0) { "Green" } else { "Red" }

Write-Host "Passed: $($results.succeeded)/$totalTests" -ForegroundColor $passColor
if ($results.succeededWithWarnings -gt 0) {
    Write-Host "Warnings: $($results.succeededWithWarnings)" -ForegroundColor Yellow
}
if ($results.failed -gt 0) {
    Write-Host "Failed: $($results.failed)" -ForegroundColor Red
}
Write-Host "Total Duration: $([math]::Round($results.totalDuration * 1000, 2)) ms"
Write-Host ""
Write-Host "----------------------------------------"

# Each test
foreach ($test in $results.tests) {
    $stateColor = switch ($test.state) {
        "Success" { "Green" }
        "Failure" { "Red" }
        "InProcess" { "Cyan" }
        default { "Gray" }
    }
    
    $stateIcon = switch ($test.state) {
        "Success" { "[PASS]" }
        "Failure" { "[FAIL]" }
        "InProcess" { "[RUN]" }
        default { "[?]" }
    }
    
    Write-Host ""
    # In compact mode, skip passing tests entirely
    if ($Compact -and $test.state -eq "Success") { continue }

    Write-Host "$stateIcon $($test.testDisplayName)" -ForegroundColor $stateColor
    
    # Print event messages (timing info) — skipped in compact mode for passing tests
    if (-not $Compact) {
        foreach ($entry in $test.entries) {
            if ($entry.event -and $entry.event.message) {
                $msgColor = switch ($entry.event.type) {
                    "Info" { "Cyan" }
                    "Warning" { "Yellow" }
                    "Error" { "Red" }
                    default { "White" }
                }
                Write-Host "  $($entry.event.message)" -ForegroundColor $msgColor
            }
        }
    } else {
        # In compact mode, show error/warning entries for failed tests
        foreach ($entry in $test.entries) {
            if ($entry.event -and $entry.event.message -and $entry.event.type -in @("Error", "Warning")) {
                $msgColor = if ($entry.event.type -eq "Error") { "Red" } else { "Yellow" }
                Write-Host "  $($entry.event.message)" -ForegroundColor $msgColor
            }
        }
    }
}

Write-Host ""
Write-Host "----------------------------------------"
Write-Host ""
