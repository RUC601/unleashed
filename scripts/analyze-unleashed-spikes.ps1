param(
    [string]$Samples,
    [string]$Summary,
    [int]$Top = 8,
    [double]$ProcessSpikeMs = 500.0,
    [double]$AgeSpikeMs = 500.0,
    [double]$VmAgeSpikeMs = 500.0,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

function Resolve-SamplesPath {
    param(
        [string]$SamplesPath,
        [string]$SummaryPath
    )

    if (-not [string]::IsNullOrWhiteSpace($SamplesPath)) {
        if (-not (Test-Path -LiteralPath $SamplesPath)) {
            throw "Samples file not found: $SamplesPath"
        }
        return (Resolve-Path -LiteralPath $SamplesPath).Path
    }

    if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
        throw "Provide -Samples or -Summary."
    }
    if (-not (Test-Path -LiteralPath $SummaryPath)) {
        throw "Summary file not found: $SummaryPath"
    }

    $resolvedSummary = (Resolve-Path -LiteralPath $SummaryPath).Path
    $candidate = $resolvedSummary -replace '\.summary\.json$', '.samples.csv'
    if ($candidate -eq $resolvedSummary -or -not (Test-Path -LiteralPath $candidate)) {
        throw "Could not infer samples file from summary: $SummaryPath"
    }
    return (Resolve-Path -LiteralPath $candidate).Path
}

function NumberOrZero($value) {
    if ($null -eq $value -or $value -eq '') { return 0.0 }
    try { return [double]$value } catch { return 0.0 }
}

function NumberOrNull($value) {
    if ($null -eq $value -or $value -eq '') { return $null }
    try { return [double]$value } catch { return $null }
}

function TeamNameOpName($value) {
    $op = [int](NumberOrZero $value)
    switch ($op) {
        1 { return 'hero_lookup' }
        2 { return 'bot_adjust' }
        3 { return 'battle_tag' }
        4 { return 'team_read' }
        default { return 'none' }
    }
}

function SkeletonOpName($value) {
    $op = [int](NumberOrZero $value)
    switch ($op) {
        1 { return 'velocity_bone_read' }
        2 { return 'cache_skeleton_bones' }
        default { return 'none' }
    }
}

function Pick-TopPhase {
    param($sample)

    $phases = [ordered]@{
        begin_frame = NumberOrZero $sample.phase_begin_frame_ms
        consume_scan = NumberOrZero $sample.phase_consume_scan_ms
        previous_snapshot_copy = NumberOrZero $sample.phase_previous_snapshot_copy_ms
        prefetch = NumberOrZero $sample.phase_prefetch_ms
        previous_index = NumberOrZero $sample.phase_previous_index_ms
        hot_scatter_prepare = NumberOrZero $sample.phase_hot_scatter_prepare_ms
        base_decrypt = NumberOrZero $sample.phase_base_decrypt_ms
        base_cache = NumberOrZero $sample.phase_base_cache_ms
        hot_scatter = NumberOrZero $sample.phase_hot_scatter_execute_ms
        health = NumberOrZero $sample.phase_health_ms
        hero = NumberOrZero $sample.phase_hero_ms
        visibility = NumberOrZero $sample.phase_visibility_ms
        skeleton = NumberOrZero $sample.phase_skeleton_ms
        skill = NumberOrZero $sample.phase_skill_ms
        team_name = NumberOrZero $sample.phase_team_name_ms
        local_select = NumberOrZero $sample.phase_local_select_ms
        publish = NumberOrZero $sample.phase_publish_ms
        record_sync = NumberOrZero $sample.phase_record_sync_ms
        entity_loop_setup = NumberOrZero $sample.phase_entity_loop_setup_ms
        entity_header_special = NumberOrZero $sample.phase_entity_header_special_ms
        entity_cache_apply = NumberOrZero $sample.phase_entity_cache_apply_ms
        entity_hot_fields = NumberOrZero $sample.phase_entity_hot_fields_ms
        entity_rotation_position = NumberOrZero $sample.phase_entity_rotation_position_ms
        entity_loop_gap = NumberOrZero $sample.phase_entity_loop_gap_ms
    }

    $bestName = $null
    $bestValue = -1.0
    $sum = 0.0
    foreach ($entry in $phases.GetEnumerator()) {
        $sum += $entry.Value
        if ($entry.Value -gt $bestValue) {
            $bestName = $entry.Key
            $bestValue = $entry.Value
        }
    }

    $reportedGap = NumberOrNull $sample.phase_cycle_gap_ms
    $gapName = if ($null -ne $reportedGap) { 'cycle_gap' } else { 'unattributed' }
    $cycleMs = NumberOrZero $sample.entity_cycle_ms
    $unattributed = if ($null -ne $reportedGap) {
        [math]::Max(0.0, $reportedGap)
    } else {
        [math]::Max(0.0, $cycleMs - $sum)
    }
    if ($unattributed -gt $bestValue) {
        $bestName = $gapName
        $bestValue = $unattributed
    }

    return [pscustomobject]@{
        name = $bestName
        value = [math]::Round($bestValue, 3)
        known_sum_ms = [math]::Round($sum, 3)
        unattributed_ms = [math]::Round($unattributed, 3)
    }
}

