<#
.SYNOPSIS
Run the fixed-rate benchmark against the Windows Debug and Release servers.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File server/tests/run_fixed_rate_windows.ps1 -Quick
.EXAMPLE
./server/tests/run_fixed_rate_windows.ps1 -Rates 60 -Duration 120 -Repetitions 5
#>
[CmdletBinding()]
param(
    [switch]$Quick,
    [ValidateSet(10, 12)]
    [int]$Players = 12,
    [ValidateRange(1, 2147483647)]
    [int[]]$Rates = @(30, 60, 120),
    [ValidateRange(0.001, 86400)]
    [double]$Warmup = 10,
    [ValidateRange(0.001, 86400)]
    [double]$Duration = 120,
    [ValidateRange(1, 2147483647)]
    [int]$Repetitions = 5,
    [ValidateRange(0.001, 86400)]
    [double]$Timeout = 2,
    [ValidateSet('process', 'thread')]
    [string]$ReceiverMode = 'process',
    [ValidateSet('spread', 'burst')]
    [string]$PlayerTiming = 'spread',
    [ValidateRange(0, 86400000)]
    [double[]]$PhaseOffsetsMs = @(0, 4, 8, 12),
    [string]$Output,
    [string]$PythonExecutable = 'python'
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') {
    throw 'This launcher requires Windows.'
}

$serverRoot = Split-Path -Parent $PSScriptRoot
if (-not $Output) {
    $Output = Join-Path $PSScriptRoot 'latency_results'
}
$debugServer = Join-Path $serverRoot 'build/x64-debug/main.exe'
$releaseServer = Join-Path $serverRoot 'build/x64-release/main.exe'
foreach ($serverPath in @($debugServer, $releaseServer)) {
    if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
        throw "Server executable missing: $serverPath. Build x64-debug and x64-release first."
    }
}
$null = Get-Command $PythonExecutable -ErrorAction Stop

if ($Quick) {
    $Rates = @(60)
    $Warmup = 1
    $Duration = 3
    $Repetitions = 1
    Write-Host 'Quick check: 60 pps, 1s warmup, 3s measurement, one Debug/Release pair.'
    Write-Host 'Use the full run for performance conclusions.'
}

# Use invariant decimal formatting even when Windows uses a decimal comma.
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$benchmarkArguments = @(
    '-u', (Join-Path $PSScriptRoot 'fixed_rate_benchmark.py'),
    '--debug', $debugServer, '--release', $releaseServer,
    '--players', $Players.ToString($culture), '--rates'
)
$benchmarkArguments += @($Rates | ForEach-Object { $_.ToString($culture) })
$benchmarkArguments += @(
    '--warmup', $Warmup.ToString($culture),
    '--duration', $Duration.ToString($culture),
    '--repetitions', $Repetitions.ToString($culture),
    '--timeout', $Timeout.ToString($culture),
    '--receiver-mode', $ReceiverMode, '--player-timing', $PlayerTiming,
    '--output', $Output, '--phase-offsets-ms'
)
$benchmarkArguments += @($PhaseOffsetsMs | ForEach-Object { $_.ToString($culture) })

# UTF-8 keeps Korean progress messages readable when output is redirected.
$previousPythonUtf8 = $env:PYTHONUTF8
try {
    $env:PYTHONUTF8 = '1'
    & $PythonExecutable @benchmarkArguments
    $benchmarkExitCode = $LASTEXITCODE
}
finally {
    $env:PYTHONUTF8 = $previousPythonUtf8
}
exit $benchmarkExitCode
