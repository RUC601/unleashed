param(
    [string]$SummaryGlob = (Join-Path $PSScriptRoot '..\logs\perf-monitor-*.summary.json'),
    [int]$Top = 12,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

function MetricAvg($summary, $name) {
    if ($null -eq $summary.$name -or $null -eq $summary.$name.avg) { return $null }
    try { return [double]$summary.$name.avg } catch { return $null }
}

function MetricMax($summary, $name) {
    if ($null -eq $summary.$name -or $null -eq $summary.$name.max) { return $null }
    try { return [double]$summary.$name.max } catch { return $null }
}

function BoolValue($value) {
    if ($null -eq $value) { return $false }
    try { return [bool]$value } catch { return $false }
}

function PickMaxPhase($pairs) {
    $bestName = $null
    $bestValue = $null
    foreach ($pair in $pairs.GetEnumerator()) {
        $value = $pair.Value
        if ($null -eq $value) { continue }
        if ($null -eq $bestValue -or $value -gt $bestValue) {
            $bestName = $pair.Key
            $bestValue = $value
        }
    }
    return [pscustomobject]@{
        name = $bestName
        value = $bestValue
    }
}

$files = @(Get-ChildItem -Path $SummaryGlob -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
if ($files.Count -eq 0) {
    throw "No summary files matched: $SummaryGlob"
}

$rows = foreach ($file in $files) {
    $summary = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json

    $scanAvg = MetricAvg $summary 'scan_get_ow_entities_ms'
    $scanMax = MetricMax $summary 'scan_get_ow_entities_ms'
    $processAvg = MetricAvg $summary 'entity_cycle_ms'
    $processMax = MetricMax $summary 'entity_cycle_ms'
    $publishHz = MetricAvg $summary 'entity_publish_hz'
    $publishAgeMax = MetricMax $summary 'entity_publish_age_ms'
    $jumps = if ($null -ne $summary.projection_jump_samples) { [int]$summary.projection_jump_samples } else { 0 }
    $recordBuildAvg = MetricAvg $summary 'scan_record_build_ms'
    $matchLinkAvg = MetricAvg $summary 'scan_match_link_ms'
    $targetMapAvg = MetricAvg $summary 'scan_cn_ne_target_map_ms'

    $scanPhase = PickMaxPhase ([ordered]@{
        record_build = $recordBuildAvg
        match_link = $matchLinkAvg
        cn_ne_target_map = $targetMapAvg
        list_read = MetricAvg $summary 'scan_list_read_ms'
    })

    $processPhase = PickMaxPhase ([ordered]@{
        begin_frame = MetricAvg $summary 'phase_begin_frame_ms'
        prefetch = MetricAvg $summary 'phase_prefetch_ms'
        previous_index = MetricAvg $summary 'phase_previous_index_ms'
        hot_scatter = MetricAvg $summary 'phase_hot_scatter_execute_ms'
        base_decrypt = MetricAvg $summary 'phase_base_decrypt_ms'
        skeleton = MetricAvg $summary 'phase_skeleton_ms'
        skeleton_velocity_read = MetricAvg $summary 'phase_skeleton_velocity_read_ms'
        skeleton_cache_call = MetricAvg $summary 'phase_skeleton_cache_call_ms'
        skill = MetricAvg $summary 'phase_skill_ms'
        team_name = MetricAvg $summary 'phase_team_name_ms'
        team_name_hero_lookup = MetricAvg $summary 'phase_team_name_hero_lookup_ms'
        team_name_bot_adjust = MetricAvg $summary 'phase_team_name_bot_adjust_ms'
        team_name_battle_tag = MetricAvg $summary 'phase_team_name_battle_tag_ms'
        team_name_team_read = MetricAvg $summary 'phase_team_name_team_read_ms'
        local_select = MetricAvg $summary 'phase_local_select_ms'
        publish = MetricAvg $summary 'phase_publish_ms'
        record_sync = MetricAvg $summary 'phase_record_sync_ms'
        entity_loop_setup = MetricAvg $summary 'phase_entity_loop_setup_ms'
        entity_header_special = MetricAvg $summary 'phase_entity_header_special_ms'
        entity_cache_apply = MetricAvg $summary 'phase_entity_cache_apply_ms'
        entity_hot_fields = MetricAvg $summary 'phase_entity_hot_fields_ms'
        entity_rotation_position = MetricAvg $summary 'phase_entity_rotation_position_ms'
        entity_loop_gap = MetricAvg $summary 'phase_entity_loop_gap_ms'
        cycle_gap = MetricAvg $summary 'phase_cycle_gap_ms'
    })

    $mapCacheEnabled = BoolValue $summary.scan_cn_ne_map_candidate_cache_enabled
    $mapLookup = MetricAvg $summary 'scan_cn_ne_map_candidate_cache_lookup_count'
    $mapHits = MetricAvg $summary 'scan_cn_ne_map_candidate_cache_hit_count'
    $mapMisses = MetricAvg $summary 'scan_cn_ne_map_candidate_cache_miss_count'
    $mapHitRate = if ($null -ne $mapLookup -and $mapLookup -gt 0) {
        [math]::Round(($mapHits / $mapLookup) * 100.0, 2)
    } else {
        $null
    }

    $mapDiagEnabled = BoolValue $summary.scan_cn_ne_map_diag_enabled
    $parentLookup = MetricAvg $summary 'scan_cn_ne_map_candidate_parent_lookup_count'
    $uniqueParents = MetricAvg $summary 'scan_cn_ne_map_candidate_unique_parent_count'
    $duplicateParents = MetricAvg $summary 'scan_cn_ne_map_candidate_duplicate_parent_count'
    $duplicateParentRate = if ($null -ne $parentLookup -and $parentLookup -gt 0) {
        [math]::Round(($duplicateParents / $parentLookup) * 100.0, 2)
    } else {
        $null
    }

    $dominant = 'unknown'
    if ($null -ne $scanAvg -and $null -ne $processAvg) {
        if ($scanAvg -ge ($processAvg * 8.0)) {
            $dominant = 'scanner'
        } elseif ($processMax -ge 500.0) {
            $dominant = 'process-spikes'
        } else {
            $dominant = 'mixed'
        }
    }

    $nextAction = 'collect-connected-sample'
    $targetMapShare = if ($null -ne $scanAvg -and $scanAvg -gt 0 -and $null -ne $targetMapAvg) {
        $targetMapAvg / $scanAvg
    } else {
        $null
    }

    if ($dominant -eq 'scanner' -and $null -ne $targetMapShare -and $targetMapShare -ge 0.25 -and -not $mapDiagEnabled) {
        $nextAction = 'collect-with-UN_DMA_CN_NE_MAP_DIAG'
    } elseif ($dominant -eq 'scanner' -and $null -ne $targetMapShare -and $targetMapShare -ge 0.25 -and $mapCacheEnabled -and $mapHitRate -ne $null -and $mapHitRate -lt 10.0) {
        $nextAction = 'test-coarser-map-cache-shape'
    } elseif ($dominant -eq 'process-spikes') {
        $nextAction = 'isolate-process-spike-phase'
    }

    [pscustomobject]@{
        file = $file.Name
        label = $summary.label
        samples = $summary.sample_count
        scan_ms_avg = $scanAvg
        scan_ms_max = $scanMax
        process_ms_avg = $processAvg
        process_ms_max = $processMax
        publish_hz_avg = $publishHz
        publish_age_ms_max = $publishAgeMax
        projection_jumps = $jumps
        dominant = $dominant
        record_build_ms_avg = $recordBuildAvg
        match_link_ms_avg = $matchLinkAvg
        target_map_ms_avg = $targetMapAvg
        top_scan_phase = $scanPhase.name
        top_scan_phase_ms = $scanPhase.value
        top_process_phase = $processPhase.name
        top_process_phase_ms = $processPhase.value
        entity_hot_fields_ms_avg = MetricAvg $summary 'phase_entity_hot_fields_ms'
        entity_loop_gap_ms_avg = MetricAvg $summary 'phase_entity_loop_gap_ms'
        skeleton_ms_avg = MetricAvg $summary 'phase_skeleton_ms'
        skeleton_cache_call_ms_avg = MetricAvg $summary 'phase_skeleton_cache_call_ms'
        team_name_ms_avg = MetricAvg $summary 'phase_team_name_ms'
        team_name_hero_lookup_ms_avg = MetricAvg $summary 'phase_team_name_hero_lookup_ms'
        team_name_bot_adjust_ms_avg = MetricAvg $summary 'phase_team_name_bot_adjust_ms'
        team_name_battle_tag_ms_avg = MetricAvg $summary 'phase_team_name_battle_tag_ms'
        team_name_team_read_ms_avg = MetricAvg $summary 'phase_team_name_team_read_ms'
        map_cache_enabled = $mapCacheEnabled
        map_cache_hit_rate_percent = $mapHitRate
        map_diag_enabled = $mapDiagEnabled
        map_parent_lookup_avg = $parentLookup
        map_unique_parent_avg = $uniqueParents
        map_duplicate_parent_rate_percent = $duplicateParentRate
        next_action = $nextAction
    }
}

$selected = $rows | Sort-Object scan_ms_avg -Descending | Select-Object -First $Top

if ($Json) {
    $selected | ConvertTo-Json -Depth 6
    return
}

$selected |
    Select-Object file,samples,dominant,scan_ms_avg,process_ms_avg,process_ms_max,record_build_ms_avg,match_link_ms_avg,target_map_ms_avg,top_process_phase,top_process_phase_ms,entity_hot_fields_ms_avg,entity_loop_gap_ms_avg,skeleton_ms_avg,skeleton_cache_call_ms_avg,team_name_ms_avg,team_name_team_read_ms_avg,map_cache_enabled,map_cache_hit_rate_percent,map_diag_enabled,next_action |
    Format-Table -AutoSize