function Convert-SampleRow {
    param(
        $sample,
        $previous
    )

    $topPhase = Pick-TopPhase $sample
    $dmaTotal = NumberOrZero $sample.dma_total
    $dmaFailed = NumberOrZero $sample.dma_failed
    $previousDmaTotal = if ($null -ne $previous) { NumberOrZero $previous.dma_total } else { $dmaTotal }
    $previousDmaFailed = if ($null -ne $previous) { NumberOrZero $previous.dma_failed } else { $dmaFailed }
    $dmaDelta = [math]::Max(0.0, $dmaTotal - $previousDmaTotal)
    $dmaFailDelta = [math]::Max(0.0, $dmaFailed - $previousDmaFailed)

    return [pscustomobject]@{
        captured_at = $sample.captured_at
        entity_publish_hz = NumberOrNull $sample.entity_publish_hz
        entity_publish_age_ms = NumberOrZero $sample.entity_publish_age_ms
        entity_cycle_ms = NumberOrZero $sample.entity_cycle_ms
        top_process_phase = $topPhase.name
        top_process_phase_ms = $topPhase.value
        known_process_phase_ms = $topPhase.known_sum_ms
        process_unattributed_ms = $topPhase.unattributed_ms
        render_viewmatrix_age_ms = NumberOrZero $sample.render_viewmatrix_age_ms
        viewmatrix_publish_age_ms = NumberOrZero $sample.viewmatrix_publish_age_ms
        render_viewmatrix_max_age_ms = NumberOrZero $sample.render_viewmatrix_max_age_ms
        dma_total_delta = [math]::Round($dmaDelta, 3)
        dma_failed_delta = [math]::Round($dmaFailDelta, 3)
        dma_avg_latency_us = NumberOrNull $sample.dma_avg_latency_us
        dma_max_latency_us = NumberOrNull $sample.dma_max_latency_us
        dma_window_max_latency_us = NumberOrNull $sample.dma_window_max_latency_us
        dma_window_max_callsite = $sample.dma_window_max_callsite
        dma_window_max_callsite_latency_us = NumberOrNull $sample.dma_window_max_callsite_latency_us
        dma_window_entity_scan_max_us = NumberOrNull $sample.dma_window_entity_scan_max_us
        dma_window_entity_scan_root_max_us = NumberOrNull $sample.dma_window_entity_scan_root_max_us
        dma_window_entity_scan_list_read_max_us = NumberOrNull $sample.dma_window_entity_scan_list_read_max_us
        dma_window_entity_scan_record_build_max_us = NumberOrNull $sample.dma_window_entity_scan_record_build_max_us
        dma_window_entity_scan_record_match_id_max_us = NumberOrNull $sample.dma_window_entity_scan_record_match_id_max_us
        dma_window_entity_scan_record_header_max_us = NumberOrNull $sample.dma_window_entity_scan_record_header_max_us
        dma_window_entity_scan_record_pool_ptr_max_us = NumberOrNull $sample.dma_window_entity_scan_record_pool_ptr_max_us
        dma_window_entity_scan_record_pool_id_max_us = NumberOrNull $sample.dma_window_entity_scan_record_pool_id_max_us
        dma_window_entity_scan_match_link_max_us = NumberOrNull $sample.dma_window_entity_scan_match_link_max_us
        dma_window_entity_scan_target_map_max_us = NumberOrNull $sample.dma_window_entity_scan_target_map_max_us
        dma_window_entity_scan_self_validation_max_us = NumberOrNull $sample.dma_window_entity_scan_self_validation_max_us
        dma_window_entity_scan_component_validation_max_us = NumberOrNull $sample.dma_window_entity_scan_component_validation_max_us
        dma_window_entity_decrypt_max_us = NumberOrNull $sample.dma_window_entity_decrypt_max_us
        dma_window_entity_base_decrypt_max_us = NumberOrNull $sample.dma_window_entity_base_decrypt_max_us
        dma_window_entity_header_special_max_us = NumberOrNull $sample.dma_window_entity_header_special_max_us
        dma_window_entity_hot_scatter_max_us = NumberOrNull $sample.dma_window_entity_hot_scatter_max_us
        dma_window_entity_hot_fields_max_us = NumberOrNull $sample.dma_window_entity_hot_fields_max_us
        dma_window_entity_rotation_position_max_us = NumberOrNull $sample.dma_window_entity_rotation_position_max_us
        dma_window_entity_prefetch_max_us = NumberOrNull $sample.dma_window_entity_prefetch_max_us
        dma_window_viewmatrix_max_us = NumberOrNull $sample.dma_window_viewmatrix_max_us
        scan_get_ow_entities_ms = NumberOrNull $sample.scan_get_ow_entities_ms
        scan_completed_hz = NumberOrNull $sample.scan_completed_hz
        scan_skip_stable_topology_count = NumberOrNull $sample.scan_skip_stable_topology_count
        entity_count = NumberOrNull $sample.entity_count
        phase_base_decrypt_ms = NumberOrZero $sample.phase_base_decrypt_ms
        base_decrypt_slow_call_count = NumberOrZero $sample.base_decrypt_slow_call_count
        base_decrypt_fallback_attempt_count = NumberOrZero $sample.base_decrypt_fallback_attempt_count
        base_decrypt_fallback_success_count = NumberOrZero $sample.base_decrypt_fallback_success_count
        base_decrypt_fallback_fail_count = NumberOrZero $sample.base_decrypt_fallback_fail_count
        base_decrypt_unique_key_count = NumberOrZero $sample.base_decrypt_unique_key_count
        base_decrypt_duplicate_key_count = NumberOrZero $sample.base_decrypt_duplicate_key_count
        base_decrypt_max_duplicate_key_count = NumberOrZero $sample.base_decrypt_max_duplicate_key_count
        base_decrypt_max_duplicate_key_type = NumberOrZero $sample.base_decrypt_max_duplicate_key_type
        base_decrypt_max_duplicate_key_parent = $sample.base_decrypt_max_duplicate_key_parent
        base_decrypt_max_call_ms = NumberOrZero $sample.base_decrypt_max_call_ms
        base_decrypt_max_call_type = NumberOrZero $sample.base_decrypt_max_call_type
        base_decrypt_max_call_parent = $sample.base_decrypt_max_call_parent
        base_decrypt_max_call_success = NumberOrZero $sample.base_decrypt_max_call_success
        team_name_slow_call_count = NumberOrZero $sample.team_name_slow_call_count
        team_name_max_call_ms = NumberOrZero $sample.team_name_max_call_ms
        team_name_max_call_op = NumberOrZero $sample.team_name_max_call_op
        team_name_max_call_op_name = TeamNameOpName $sample.team_name_max_call_op
        team_name_max_call_hero_id = $sample.team_name_max_call_hero_id
        team_name_max_call_link_base = $sample.team_name_max_call_link_base
        team_name_max_call_team_base = $sample.team_name_max_call_team_base
        team_name_max_call_success = NumberOrZero $sample.team_name_max_call_success
        phase_hot_scatter_execute_ms = NumberOrZero $sample.phase_hot_scatter_execute_ms
        phase_skeleton_ms = NumberOrZero $sample.phase_skeleton_ms
        phase_skeleton_velocity_read_ms = NumberOrZero $sample.phase_skeleton_velocity_read_ms
        phase_skeleton_cache_call_ms = NumberOrZero $sample.phase_skeleton_cache_call_ms
        skeleton_slow_call_count = NumberOrZero $sample.skeleton_slow_call_count
        skeleton_max_call_ms = NumberOrZero $sample.skeleton_max_call_ms
        skeleton_max_call_op = NumberOrZero $sample.skeleton_max_call_op
        skeleton_max_call_op_name = SkeletonOpName $sample.skeleton_max_call_op
        skeleton_max_call_hero_id = $sample.skeleton_max_call_hero_id
        skeleton_max_call_entity = $sample.skeleton_max_call_entity
        skeleton_max_call_bone_base = $sample.skeleton_max_call_bone_base
        skeleton_max_call_velocity_base = $sample.skeleton_max_call_velocity_base
        skeleton_max_call_velocity_bone_data = $sample.skeleton_max_call_velocity_bone_data
        skeleton_max_call_cache_hit = NumberOrZero $sample.skeleton_max_call_cache_hit
        skeleton_max_call_cache_valid = NumberOrZero $sample.skeleton_max_call_cache_valid
        skeleton_max_call_fallback = NumberOrZero $sample.skeleton_max_call_fallback
        skeleton_max_call_max_mapped_index = NumberOrZero $sample.skeleton_max_call_max_mapped_index
        skeleton_max_call_success = NumberOrZero $sample.skeleton_max_call_success
        phase_skill_ms = NumberOrZero $sample.phase_skill_ms
        phase_entity_loop_wall_ms = NumberOrZero $sample.phase_entity_loop_wall_ms
        phase_entity_loop_setup_ms = NumberOrZero $sample.phase_entity_loop_setup_ms
        phase_entity_header_special_ms = NumberOrZero $sample.phase_entity_header_special_ms
        phase_entity_header_component_ms = NumberOrZero $sample.phase_entity_header_component_ms
        phase_entity_header_link_ms = NumberOrZero $sample.phase_entity_header_link_ms
        phase_entity_special_probe_ms = NumberOrZero $sample.phase_entity_special_probe_ms
        phase_entity_cache_apply_ms = NumberOrZero $sample.phase_entity_cache_apply_ms
        phase_entity_cache_match_id_ms = NumberOrZero $sample.phase_entity_cache_match_id_ms
        phase_entity_cache_record_update_ms = NumberOrZero $sample.phase_entity_cache_record_update_ms
        phase_entity_hot_fields_ms = NumberOrZero $sample.phase_entity_hot_fields_ms
        phase_entity_rotation_position_ms = NumberOrZero $sample.phase_entity_rotation_position_ms
        phase_entity_loop_gap_ms = NumberOrZero $sample.phase_entity_loop_gap_ms
        phase_record_sync_ms = NumberOrZero $sample.phase_record_sync_ms
        phase_team_name_ms = NumberOrZero $sample.phase_team_name_ms
        phase_team_name_hero_lookup_ms = NumberOrZero $sample.phase_team_name_hero_lookup_ms
        phase_team_name_bot_adjust_ms = NumberOrZero $sample.phase_team_name_bot_adjust_ms
        phase_team_name_battle_tag_ms = NumberOrZero $sample.phase_team_name_battle_tag_ms
        phase_team_name_team_read_ms = NumberOrZero $sample.phase_team_name_team_read_ms
        phase_cycle_gap_ms = $topPhase.unattributed_ms
    }
}

