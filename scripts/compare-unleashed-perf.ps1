param(
    [Parameter(Mandatory=$true)]
    [string]$Baseline,
    [Parameter(Mandatory=$true)]
    [string]$Candidate,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

function Load-Summary($path) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Summary not found: $path"
    }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

function MetricAvg($summary, $name) {
    if ($null -eq $summary.$name -or $null -eq $summary.$name.avg) { return $null }
    try { return [double]$summary.$name.avg } catch { return $null }
}

function MetricMax($summary, $name) {
    if ($null -eq $summary.$name -or $null -eq $summary.$name.max) { return $null }
    try { return [double]$summary.$name.max } catch { return $null }
}

function NumberOrNull($value) {
    if ($null -eq $value) { return $null }
    try { return [double]$value } catch { return $null }
}

function Delta($before, $after) {
    if ($null -eq $before -or $null -eq $after) { return $null }
    return [math]::Round($after - $before, 3)
}

function PercentDelta($before, $after) {
    if ($null -eq $before -or $null -eq $after -or [math]::Abs($before) -lt 0.000001) {
        return $null
    }
    return [math]::Round((($after - $before) / $before) * 100.0, 2)
}

function Extract-Perf($summary, $name) {
    $mapLookup = MetricAvg $summary 'scan_cn_ne_map_candidate_cache_lookup_count'
    $mapHits = MetricAvg $summary 'scan_cn_ne_map_candidate_cache_hit_count'
    $mapHitRate = if ($null -ne $mapLookup -and $mapLookup -gt 0) {
        [math]::Round(($mapHits / $mapLookup) * 100.0, 2)
    } else {
        $null
    }

    $parentLookup = MetricAvg $summary 'scan_cn_ne_map_candidate_parent_lookup_count'
    $duplicateParents = MetricAvg $summary 'scan_cn_ne_map_candidate_duplicate_parent_count'
    $duplicateParentRate = if ($null -ne $parentLookup -and $parentLookup -gt 0) {
        [math]::Round(($duplicateParents / $parentLookup) * 100.0, 2)
    } else {
        $null
    }

    return [ordered]@{
        name = $name
        label = $summary.label
        samples = NumberOrNull $summary.sample_count
        scan_ms_avg = MetricAvg $summary 'scan_get_ow_entities_ms'
        scan_ms_max = MetricMax $summary 'scan_get_ow_entities_ms'
        record_build_ms_avg = MetricAvg $summary 'scan_record_build_ms'
        match_link_ms_avg = MetricAvg $summary 'scan_match_link_ms'
        target_map_ms_avg = MetricAvg $summary 'scan_cn_ne_target_map_ms'
        process_ms_avg = MetricAvg $summary 'entity_cycle_ms'
        process_ms_max = MetricMax $summary 'entity_cycle_ms'
        publish_hz_avg = MetricAvg $summary 'entity_publish_hz'
        publish_age_ms_max = MetricMax $summary 'entity_publish_age_ms'
        projection_jumps = NumberOrNull $summary.projection_jump_samples
        map_cache_enabled = [bool]$summary.scan_cn_ne_map_candidate_cache_enabled
        map_cache_hit_rate_percent = $mapHitRate
        map_diag_enabled = [bool]$summary.scan_cn_ne_map_diag_enabled
        map_parent_lookup_avg = $parentLookup
        map_duplicate_parent_rate_percent = $duplicateParentRate
    }
}

$baselineSummary = Load-Summary $Baseline
$candidateSummary = Load-Summary $Candidate
$before = Extract-Perf $baselineSummary 'baseline'
$after = Extract-Perf $candidateSummary 'candidate'

$scanPct = PercentDelta $before.scan_ms_avg $after.scan_ms_avg
$targetMapPct = PercentDelta $before.target_map_ms_avg $after.target_map_ms_avg
$processMaxPct = PercentDelta $before.process_ms_max $after.process_ms_max
$publishHzPct = PercentDelta $before.publish_hz_avg $after.publish_hz_avg
$publishAgePct = PercentDelta $before.publish_age_ms_max $after.publish_age_ms_max

$verdict = 'inconclusive'
if ($null -ne $scanPct) {
    if ($scanPct -le -15.0 -and
        ($null -eq $publishAgePct -or $publishAgePct -le 25.0) -and
        ($null -eq $processMaxPct -or $processMaxPct -le 25.0) -and
        ($after.projection_jumps -le $before.projection_jumps + 2)) {
        $verdict = 'candidate-improved'
    } elseif ($scanPct -ge 10.0 -or
              ($null -ne $publishAgePct -and $publishAgePct -gt 50.0) -or
              ($null -ne $processMaxPct -and $processMaxPct -gt 50.0) -or
              ($after.projection_jumps -gt $before.projection_jumps + 10)) {
        $verdict = 'candidate-regressed'
    }
}

$comparison = [ordered]@{
    baseline_path = (Resolve-Path -LiteralPath $Baseline).Path
    candidate_path = (Resolve-Path -LiteralPath $Candidate).Path
    verdict = $verdict
    baseline = $before
    candidate = $after
    delta = [ordered]@{
        scan_ms_avg = Delta $before.scan_ms_avg $after.scan_ms_avg
        scan_ms_avg_percent = $scanPct
        target_map_ms_avg = Delta $before.target_map_ms_avg $after.target_map_ms_avg
        target_map_ms_avg_percent = $targetMapPct
        process_ms_avg = Delta $before.process_ms_avg $after.process_ms_avg
        process_ms_max = Delta $before.process_ms_max $after.process_ms_max
        process_ms_max_percent = $processMaxPct
        publish_hz_avg = Delta $before.publish_hz_avg $after.publish_hz_avg
        publish_hz_avg_percent = $publishHzPct
        publish_age_ms_max = Delta $before.publish_age_ms_max $after.publish_age_ms_max
        publish_age_ms_max_percent = $publishAgePct
        projection_jumps = Delta $before.projection_jumps $after.projection_jumps
        map_cache_hit_rate_percent = Delta $before.map_cache_hit_rate_percent $after.map_cache_hit_rate_percent
        map_duplicate_parent_rate_percent = Delta $before.map_duplicate_parent_rate_percent $after.map_duplicate_parent_rate_percent
    }
}

if ($Json) {
    $comparison | ConvertTo-Json -Depth 8
    return
}

[pscustomobject]@{
    verdict = $comparison.verdict
    scan_ms_avg_delta = $comparison.delta.scan_ms_avg
    scan_ms_avg_percent = $comparison.delta.scan_ms_avg_percent
    target_map_ms_avg_delta = $comparison.delta.target_map_ms_avg
    target_map_ms_avg_percent = $comparison.delta.target_map_ms_avg_percent
    process_ms_max_percent = $comparison.delta.process_ms_max_percent
    publish_hz_avg_percent = $comparison.delta.publish_hz_avg_percent
    publish_age_ms_max_percent = $comparison.delta.publish_age_ms_max_percent
    projection_jumps_delta = $comparison.delta.projection_jumps
} | Format-List
