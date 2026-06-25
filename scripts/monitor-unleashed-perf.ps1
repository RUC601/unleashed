param(
    [int]$Seconds = 60,
    [int]$IntervalSeconds = 2,
    [string]$BaseUrl = 'http://127.0.0.1:19550',
    [string]$DiagLog = (Join-Path $PSScriptRoot '..\build\Release\unleashed_diag.log'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\logs')
)

$ErrorActionPreference = 'Stop'

function As-DoubleOrNull($value) {
    if ($null -eq $value) { return $null }
    try { return [double]$value } catch { return $null }
}

function As-Int64OrZero($value) {
    if ($null -eq $value) { return [int64]0 }
    try { return [int64]$value } catch { return [int64]0 }
}

function Measure-SampleField($samples, $property) {
    $values = @(
        $samples |
            ForEach-Object { $_.$property } |
            Where-Object { $null -ne $_ } |
            ForEach-Object { [double]$_ }
    )

    if ($values.Count -eq 0) {
        return [ordered]@{ avg = $null; min = $null; max = $null }
    }

    $m = $values | Measure-Object -Average -Minimum -Maximum
    return [ordered]@{
        avg = [math]::Round($m.Average, 3)
        min = [math]::Round($m.Minimum, 3)
        max = [math]::Round($m.Maximum, 3)
    }
}

function Read-NewLogLines($path, $skip) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    return @(Get-Content -LiteralPath $path | Select-Object -Skip $skip)
}

function Count-LogLines($path) {
    if (-not (Test-Path -LiteralPath $path)) { return 0 }
    return (Get-Content -LiteralPath $path | Measure-Object -Line).Lines
}

$diagUrl = ($BaseUrl.TrimEnd('/')) + '/api/diagnostics'
$startLineCount = Count-LogLines $DiagLog
$samples = New-Object System.Collections.Generic.List[object]
$startedAt = Get-Date
$deadline = $startedAt.AddSeconds($Seconds)

Write-Host "Sampling $diagUrl for $Seconds seconds every $IntervalSeconds seconds..."