function Top-Rows {
    param(
        [array]$Rows,
        [string]$Field,
        [int]$Count
    )

    return @($Rows |
        Sort-Object { NumberOrZero $_.$Field } -Descending |
        Select-Object -First $Count)
}

function Count-Where {
    param(
        [array]$Rows,
        [scriptblock]$Predicate
    )

    return @($Rows | Where-Object $Predicate).Count
}

$samplesPath = Resolve-SamplesPath $Samples $Summary
$rawRows = @(Import-Csv -LiteralPath $samplesPath)
if ($rawRows.Count -eq 0) {
    throw "No rows in samples file: $samplesPath"
}

$rows = @()
for ($i = 0; $i -lt $rawRows.Count; ++$i) {
    $previous = if ($i -gt 0) { $rawRows[$i - 1] } else { $null }
    $rows += Convert-SampleRow $rawRows[$i] $previous
}

$processRows = @($rows | Where-Object { $_.entity_cycle_ms -ge $ProcessSpikeMs })
$ageRows = @($rows | Where-Object { $_.entity_publish_age_ms -ge $AgeSpikeMs })
$vmRows = @($rows | Where-Object { $_.render_viewmatrix_age_ms -ge $VmAgeSpikeMs -or $_.viewmatrix_publish_age_ms -ge $VmAgeSpikeMs })

