param(
    [ValidateSet('Baseline', 'MapDiag')]
    [string]$Phase = 'Baseline',
    [int]$Seconds = 120,
    [int]$IntervalSeconds = 2,
    [string]$BaseUrl = 'http://127.0.0.1:19550',
    [string]$OutDir = (Join-Path $PSScriptRoot '..\logs'),
    [string]$CompareWith = ''
)

$ErrorActionPreference = 'Stop'

$monitorScript = Join-Path $PSScriptRoot 'monitor-unleashed-perf.ps1'
$analyzeScript = Join-Path $PSScriptRoot 'analyze-unleashed-perf.ps1'
$compareScript = Join-Path $PSScriptRoot 'compare-unleashed-perf.ps1'

function Invoke-Preflight {
    $output = & $monitorScript -PreflightOnly -BaseUrl $BaseUrl 2>&1
    $text = ($output | Out-String).Trim()
    try {
        return $text | ConvertFrom-Json
    }
    catch {
        throw "Preflight did not return JSON. Output: $text"
    }
}

function New-StartupHint($phase) {
    if ($phase -eq 'MapDiag') {
        return @"
Start the runtime from a shell that sets the runtime gate before launch:

`$env:UN_DMA_CN_NE_MAP_DIAG='1'
.\build\Release\Unleashed.exe --test-server --test-server-port 19550
"@
    }

    return @"
Start the runtime with mapdiag unset for a clean baseline:

Remove-Item Env:\UN_DMA_CN_NE_MAP_DIAG -ErrorAction SilentlyContinue
.\build\Release\Unleashed.exe --test-server --test-server-port 19550
"@
}

$preflight = Invoke-Preflight
if (-not $preflight.diagnostics_ok) {
    throw "No diagnostics endpoint at $BaseUrl. $(New-StartupHint $Phase)"
}
if (-not $preflight.process_connected) {
    throw "Test server is reachable but the target process is not connected. Wait for process.connected=true before collecting perf evidence."
}

$runtimeMapDiag = $false
if ($preflight.runtime_gates -and $null -ne $preflight.runtime_gates.cn_ne_map_diag_enabled) {
    $runtimeMapDiag = [bool]$preflight.runtime_gates.cn_ne_map_diag_enabled
}

if ($Phase -eq 'Baseline' -and $runtimeMapDiag) {
    throw "Refusing to collect Baseline because runtime cn_ne_map_diag_enabled=true. $(New-StartupHint $Phase)"
}
if ($Phase -eq 'MapDiag' -and -not $runtimeMapDiag) {
    throw "Refusing to collect MapDiag because runtime cn_ne_map_diag_enabled=false. $(New-StartupHint $Phase)"
}

$label = if ($Phase -eq 'MapDiag') { 'mapdiag' } else { 'baseline' }
$started = Get-Date
& $monitorScript -Seconds $Seconds -IntervalSeconds $IntervalSeconds -BaseUrl $BaseUrl -OutDir $OutDir -Label $label
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$latestSummary = Get-ChildItem -Path $OutDir -Filter "perf-monitor-$label-*.summary.json" |
    Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-2) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $latestSummary) {
    throw "Monitor completed but no matching summary was found for label '$label' in $OutDir."
}

Write-Host ""
Write-Host "Evidence summary:"
& $analyzeScript -SummaryGlob $latestSummary.FullName -Top 1

if (-not [string]::IsNullOrWhiteSpace($CompareWith)) {
    Write-Host ""
    Write-Host "Comparison:"
    if ($Phase -eq 'Baseline') {
        & $compareScript -Baseline $latestSummary.FullName -Candidate $CompareWith
    } else {
        & $compareScript -Baseline $CompareWith -Candidate $latestSummary.FullName
    }
}