while ((Get-Date) -lt $deadline) {
    try {
        $response = Invoke-RestMethod -Uri $diagUrl -TimeoutSec 2
        $d = $response.diagnostics
        $r = $d.render
        $pi = $d.player_info
        $screen = $d.screen
        $projected = $pi.sample_projected
        $dma = $d.dma_reads

        $sample = [pscustomobject]@{
            captured_at = (Get-Date)
            fps = As-DoubleOrNull $d.fps
            entity_count = As-Int64OrZero $d.entity_count
            entity_process_hz = As-DoubleOrNull $d.entity_process_hz
            entity_scan_hz = As-DoubleOrNull $d.entity_scan_hz
            view_matrix_ok = [bool]$d.view_matrix_ok
            view_matrix_resolved = [bool]$d.view_matrix_resolved
            view_matrix_valid = [bool]$d.view_matrix_valid
            manual_width = As-Int64OrZero $screen.manual_width
            manual_height = As-Int64OrZero $screen.manual_height
            detected_width = As-Int64OrZero $screen.detected_width
            detected_height = As-Int64OrZero $screen.detected_height
            resolved_wx = As-DoubleOrNull $screen.resolved_wx
            resolved_wy = As-DoubleOrNull $screen.resolved_wy
            player_info_input = As-Int64OrZero $pi.input
            player_info_projected = As-Int64OrZero $pi.projected
            player_info_drawn = As-Int64OrZero $pi.drawn
            skipped_world_to_screen = As-Int64OrZero $pi.skipped_world_to_screen
            sample_projected_available = [bool]$projected.available
            sample_projected_left = As-Int64OrZero $projected.left
            sample_projected_top = As-Int64OrZero $projected.top
            sample_projected_width = As-Int64OrZero $projected.width
            sample_projected_height = As-Int64OrZero $projected.height
            dma_total = As-Int64OrZero $dma.total
            dma_failed = As-Int64OrZero $dma.failed
            dma_avg_latency_us = As-DoubleOrNull $dma.avg_latency_us
            dma_max_latency_us = As-DoubleOrNull $dma.max_latency_us
            render_frame_ms = As-DoubleOrNull $r.frame_ms
            render_callback_ms = As-DoubleOrNull $r.render_callback_ms
            present_ms = As-DoubleOrNull $r.present_ms
            render_mode = $r.mode
            player_info_called = [bool]$r.player_info_called
            skill_info_called = [bool]$r.skill_info_called
            entity_list_empty = [bool]$r.entity_list_empty
        }
        $samples.Add($sample) | Out-Null

        Write-Host ("{0} fps={1:n1} entities={2} vm={3} screen={4:n0}x{5:n0} sample=({6},{7}) render={8:n2}ms rt_dmatotal={9}" -f `
            (Get-Date -Format 'HH:mm:ss'),
            $sample.fps,
            $sample.entity_count,
            $sample.view_matrix_ok,
            $sample.resolved_wx,
            $sample.resolved_wy,
            $sample.sample_projected_left,
            $sample.sample_projected_top,
            $sample.render_callback_ms,
            $sample.dma_total)
    }
    catch {
        Write-Warning ("sample failed: {0}" -f $_.Exception.Message)
    }

    Start-Sleep -Seconds $IntervalSeconds
}

if ($samples.Count -eq 0) {
    throw "No diagnostics samples collected. Start Unleashed with --test-server --test-server-port 19550 first."
}

$newLogLines = Read-NewLogLines $DiagLog $startLineCount
$slowLines = @($newLogLines | Where-Object { $_ -match 'SLOW_FRAME' })
$rtDmaNonZero = @($slowLines | Where-Object { $_ -match 'rtDma\[reads=([1-9][0-9]*)' }).Count
$renderCanvasNonZero = @($slowLines | Where-Object { $_ -match 'RenderCanvas\[rd=([1-9][0-9]*)' }).Count
$slowFrameEffectiveCount = 0
$slowRenderMaxMs = $null

foreach ($line in $slowLines) {
    if ($line -match 'slowCount=(\d+)') {
        $slowFrameEffectiveCount += [int]$matches[1]
    } else {
        $slowFrameEffectiveCount += 1
    }

    if ($line -match 'render=([0-9.]+)ms') {
        $renderMs = [double]$matches[1]
        if ($null -eq $slowRenderMaxMs -or $renderMs -gt $slowRenderMaxMs) {
            $slowRenderMaxMs = $renderMs
        }
    }
}

$first = $samples[0]
$last = $samples[$samples.Count - 1]
$durationSeconds = [math]::Max(0.001, (($last.captured_at) - ($first.captured_at)).TotalSeconds)
$dmaTotalDelta = [math]::Max(0, $last.dma_total - $first.dma_total)
$dmaFailDelta = [math]::Max(0, $last.dma_failed - $first.dma_failed)
$viewMatrixBadSamples = @($samples | Where-Object { -not $_.view_matrix_ok }).Count
$resolvedSizeKeys = @(
    $samples |
        ForEach-Object { "{0}x{1}" -f $_.resolved_wx, $_.resolved_wy } |
        Sort-Object -Unique
)
$projectionJumpSamples = 0
$previousProjectedSample = $null
foreach ($sample in $samples) {
    if (-not $sample.sample_projected_available) {
        continue
    }
    if ($null -ne $previousProjectedSample) {
        $dx = [math]::Abs($sample.sample_projected_left - $previousProjectedSample.sample_projected_left)
        $dy = [math]::Abs($sample.sample_projected_top - $previousProjectedSample.sample_projected_top)
        if (($dx + $dy) -gt 150) {
            $projectionJumpSamples++
        }
    }
    $previousProjectedSample = $sample
}

$summary = [ordered]@{
    started_at = $startedAt.ToString('o')
    ended_at = (Get-Date).ToString('o')
    sample_count = $samples.Count
    duration_seconds = [math]::Round($durationSeconds, 3)
    fps = Measure-SampleField $samples 'fps'
    entity_process_hz = Measure-SampleField $samples 'entity_process_hz'
    entity_scan_hz = Measure-SampleField $samples 'entity_scan_hz'
    entity_count = Measure-SampleField $samples 'entity_count'
    view_matrix_bad_samples = $viewMatrixBadSamples
    resolved_screen_sizes = $resolvedSizeKeys
    projection_jump_samples = $projectionJumpSamples
    render_callback_ms = Measure-SampleField $samples 'render_callback_ms'
    present_ms = Measure-SampleField $samples 'present_ms'
    render_frame_ms = Measure-SampleField $samples 'render_frame_ms'
    dma_reads_per_second = [math]::Round($dmaTotalDelta / $durationSeconds, 3)
    dma_fail_rate_percent = if ($dmaTotalDelta -gt 0) {
        [math]::Round(($dmaFailDelta / $dmaTotalDelta) * 100.0, 3)
    } else {
        $null
    }
    dma_total_delta = $dmaTotalDelta
    dma_fail_delta = $dmaFailDelta
    slow_frame_log_lines = $slowLines.Count
    slow_frame_effective_count = $slowFrameEffectiveCount
    slow_frame_max_render_ms = $slowRenderMaxMs
    slow_frame_rt_dma_nonzero_lines = $rtDmaNonZero
    slow_frame_render_canvas_nonzero_lines = $renderCanvasNonZero
    log_path = (Resolve-Path -LiteralPath $DiagLog -ErrorAction SilentlyContinue).Path
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$samplePath = Join-Path $OutDir "perf-monitor-$stamp.samples.csv"
$summaryPath = Join-Path $OutDir "perf-monitor-$stamp.summary.json"

$samples | Export-Csv -NoTypeInformation -LiteralPath $samplePath
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host ""
Write-Host "Summary:"
($summary | ConvertTo-Json -Depth 8)
Write-Host ""
Write-Host "Wrote samples: $samplePath"
Write-Host "Wrote summary: $summaryPath"