$phaseCounts = @{}
foreach ($row in $processRows) {
    if (-not $phaseCounts.ContainsKey($row.top_process_phase)) {
        $phaseCounts[$row.top_process_phase] = 0
    }
    $phaseCounts[$row.top_process_phase] += 1
}

$phaseSummary = @(@(
    foreach ($key in $phaseCounts.Keys) {
        [pscustomobject]@{
            phase = $key
            spike_rows = $phaseCounts[$key]
            max_phase_ms = [math]::Round((@($processRows | Where-Object { $_.top_process_phase -eq $key } | ForEach-Object { $_.top_process_phase_ms }) | Measure-Object -Maximum).Maximum, 3)
        }
    }
) | Sort-Object spike_rows,max_phase_ms -Descending)

$globalDmaLikeRows = @($ageRows | Where-Object {
    ($_.render_viewmatrix_age_ms -ge $VmAgeSpikeMs -or $_.viewmatrix_publish_age_ms -ge $VmAgeSpikeMs) -and
    $_.dma_total_delta -le 1000
})

$topEntityAge = Top-Rows $rows 'entity_publish_age_ms' $Top
$topProcess = Top-Rows $rows 'entity_cycle_ms' $Top
$topVmAge = Top-Rows $rows 'render_viewmatrix_age_ms' $Top
$topDmaLatency = Top-Rows $rows 'dma_max_latency_us' $Top
$topDmaWindowLatency = Top-Rows $rows 'dma_window_max_latency_us' $Top
$hasBaseDecryptCallDiag = Count-Where $rows { $_.base_decrypt_max_call_ms -gt 0.0 } -gt 0
$hasTeamNameCallDiag = Count-Where $rows { $_.team_name_max_call_ms -gt 0.0 } -gt 0
$hasSkeletonCallDiag = Count-Where $rows { $_.skeleton_max_call_ms -gt 0.0 } -gt 0

$nextAction = 'collect-more-connected-samples'
if ($processRows.Count -gt 0 -and $phaseSummary.Count -gt 0) {
    $dominantPhase = $phaseSummary[0].phase
    if ($dominantPhase -eq 'skill') {
        $nextAction = 'test-or-tune-skill-refresh-budget'
    } elseif ($dominantPhase -eq 'skeleton') {
        $nextAction = if ($hasSkeletonCallDiag) { 'analyze-skeleton-call-shape' } else { 'add-skeleton-spike-diagnostics' }
    } elseif ($dominantPhase -eq 'base_decrypt') {
        $nextAction = if ($hasBaseDecryptCallDiag) { 'analyze-base-decrypt-call-shape' } else { 'add-base-decrypt-spike-diagnostics' }
    } elseif ($dominantPhase -eq 'team_name') {
        $nextAction = if ($hasTeamNameCallDiag) { 'analyze-team-name-call-shape' } else { 'add-team-name-spike-diagnostics' }
    } elseif ($dominantPhase -eq 'hot_scatter') {
        $nextAction = 'check-dma-scatter-latency-and-page-shape'
    } elseif ($dominantPhase -eq 'entity_loop_gap') {
        $nextAction = 'split-entity-loop-gap-diagnostics'
    } elseif ($dominantPhase -eq 'entity_hot_fields') {
        $nextAction = 'inspect-hot-field-fallback-shape'
    } elseif ($dominantPhase -eq 'entity_header_special') {
        $nextAction = 'inspect-header-special-dma-shape'
    } elseif ($dominantPhase -eq 'entity_cache_apply') {
        $nextAction = 'inspect-cache-apply-record-bookkeeping'
    } elseif ($dominantPhase -eq 'entity_rotation_position') {
        $nextAction = 'inspect-rotation-position-fallback-reads'
    } elseif ($dominantPhase -eq 'entity_loop_setup') {
        $nextAction = 'inspect-loop-setup-dynamic-cache'
    } elseif ($dominantPhase -eq 'record_sync') {
        $nextAction = 'check-record-sync-roster-bookkeeping'
    } elseif ($dominantPhase -eq 'cycle_gap') {
        $nextAction = 'collect-cycle-gap-full-sample'
    } elseif ($dominantPhase -eq 'unattributed') {
        $nextAction = 'add-process-cycle-gap-diagnostics'
    }
}
if ($globalDmaLikeRows.Count -ge [math]::Max(2, [math]::Ceiling($ageRows.Count * 0.25))) {
    $nextAction = 'separate-global-dma-vm-stalls-from-process-spikes'
}

$result = [ordered]@{
    samples_path = $samplesPath
    sample_count = $rows.Count
    thresholds = [ordered]@{
        process_spike_ms = $ProcessSpikeMs
        entity_age_spike_ms = $AgeSpikeMs
        viewmatrix_age_spike_ms = $VmAgeSpikeMs
    }
    counts = [ordered]@{
        process_spike_rows = $processRows.Count
        entity_age_spike_rows = $ageRows.Count
        viewmatrix_age_spike_rows = $vmRows.Count
        global_dma_like_age_rows = $globalDmaLikeRows.Count
        entity_age_over_1000ms = Count-Where $rows { $_.entity_publish_age_ms -ge 1000.0 }
        process_over_1000ms = Count-Where $rows { $_.entity_cycle_ms -ge 1000.0 }
        process_unattributed_over_500ms = Count-Where $rows { $_.process_unattributed_ms -ge 500.0 }
        process_entity_loop_gap_over_500ms = Count-Where $rows { $_.phase_entity_loop_gap_ms -ge 500.0 }
        skeleton_slow_call_rows = Count-Where $rows { $_.skeleton_slow_call_count -gt 0.0 }
        team_name_slow_call_rows = Count-Where $rows { $_.team_name_slow_call_count -gt 0.0 }
        viewmatrix_age_over_1000ms = Count-Where $rows { $_.render_viewmatrix_age_ms -ge 1000.0 -or $_.viewmatrix_publish_age_ms -ge 1000.0 }
    }
    process_phase_spikes = $phaseSummary
    top_entity_age_rows = $topEntityAge
    top_process_rows = $topProcess
    top_viewmatrix_age_rows = $topVmAge
    top_dma_latency_rows = $topDmaLatency
    top_dma_window_latency_rows = $topDmaWindowLatency
    next_action = $nextAction
}

if ($Json) {
    $result | ConvertTo-Json -Depth 8
    return
}

[pscustomobject]@{
    samples = Split-Path $samplesPath -Leaf
    process_spike_rows = $result.counts.process_spike_rows
    entity_age_spike_rows = $result.counts.entity_age_spike_rows
    viewmatrix_age_spike_rows = $result.counts.viewmatrix_age_spike_rows
    global_dma_like_age_rows = $result.counts.global_dma_like_age_rows
    next_action = $nextAction
} | Format-List

if ($phaseSummary.Count -gt 0) {
    "`nProcess spike phase attribution:"
    $phaseSummary | Format-Table -AutoSize
}

"`nTop entity age rows:"
$topEntityAge |
    Select-Object captured_at,entity_publish_age_ms,entity_publish_hz,entity_cycle_ms,top_process_phase,top_process_phase_ms,process_unattributed_ms,render_viewmatrix_age_ms,viewmatrix_publish_age_ms,dma_total_delta,dma_max_latency_us,dma_window_max_callsite,dma_window_max_latency_us,entity_count |
    Format-Table -AutoSize

"`nTop process rows:"
$topProcess |
    Select-Object captured_at,entity_cycle_ms,top_process_phase,top_process_phase_ms,known_process_phase_ms,process_unattributed_ms,phase_entity_loop_wall_ms,phase_entity_loop_setup_ms,phase_entity_header_special_ms,phase_entity_header_component_ms,phase_entity_header_link_ms,phase_entity_special_probe_ms,phase_entity_cache_apply_ms,phase_entity_cache_match_id_ms,phase_entity_cache_record_update_ms,phase_entity_hot_fields_ms,phase_entity_rotation_position_ms,phase_entity_loop_gap_ms,phase_record_sync_ms,entity_publish_age_ms,render_viewmatrix_age_ms,dma_total_delta,dma_max_latency_us,dma_window_max_callsite,dma_window_max_latency_us,dma_window_entity_base_decrypt_max_us,dma_window_entity_hot_scatter_max_us,dma_window_entity_hot_fields_max_us,dma_window_entity_rotation_position_max_us,dma_window_entity_header_special_max_us,phase_base_decrypt_ms,base_decrypt_slow_call_count,base_decrypt_fallback_attempt_count,base_decrypt_fallback_success_count,base_decrypt_fallback_fail_count,base_decrypt_unique_key_count,base_decrypt_duplicate_key_count,base_decrypt_max_duplicate_key_count,base_decrypt_max_duplicate_key_type,base_decrypt_max_duplicate_key_parent,base_decrypt_max_call_ms,base_decrypt_max_call_type,base_decrypt_max_call_parent,base_decrypt_max_call_success,phase_team_name_ms,phase_team_name_hero_lookup_ms,phase_team_name_bot_adjust_ms,phase_team_name_battle_tag_ms,phase_team_name_team_read_ms,team_name_slow_call_count,team_name_max_call_ms,team_name_max_call_op_name,team_name_max_call_hero_id,team_name_max_call_link_base,team_name_max_call_team_base,team_name_max_call_success,phase_skeleton_ms,phase_skeleton_velocity_read_ms,phase_skeleton_cache_call_ms,skeleton_slow_call_count,skeleton_max_call_ms,skeleton_max_call_op_name,skeleton_max_call_hero_id,skeleton_max_call_entity,skeleton_max_call_bone_base,skeleton_max_call_velocity_base,skeleton_max_call_velocity_bone_data,skeleton_max_call_cache_hit,skeleton_max_call_cache_valid,skeleton_max_call_fallback,skeleton_max_call_max_mapped_index,skeleton_max_call_success,phase_skill_ms |
    Format-Table -AutoSize

"`nTop DMA window latency rows:"
$topDmaWindowLatency |
    Select-Object captured_at,dma_window_max_latency_us,dma_window_max_callsite,dma_window_entity_scan_max_us,dma_window_entity_scan_root_max_us,dma_window_entity_scan_list_read_max_us,dma_window_entity_scan_record_build_max_us,dma_window_entity_scan_record_match_id_max_us,dma_window_entity_scan_record_header_max_us,dma_window_entity_scan_record_pool_ptr_max_us,dma_window_entity_scan_record_pool_id_max_us,dma_window_entity_scan_match_link_max_us,dma_window_entity_scan_target_map_max_us,dma_window_entity_scan_self_validation_max_us,dma_window_entity_scan_component_validation_max_us,dma_window_entity_decrypt_max_us,dma_window_entity_base_decrypt_max_us,dma_window_entity_header_special_max_us,dma_window_entity_hot_scatter_max_us,dma_window_entity_hot_fields_max_us,dma_window_entity_rotation_position_max_us,dma_window_entity_prefetch_max_us,dma_window_viewmatrix_max_us,entity_cycle_ms,top_process_phase,top_process_phase_ms,entity_publish_age_ms,render_viewmatrix_age_ms |
    Format-Table -AutoSize
