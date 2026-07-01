param(
    [int]$Seconds = 60,
    [int]$IntervalSeconds = 2,
    [string]$BaseUrl = 'http://127.0.0.1:19550',
    [string]$DiagLog = (Join-Path $PSScriptRoot '..\build\Release\unleashed_diag.log'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\logs'),
    [string]$Label = '',
    [switch]$PreflightOnly,
    [switch]$FailOnPreflightError
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

function Get-DmaCallsiteField($dma, [string]$name, [string]$field) {
    if ($null -eq $dma -or $null -eq $dma.window_callsite) { return $null }
    $property = $dma.window_callsite.PSObject.Properties[$name]
    if ($null -eq $property -or $null -eq $property.Value) { return $null }
    return $property.Value.$field
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

function Get-EnvGateValue($name) {
    $value = [Environment]::GetEnvironmentVariable($name, 'Process')
    if ($null -eq $value) {
        $value = [Environment]::GetEnvironmentVariable($name, 'User')
    }
    if ($null -eq $value) {
        $value = [Environment]::GetEnvironmentVariable($name, 'Machine')
    }
    if ($null -eq $value) {
        return $null
    }
    return $value
}

function Invoke-EndpointProbe($url) {
    try {
        $response = Invoke-RestMethod -Uri $url -TimeoutSec 2
        return [ordered]@{
            ok = $true
            error = $null
            response = $response
        }
    }
    catch {
        return [ordered]@{
            ok = $false
            error = $_.Exception.Message
            response = $null
        }
    }
}

$diagUrl = ($BaseUrl.TrimEnd('/')) + '/api/diagnostics'
$healthUrl = ($BaseUrl.TrimEnd('/')) + '/api/health'
$envGates = [ordered]@{
    UN_DMA_ENTITY_PIPELINE_V2 = Get-EnvGateValue 'UN_DMA_ENTITY_PIPELINE_V2'
    UN_DMA_LIGHT_SCAN = Get-EnvGateValue 'UN_DMA_LIGHT_SCAN'
    UN_DMA_LIGHT_SCAN_MAX_CANDIDATES = Get-EnvGateValue 'UN_DMA_LIGHT_SCAN_MAX_CANDIDATES'
    UN_DMA_CN_NE_STAGE4B_MAP_CACHE = Get-EnvGateValue 'UN_DMA_CN_NE_STAGE4B_MAP_CACHE'
    UN_DMA_CN_NE_STAGE4B_MAP_CACHE_PERSIST_MS = Get-EnvGateValue 'UN_DMA_CN_NE_STAGE4B_MAP_CACHE_PERSIST_MS'
    UN_DMA_CN_NE_STAGE4B_MAP_CACHE_REFRESH_BUDGET = Get-EnvGateValue 'UN_DMA_CN_NE_STAGE4B_MAP_CACHE_REFRESH_BUDGET'
    UN_DMA_CN_NE_RECORD_MATCH_ID_FROM_HEADER = Get-EnvGateValue 'UN_DMA_CN_NE_RECORD_MATCH_ID_FROM_HEADER'
    UN_DMA_CN_NE_ENTITY_LIST_ROOT_CACHE_MS = Get-EnvGateValue 'UN_DMA_CN_NE_ENTITY_LIST_ROOT_CACHE_MS'
    UN_DMA_CN_NE_ENTITY_LIST_READ_NEGATIVE_CACHE_MS = Get-EnvGateValue 'UN_DMA_CN_NE_ENTITY_LIST_READ_NEGATIVE_CACHE_MS'
    UN_DMA_CN_NE_ENTITY_LIST_READ_CACHE_MS = Get-EnvGateValue 'UN_DMA_CN_NE_ENTITY_LIST_READ_CACHE_MS'
    UN_DMA_CN_NE_SCANNER_STALE_METADATA_MS = Get-EnvGateValue 'UN_DMA_CN_NE_SCANNER_STALE_METADATA_MS'
    UN_DMA_CN_NE_SCANNER_STALE_METADATA_ONLY = Get-EnvGateValue 'UN_DMA_CN_NE_SCANNER_STALE_METADATA_ONLY'
    UN_DMA_CN_NE_ENTITY_LIST_CHUNK_SIZE = Get-EnvGateValue 'UN_DMA_CN_NE_ENTITY_LIST_CHUNK_SIZE'
    UN_DMA_CN_NE_RECORD_SNAPSHOT_CACHE_MS = Get-EnvGateValue 'UN_DMA_CN_NE_RECORD_SNAPSHOT_CACHE_MS'
    UN_DMA_CN_NE_RECORD_SNAPSHOT_CACHE_REFRESH_BUDGET = Get-EnvGateValue 'UN_DMA_CN_NE_RECORD_SNAPSHOT_CACHE_REFRESH_BUDGET'
    UN_DMA_CN_NE_COMPONENT_NEGATIVE_CACHE_MS = Get-EnvGateValue 'UN_DMA_CN_NE_COMPONENT_NEGATIVE_CACHE_MS'
    UN_DMA_CN_NE_COMPONENT_NEGATIVE_CACHE_REFRESH_BUDGET = Get-EnvGateValue 'UN_DMA_CN_NE_COMPONENT_NEGATIVE_CACHE_REFRESH_BUDGET'
    UN_DMA_SCAN_DMA_RANGE_DIAG = Get-EnvGateValue 'UN_DMA_SCAN_DMA_RANGE_DIAG'
    UN_DMA_VIEWMATRIX_SLEEP_MS = Get-EnvGateValue 'UN_DMA_VIEWMATRIX_SLEEP_MS'
    UN_DMA_VIEWMATRIX_SCAN_BACKOFF_MS = Get-EnvGateValue 'UN_DMA_VIEWMATRIX_SCAN_BACKOFF_MS'
    UN_DMA_VIEWMATRIX_SCAN_DUE_GUARD_MS = Get-EnvGateValue 'UN_DMA_VIEWMATRIX_SCAN_DUE_GUARD_MS'
    UN_DMA_CN_NE_MAP_DIAG = Get-EnvGateValue 'UN_DMA_CN_NE_MAP_DIAG'
    UN_DMA_ENTITY_RECORD_STORE = Get-EnvGateValue 'UN_DMA_ENTITY_RECORD_STORE'
    UN_DMA_BASE_DECRYPT_LIFETIME_ONLY = Get-EnvGateValue 'UN_DMA_BASE_DECRYPT_LIFETIME_ONLY'
    UN_DMA_ENTITY_SOFT_REFRESH_GAP_MS = Get-EnvGateValue 'UN_DMA_ENTITY_SOFT_REFRESH_GAP_MS'
    UN_DMA_ENTITY_HARD_RESCAN_GAP_MS = Get-EnvGateValue 'UN_DMA_ENTITY_HARD_RESCAN_GAP_MS'
    UN_DMA_ENTITY_SCAN_MISS_GRACE_COUNT = Get-EnvGateValue 'UN_DMA_ENTITY_SCAN_MISS_GRACE_COUNT'
    UN_DMA_SKILL_REFRESH_BUDGET = Get-EnvGateValue 'UN_DMA_SKILL_REFRESH_BUDGET'
    UN_DMA_TEAM_NAME_REFRESH_BUDGET = Get-EnvGateValue 'UN_DMA_TEAM_NAME_REFRESH_BUDGET'
    UN_DMA_SKELETON_REFRESH_BUDGET = Get-EnvGateValue 'UN_DMA_SKELETON_REFRESH_BUDGET'
}

$healthProbe = Invoke-EndpointProbe $healthUrl
$diagnosticsProbe = Invoke-EndpointProbe $diagUrl
$preflight = [ordered]@{
    base_url = $BaseUrl
    health_ok = [bool]$healthProbe.ok
    diagnostics_ok = [bool]$diagnosticsProbe.ok
    health_error = $healthProbe.error
    diagnostics_error = $diagnosticsProbe.error
    process_connected = if ($healthProbe.ok -and $healthProbe.response.process) { [bool]$healthProbe.response.process.connected } else { $null }
    process_status = if ($healthProbe.ok -and $healthProbe.response.process) { $healthProbe.response.process.status } else { $null }
    entity_count = if ($diagnosticsProbe.ok -and $diagnosticsProbe.response.diagnostics) { As-Int64OrZero $diagnosticsProbe.response.diagnostics.entity_count } else { $null }
    entity_scan_hz = if ($diagnosticsProbe.ok -and $diagnosticsProbe.response.diagnostics) { As-DoubleOrNull $diagnosticsProbe.response.diagnostics.entity_scan_hz } else { $null }
    entity_publish_hz = if ($diagnosticsProbe.ok -and $diagnosticsProbe.response.diagnostics) { As-DoubleOrNull $diagnosticsProbe.response.diagnostics.entity_publish_hz } else { $null }
    env_gates = $envGates
    runtime_gates = if ($diagnosticsProbe.ok -and $diagnosticsProbe.response.diagnostics) {
        $rd = $diagnosticsProbe.response.diagnostics
        $rPipeline = $rd.entity_pipeline
        $rScanDetail = $rd.entity_scan_detail
        $rCounters = $rScanDetail.scanner_counters
        [ordered]@{
            viewmatrix_poll_sleep_ms = As-Int64OrZero $rd.viewmatrix_poll_sleep_ms
            viewmatrix_scan_backoff_ms = As-Int64OrZero $rd.viewmatrix_scan_backoff_ms
            viewmatrix_scan_due_guard_ms = As-Int64OrZero $rd.viewmatrix_scan_due_guard_ms
            scan_cold_topology_enabled = if ($rPipeline -and $rPipeline.scan) { [bool]$rPipeline.scan.scan_cold_topology_enabled } else { $null }
            light_scan_enabled = if ($rScanDetail) { [bool]$rScanDetail.light_scan_enabled } else { $null }
            cn_ne_map_candidate_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_map_candidate_cache_enabled } else { $null }
            cn_ne_map_candidate_persistent_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_map_candidate_persistent_cache_enabled } else { $null }
            cn_ne_map_candidate_persistent_cache_ttl_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_map_candidate_persistent_cache_ttl_ms } else { $null }
            cn_ne_map_candidate_persistent_cache_refresh_budget = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_map_candidate_persistent_cache_refresh_budget } else { $null }
            cn_ne_map_diag_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_map_diag_enabled } else { $null }
            cn_ne_record_match_id_from_header_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_record_match_id_from_header_enabled } else { $null }
            cn_ne_entity_list_root_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_entity_list_root_cache_enabled } else { $null }
            cn_ne_entity_list_root_cache_ttl_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_entity_list_root_cache_ttl_ms } else { $null }
            cn_ne_entity_list_read_negative_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_entity_list_read_negative_cache_enabled } else { $null }
            cn_ne_entity_list_read_negative_cache_ttl_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_entity_list_read_negative_cache_ttl_ms } else { $null }
            cn_ne_entity_list_read_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_entity_list_read_cache_enabled } else { $null }
            cn_ne_entity_list_read_cache_ttl_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_entity_list_read_cache_ttl_ms } else { $null }
            cn_ne_scanner_stale_metadata_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_scanner_stale_metadata_ms } else { $null }
            cn_ne_scanner_stale_metadata_only_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_scanner_stale_metadata_only_enabled } else { $null }
            cn_ne_entity_list_chunk_size = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_entity_list_chunk_size } else { $null }
            cn_ne_record_snapshot_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_record_snapshot_cache_enabled } else { $null }
            cn_ne_record_snapshot_cache_ttl_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_record_snapshot_cache_ttl_ms } else { $null }
            cn_ne_record_snapshot_cache_refresh_budget = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_record_snapshot_cache_refresh_budget } else { $null }
            cn_ne_link_decrypt_negative_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_link_decrypt_negative_cache_enabled } else { $null }
            cn_ne_link_decrypt_negative_cache_ttl_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_link_decrypt_negative_cache_ttl_ms } else { $null }
            cn_ne_component_negative_cache_enabled = if ($rCounters) { [bool]$rCounters.cn_ne_component_negative_cache_enabled } else { $null }
            cn_ne_component_negative_cache_ttl_ms = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_component_negative_cache_ttl_ms } else { $null }
            cn_ne_component_negative_cache_refresh_budget = if ($rCounters) { As-Int64OrZero $rCounters.cn_ne_component_negative_cache_refresh_budget } else { $null }
            record_store_enabled = if ($rPipeline -and $rPipeline.lifecycle) { [bool]$rPipeline.lifecycle.record_store_enabled } else { $null }
            entity_soft_refresh_gap_ms_env = $envGates.UN_DMA_ENTITY_SOFT_REFRESH_GAP_MS
            entity_hard_rescan_gap_ms_env = $envGates.UN_DMA_ENTITY_HARD_RESCAN_GAP_MS
            entity_scan_miss_grace_count_env = $envGates.UN_DMA_ENTITY_SCAN_MISS_GRACE_COUNT
        }
    } else {
        $null
    }
}

if ($PreflightOnly) {
    $preflight | ConvertTo-Json -Depth 8
    if ($FailOnPreflightError -and -not $preflight.diagnostics_ok) {
        throw "Diagnostics endpoint unavailable at $diagUrl."
    }
    return
}

$startLineCount = Count-LogLines $DiagLog
$samples = New-Object System.Collections.Generic.List[object]
$startedAt = Get-Date
$deadline = $startedAt.AddSeconds($Seconds)

Write-Host "Sampling $diagUrl for $Seconds seconds every $IntervalSeconds seconds..."
Write-Host ("Preflight: health_ok={0} diagnostics_ok={1} process_connected={2} status={3}" -f `
    $preflight.health_ok,
    $preflight.diagnostics_ok,
    $preflight.process_connected,
    $preflight.process_status)
if ($preflight.runtime_gates) {
    Write-Host ("Runtime gates: map_cache={0} persist_cache={1}/{2}ms refresh_budget={3} match_id_header={4} root_cache={5}/{6}ms list_neg={7}/{8}ms list_read_cache={9}/{10}ms stale_meta={11}ms stale_only={12} chunk={13} record_snapshot={14}/{15}ms/b{16} link_neg={17}/{18}ms comp_neg={19}/{20}ms/b{21} vm_sleep={22}ms vm_scan_backoff={23}ms vm_due_guard={24}ms map_diag={25} light_scan={26} record_store={27} entity_gap_env={28}/{29}ms miss_grace_env={30}" -f `
        $preflight.runtime_gates.cn_ne_map_candidate_cache_enabled,
        $preflight.runtime_gates.cn_ne_map_candidate_persistent_cache_enabled,
        $preflight.runtime_gates.cn_ne_map_candidate_persistent_cache_ttl_ms,
        $preflight.runtime_gates.cn_ne_map_candidate_persistent_cache_refresh_budget,
        $preflight.runtime_gates.cn_ne_record_match_id_from_header_enabled,
        $preflight.runtime_gates.cn_ne_entity_list_root_cache_enabled,
        $preflight.runtime_gates.cn_ne_entity_list_root_cache_ttl_ms,
        $preflight.runtime_gates.cn_ne_entity_list_read_negative_cache_enabled,
        $preflight.runtime_gates.cn_ne_entity_list_read_negative_cache_ttl_ms,
        $preflight.runtime_gates.cn_ne_entity_list_read_cache_enabled,
        $preflight.runtime_gates.cn_ne_entity_list_read_cache_ttl_ms,
        $preflight.runtime_gates.cn_ne_scanner_stale_metadata_ms,
        $preflight.runtime_gates.cn_ne_scanner_stale_metadata_only_enabled,
        $preflight.runtime_gates.cn_ne_entity_list_chunk_size,
        $preflight.runtime_gates.cn_ne_record_snapshot_cache_enabled,
        $preflight.runtime_gates.cn_ne_record_snapshot_cache_ttl_ms,
        $preflight.runtime_gates.cn_ne_record_snapshot_cache_refresh_budget,
        $preflight.runtime_gates.cn_ne_link_decrypt_negative_cache_enabled,
        $preflight.runtime_gates.cn_ne_link_decrypt_negative_cache_ttl_ms,
        $preflight.runtime_gates.cn_ne_component_negative_cache_enabled,
        $preflight.runtime_gates.cn_ne_component_negative_cache_ttl_ms,
        $preflight.runtime_gates.cn_ne_component_negative_cache_refresh_budget,
        $preflight.runtime_gates.viewmatrix_poll_sleep_ms,
        $preflight.runtime_gates.viewmatrix_scan_backoff_ms,
        $preflight.runtime_gates.viewmatrix_scan_due_guard_ms,
        $preflight.runtime_gates.cn_ne_map_diag_enabled,
        $preflight.runtime_gates.light_scan_enabled,
        $preflight.runtime_gates.record_store_enabled,
        $preflight.runtime_gates.entity_soft_refresh_gap_ms_env,
        $preflight.runtime_gates.entity_hard_rescan_gap_ms_env,
        $preflight.runtime_gates.entity_scan_miss_grace_count_env)
}

while ((Get-Date) -lt $deadline) {
    try {
        $response = Invoke-RestMethod -Uri $diagUrl -TimeoutSec 2
        $d = $response.diagnostics
        $r = $d.render
        $pi = $d.player_info
        $screen = $d.screen
        $projected = $pi.sample_projected
        $renderPrediction = $pi.render_prediction
        $dma = $d.dma_reads
        $dmaSlowSamples = @()
        if ($dma -and $dma.slow_samples) {
            $dmaSlowSamples = @($dma.slow_samples)
        }
        $dmaSlowestSample = $null
        foreach ($slowSample in $dmaSlowSamples) {
            if ($null -eq $dmaSlowestSample -or
                (As-Int64OrZero $slowSample.latency_us) -gt
                (As-Int64OrZero $dmaSlowestSample.latency_us)) {
                $dmaSlowestSample = $slowSample
            }
        }
        $pipeline = $d.entity_pipeline
        $scanDetail = $d.entity_scan_detail
        $scanSubphases = $scanDetail.scanner_subphases
        $scanCounters = $scanDetail.scanner_counters
        $pipeScan = $pipeline.scan
        $pipeProcess = $pipeline.process
        $pipePhase = $pipeProcess.phases
        $pipeHot = $pipeline.hot
        $pipeCold = $pipeline.cold
        $pipeFallback = $pipeline.fallback
        $pipeLifecycle = $pipeline.lifecycle
        $sdk = $d.sdk

        $sample = [pscustomobject]@{
            captured_at = (Get-Date)
            fps = As-DoubleOrNull $d.fps
            render_fps = As-DoubleOrNull $d.render_fps
            entity_count = As-Int64OrZero $d.entity_count
            entity_process_hz = As-DoubleOrNull $d.entity_process_hz
            entity_scan_hz = As-DoubleOrNull $d.entity_scan_hz
            entity_publish_hz = As-DoubleOrNull $d.entity_publish_hz
            entity_publish_age_ms = As-Int64OrZero $d.entity_publish_age_ms
            entity_publish_count = As-Int64OrZero $d.entity_publish_count
            scan_loop_hz = As-DoubleOrNull $pipeScan.scan_loop_hz
            scan_due_hz = As-DoubleOrNull $pipeScan.scan_due_hz
            scan_started_hz = As-DoubleOrNull $pipeScan.scan_started_hz
            scan_completed_hz = As-DoubleOrNull $pipeScan.scan_completed_hz
            scan_failed_count = As-Int64OrZero $pipeScan.scan_failed_count
            scan_publish_attempt_count = As-Int64OrZero $pipeScan.scan_publish_attempt_count
            scan_publish_success_count = As-Int64OrZero $pipeScan.scan_publish_success_count
            scan_skip_pending_count = As-Int64OrZero $pipeScan.scan_skip_pending_count
            scan_skip_not_due_count = As-Int64OrZero $pipeScan.scan_skip_not_due_count
            scan_skip_stable_topology_count = As-Int64OrZero $pipeScan.scan_skip_stable_topology_count
            scan_overwritten_count = As-Int64OrZero $pipeScan.scan_overwritten_count
            scan_get_ow_entities_ms = As-DoubleOrNull $pipeScan.scan_get_ow_entities_ms
            scan_max_get_ow_entities_ms = As-DoubleOrNull $pipeScan.scan_max_get_ow_entities_ms
            scan_max_get_ow_entities_generation = As-Int64OrZero $pipeScan.scan_max_get_ow_entities_generation
            scan_max_get_ow_entities_records = As-Int64OrZero $pipeScan.scan_max_get_ow_entities_records
            scan_max_get_ow_entities_pairs = As-Int64OrZero $pipeScan.scan_max_get_ow_entities_pairs
            scan_max_record_build_ms = As-DoubleOrNull $pipeScan.scan_max_record_build_ms
            scan_max_match_link_ms = As-DoubleOrNull $pipeScan.scan_max_match_link_ms
            scan_max_cn_ne_target_map_ms = As-DoubleOrNull $pipeScan.scan_max_cn_ne_target_map_ms
            scan_max_component_only_validation_ms = As-DoubleOrNull $pipeScan.scan_max_component_only_validation_ms
            scan_max_dma_reads_delta = As-Int64OrZero $pipeScan.scan_max_dma_reads_delta
            scan_max_dma_fail_delta = As-Int64OrZero $pipeScan.scan_max_dma_fail_delta
            scan_max_dma_range_diag_enabled = if ($pipeScan.scan_max_dma_range_diag_enabled) { 1 } else { 0 }
            scan_max_dma_range_reads = As-Int64OrZero $pipeScan.scan_max_dma_range_reads
            scan_max_dma_range_failed = As-Int64OrZero $pipeScan.scan_max_dma_range_failed
            scan_max_dma_range_max_latency_us = As-Int64OrZero $pipeScan.scan_max_dma_range_max_latency_us
            scan_max_dma_range_max_callsite = if ($pipeScan.scan_max_dma_range_max_callsite) { [string]$pipeScan.scan_max_dma_range_max_callsite } else { '' }
            scan_max_dma_range_scanner_reads = As-Int64OrZero $pipeScan.scan_max_dma_range_scanner_reads
            scan_max_dma_range_scanner_max_latency_us = As-Int64OrZero $pipeScan.scan_max_dma_range_scanner_max_latency_us
            scan_max_dma_range_scanner_max_callsite = if ($pipeScan.scan_max_dma_range_scanner_max_callsite) { [string]$pipeScan.scan_max_dma_range_scanner_max_callsite } else { '' }
            scan_max_dma_range_foreign_reads = As-Int64OrZero $pipeScan.scan_max_dma_range_foreign_reads
            scan_max_dma_range_foreign_max_latency_us = As-Int64OrZero $pipeScan.scan_max_dma_range_foreign_max_latency_us
            scan_max_dma_range_foreign_max_callsite = if ($pipeScan.scan_max_dma_range_foreign_max_callsite) { [string]$pipeScan.scan_max_dma_range_foreign_max_callsite } else { '' }
            scan_max_dma_range_root_max_us = As-Int64OrZero $pipeScan.scan_max_dma_range_root_max_us
            scan_max_dma_range_list_read_max_us = As-Int64OrZero $pipeScan.scan_max_dma_range_list_read_max_us
            scan_max_dma_range_record_header_max_us = As-Int64OrZero $pipeScan.scan_max_dma_range_record_header_max_us
            scan_max_dma_range_record_pool_id_max_us = As-Int64OrZero $pipeScan.scan_max_dma_range_record_pool_id_max_us
            scan_max_dma_range_target_map_max_us = As-Int64OrZero $pipeScan.scan_max_dma_range_target_map_max_us
            scan_max_dma_range_component_validation_max_us = As-Int64OrZero $pipeScan.scan_max_dma_range_component_validation_max_us
            scan_max_dma_range_viewmatrix_max_us = As-Int64OrZero $pipeScan.scan_max_dma_range_viewmatrix_max_us
            scan_max_cn_ne_entity_list_root_cache_hit_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_root_cache_hit_count
            scan_max_cn_ne_entity_list_root_cache_read_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_root_cache_read_count
            scan_max_cn_ne_entity_list_root_cache_store_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_root_cache_store_count
            scan_max_cn_ne_entity_list_root_cache_expired_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_root_cache_expired_count
            scan_max_cn_ne_entity_list_root_cache_stale_hit_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_root_cache_stale_hit_count
            scan_max_list_read_skipped_count = As-Int64OrZero $pipeScan.scan_max_list_read_skipped_count
            scan_max_cn_ne_entity_list_read_negative_cache_hit_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_negative_cache_hit_count
            scan_max_cn_ne_entity_list_read_negative_cache_store_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_negative_cache_store_count
            scan_max_cn_ne_entity_list_read_negative_cache_expired_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_negative_cache_expired_count
            scan_max_cn_ne_entity_list_read_negative_cache_stale_hit_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_negative_cache_stale_hit_count
            scan_max_cn_ne_entity_list_read_cache_hit_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_cache_hit_count
            scan_max_cn_ne_entity_list_read_cache_store_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_cache_store_count
            scan_max_cn_ne_entity_list_read_cache_expired_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_cache_expired_count
            scan_max_cn_ne_entity_list_read_cache_stale_hit_count = As-Int64OrZero $pipeScan.scan_max_cn_ne_entity_list_read_cache_stale_hit_count
            scan_max_record_match_id_direct_read_count = As-Int64OrZero $pipeScan.scan_max_record_match_id_direct_read_count
            scan_max_record_match_id_direct_zero_count = As-Int64OrZero $pipeScan.scan_max_record_match_id_direct_zero_count
            scan_max_record_match_id_header_hit_count = As-Int64OrZero $pipeScan.scan_max_record_match_id_header_hit_count
            scan_max_record_match_id_header_miss_count = As-Int64OrZero $pipeScan.scan_max_record_match_id_header_miss_count
            scan_max_record_match_id_header_match_count = As-Int64OrZero $pipeScan.scan_max_record_match_id_header_match_count
            scan_max_record_match_id_header_mismatch_count = As-Int64OrZero $pipeScan.scan_max_record_match_id_header_mismatch_count
            scan_max_record_match_id_header_use_count = As-Int64OrZero $pipeScan.scan_max_record_match_id_header_use_count
            scan_max_persistent_refresh_count = As-Int64OrZero $pipeScan.scan_max_persistent_refresh_count
            scan_max_persistent_stale_hit_count = As-Int64OrZero $pipeScan.scan_max_persistent_stale_hit_count
            scan_result_raw_count = As-Int64OrZero $pipeScan.scan_result_raw_count
            scan_pending_age_ms = As-Int64OrZero $pipeScan.scan_pending_age_ms
            scan_generation = As-Int64OrZero $pipeScan.scan_generation
            scan_cold_topology_enabled = if ($pipeScan.scan_cold_topology_enabled) { 1 } else { 0 }
            scan_topology_rescan_request_count = As-Int64OrZero $pipeScan.scan_topology_rescan_request_count
            scan_topology_count_probe_count = As-Int64OrZero $pipeScan.scan_topology_count_probe_count
            scan_topology_count_probe_change_count = As-Int64OrZero $pipeScan.scan_topology_count_probe_change_count
            scan_topology_candidate_count = As-Int64OrZero $pipeScan.scan_topology_candidate_count
            scan_root_ms = As-DoubleOrNull $scanSubphases.scan_root_ms
            scan_list_read_ms = As-DoubleOrNull $scanSubphases.scan_list_read_ms
            scan_slot_walk_ms = As-DoubleOrNull $scanSubphases.scan_slot_walk_ms
            scan_record_build_ms = As-DoubleOrNull $scanSubphases.scan_record_build_ms
            scan_match_link_ms = As-DoubleOrNull $scanSubphases.scan_match_link_ms
            scan_cn_ne_target_map_ms = As-DoubleOrNull $scanSubphases.scan_cn_ne_target_map_ms
            scan_cn_ne_self_validation_ms = As-DoubleOrNull $scanSubphases.scan_cn_ne_self_validation_ms
            scan_component_only_validation_ms = As-DoubleOrNull $scanSubphases.scan_component_only_validation_ms
            scan_dynamic_pair_ms = As-DoubleOrNull $scanSubphases.scan_dynamic_pair_ms
            scan_finalize_ms = As-DoubleOrNull $scanSubphases.scan_finalize_ms
            scan_dma_reads_delta = As-Int64OrZero $scanSubphases.scan_dma_reads_delta
            scan_dma_fail_delta = As-Int64OrZero $scanSubphases.scan_dma_fail_delta
            scan_dma_range_diag_enabled = if ($scanSubphases.scan_dma_range_diag_enabled) { 1 } else { 0 }
            scan_dma_range_reads = As-Int64OrZero $scanSubphases.scan_dma_range_reads
            scan_dma_range_failed = As-Int64OrZero $scanSubphases.scan_dma_range_failed
            scan_dma_range_max_latency_us = As-Int64OrZero $scanSubphases.scan_dma_range_max_latency_us
            scan_dma_range_max_callsite = if ($scanSubphases.scan_dma_range_max_callsite) { [string]$scanSubphases.scan_dma_range_max_callsite } else { '' }
            scan_dma_range_scanner_reads = As-Int64OrZero $scanSubphases.scan_dma_range_scanner_reads
            scan_dma_range_scanner_max_latency_us = As-Int64OrZero $scanSubphases.scan_dma_range_scanner_max_latency_us
            scan_dma_range_scanner_max_callsite = if ($scanSubphases.scan_dma_range_scanner_max_callsite) { [string]$scanSubphases.scan_dma_range_scanner_max_callsite } else { '' }
            scan_dma_range_foreign_reads = As-Int64OrZero $scanSubphases.scan_dma_range_foreign_reads
            scan_dma_range_foreign_max_latency_us = As-Int64OrZero $scanSubphases.scan_dma_range_foreign_max_latency_us
            scan_dma_range_foreign_max_callsite = if ($scanSubphases.scan_dma_range_foreign_max_callsite) { [string]$scanSubphases.scan_dma_range_foreign_max_callsite } else { '' }
            scan_dma_range_root_max_us = As-Int64OrZero $scanSubphases.scan_dma_range_root_max_us
            scan_dma_range_list_read_max_us = As-Int64OrZero $scanSubphases.scan_dma_range_list_read_max_us
            scan_dma_range_record_header_max_us = As-Int64OrZero $scanSubphases.scan_dma_range_record_header_max_us
            scan_dma_range_record_pool_id_max_us = As-Int64OrZero $scanSubphases.scan_dma_range_record_pool_id_max_us
            scan_dma_range_target_map_max_us = As-Int64OrZero $scanSubphases.scan_dma_range_target_map_max_us
            scan_dma_range_component_validation_max_us = As-Int64OrZero $scanSubphases.scan_dma_range_component_validation_max_us
            scan_dma_range_viewmatrix_max_us = As-Int64OrZero $scanSubphases.scan_dma_range_viewmatrix_max_us
            scan_list_read_count = As-Int64OrZero $scanCounters.list_read_count
            scan_list_read_fail_count = As-Int64OrZero $scanCounters.list_read_fail_count
            scan_list_fallback_read_count = As-Int64OrZero $scanCounters.list_fallback_read_count
            scan_list_read_skipped_count = As-Int64OrZero $scanCounters.list_read_skipped_count
            scan_cn_ne_entity_list_chunk_size = As-Int64OrZero $scanCounters.cn_ne_entity_list_chunk_size
            scan_record_add_attempt_count = As-Int64OrZero $scanCounters.record_add_attempt_count
            scan_record_duplicate_count = As-Int64OrZero $scanCounters.record_duplicate_count
            scan_record_header_read_count = As-Int64OrZero $scanCounters.record_header_read_count
            scan_record_header_fail_count = As-Int64OrZero $scanCounters.record_header_fail_count
            scan_record_remote_fallback_read_count = As-Int64OrZero $scanCounters.record_remote_fallback_read_count
            scan_record_match_id_direct_read_count = As-Int64OrZero $scanCounters.record_match_id_direct_read_count
            scan_record_match_id_direct_zero_count = As-Int64OrZero $scanCounters.record_match_id_direct_zero_count
            scan_record_match_id_header_hit_count = As-Int64OrZero $scanCounters.record_match_id_header_hit_count
            scan_record_match_id_header_miss_count = As-Int64OrZero $scanCounters.record_match_id_header_miss_count
            scan_record_match_id_header_match_count = As-Int64OrZero $scanCounters.record_match_id_header_match_count
            scan_record_match_id_header_mismatch_count = As-Int64OrZero $scanCounters.record_match_id_header_mismatch_count
            scan_record_match_id_header_use_count = As-Int64OrZero $scanCounters.record_match_id_header_use_count
            scan_cn_ne_record_match_id_from_header_enabled = if ($scanCounters.cn_ne_record_match_id_from_header_enabled) { 1 } else { 0 }
            scan_cn_ne_entity_list_root_cache_enabled = if ($scanCounters.cn_ne_entity_list_root_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_entity_list_root_cache_ttl_ms = As-Int64OrZero $scanCounters.cn_ne_entity_list_root_cache_ttl_ms
            scan_cn_ne_entity_list_root_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_root_cache_hit_count
            scan_cn_ne_entity_list_root_cache_read_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_root_cache_read_count
            scan_cn_ne_entity_list_root_cache_store_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_root_cache_store_count
            scan_cn_ne_entity_list_root_cache_expired_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_root_cache_expired_count
            scan_cn_ne_entity_list_root_cache_stale_hit_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_root_cache_stale_hit_count
            scan_cn_ne_entity_list_read_negative_cache_enabled = if ($scanCounters.cn_ne_entity_list_read_negative_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_entity_list_read_negative_cache_ttl_ms = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_negative_cache_ttl_ms
            scan_cn_ne_entity_list_read_negative_cache_lookup_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_negative_cache_lookup_count
            scan_cn_ne_entity_list_read_negative_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_negative_cache_hit_count
            scan_cn_ne_entity_list_read_negative_cache_store_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_negative_cache_store_count
            scan_cn_ne_entity_list_read_negative_cache_expired_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_negative_cache_expired_count
            scan_cn_ne_entity_list_read_negative_cache_stale_hit_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_negative_cache_stale_hit_count
            scan_cn_ne_entity_list_read_cache_enabled = if ($scanCounters.cn_ne_entity_list_read_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_entity_list_read_cache_ttl_ms = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_cache_ttl_ms
            scan_cn_ne_entity_list_read_cache_lookup_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_cache_lookup_count
            scan_cn_ne_entity_list_read_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_cache_hit_count
            scan_cn_ne_entity_list_read_cache_store_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_cache_store_count
            scan_cn_ne_entity_list_read_cache_expired_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_cache_expired_count
            scan_cn_ne_entity_list_read_cache_stale_hit_count = As-Int64OrZero $scanCounters.cn_ne_entity_list_read_cache_stale_hit_count
            scan_cn_ne_scanner_stale_metadata_ms = As-Int64OrZero $scanCounters.cn_ne_scanner_stale_metadata_ms
            scan_cn_ne_scanner_stale_metadata_only_enabled = if ($scanCounters.cn_ne_scanner_stale_metadata_only_enabled) { 1 } else { 0 }
            scan_cn_ne_record_snapshot_cache_enabled = if ($scanCounters.cn_ne_record_snapshot_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_record_snapshot_cache_ttl_ms = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_ttl_ms
            scan_cn_ne_record_snapshot_cache_lookup_count = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_lookup_count
            scan_cn_ne_record_snapshot_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_hit_count
            scan_cn_ne_record_snapshot_cache_store_count = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_store_count
            scan_cn_ne_record_snapshot_cache_expired_count = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_expired_count
            scan_cn_ne_record_snapshot_cache_refresh_budget = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_refresh_budget
            scan_cn_ne_record_snapshot_cache_refresh_count = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_refresh_count
            scan_cn_ne_record_snapshot_cache_stale_hit_count = As-Int64OrZero $scanCounters.cn_ne_record_snapshot_cache_stale_hit_count
            scan_pool_id_read_count = As-Int64OrZero $scanCounters.pool_id_read_count
            scan_match_lookup_count = As-Int64OrZero $scanCounters.match_lookup_count
            scan_match_lookup_hit_count = As-Int64OrZero $scanCounters.match_lookup_hit_count
            scan_add_pair_attempt_count = As-Int64OrZero $scanCounters.add_pair_attempt_count
            scan_add_pair_duplicate_count = As-Int64OrZero $scanCounters.add_pair_duplicate_count
            scan_link_decrypt_attempt_count = As-Int64OrZero $scanCounters.link_decrypt_attempt_count
            scan_link_decrypt_success_count = As-Int64OrZero $scanCounters.link_decrypt_success_count
            scan_cn_ne_link_decrypt_negative_cache_enabled = if ($scanCounters.cn_ne_link_decrypt_negative_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_link_decrypt_negative_cache_ttl_ms = As-Int64OrZero $scanCounters.cn_ne_link_decrypt_negative_cache_ttl_ms
            scan_cn_ne_link_decrypt_negative_cache_lookup_count = As-Int64OrZero $scanCounters.cn_ne_link_decrypt_negative_cache_lookup_count
            scan_cn_ne_link_decrypt_negative_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_link_decrypt_negative_cache_hit_count
            scan_cn_ne_link_decrypt_negative_cache_store_count = As-Int64OrZero $scanCounters.cn_ne_link_decrypt_negative_cache_store_count
            scan_cn_ne_link_decrypt_negative_cache_expired_count = As-Int64OrZero $scanCounters.cn_ne_link_decrypt_negative_cache_expired_count
            scan_cn_ne_link_decrypt_negative_cache_stale_hit_count = As-Int64OrZero $scanCounters.cn_ne_link_decrypt_negative_cache_stale_hit_count
            scan_playable_validation_attempt_count = As-Int64OrZero $scanCounters.playable_validation_attempt_count
            scan_playable_validation_success_count = As-Int64OrZero $scanCounters.playable_validation_success_count
            scan_cn_ne_map_candidate_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_count
            scan_cn_ne_target_map_attempt_count = As-Int64OrZero $scanCounters.cn_ne_target_map_attempt_count
            scan_cn_ne_target_map_success_count = As-Int64OrZero $scanCounters.cn_ne_target_map_success_count
            scan_cn_ne_bucket_entry_scan_count = As-Int64OrZero $scanCounters.cn_ne_bucket_entry_scan_count
            scan_cn_ne_map_candidate_cache_enabled = if ($scanCounters.cn_ne_map_candidate_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_map_candidate_cache_lookup_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_cache_lookup_count
            scan_cn_ne_map_candidate_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_cache_hit_count
            scan_cn_ne_map_candidate_cache_miss_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_cache_miss_count
            scan_cn_ne_map_candidate_persistent_cache_enabled = if ($scanCounters.cn_ne_map_candidate_persistent_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_map_candidate_persistent_cache_ttl_ms = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_ttl_ms
            scan_cn_ne_map_candidate_persistent_cache_lookup_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_lookup_count
            scan_cn_ne_map_candidate_persistent_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_hit_count
            scan_cn_ne_map_candidate_persistent_cache_miss_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_miss_count
            scan_cn_ne_map_candidate_persistent_cache_store_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_store_count
            scan_cn_ne_map_candidate_persistent_cache_expired_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_expired_count
            scan_cn_ne_map_candidate_persistent_cache_refresh_budget = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_refresh_budget
            scan_cn_ne_map_candidate_persistent_cache_refresh_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_refresh_count
            scan_cn_ne_map_candidate_persistent_cache_stale_hit_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_persistent_cache_stale_hit_count
            scan_cn_ne_component_negative_cache_enabled = if ($scanCounters.cn_ne_component_negative_cache_enabled) { 1 } else { 0 }
            scan_cn_ne_component_negative_cache_ttl_ms = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_ttl_ms
            scan_cn_ne_component_negative_cache_lookup_count = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_lookup_count
            scan_cn_ne_component_negative_cache_hit_count = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_hit_count
            scan_cn_ne_component_negative_cache_store_count = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_store_count
            scan_cn_ne_component_negative_cache_expired_count = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_expired_count
            scan_cn_ne_component_negative_cache_refresh_budget = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_refresh_budget
            scan_cn_ne_component_negative_cache_refresh_count = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_refresh_count
            scan_cn_ne_component_negative_cache_stale_hit_count = As-Int64OrZero $scanCounters.cn_ne_component_negative_cache_stale_hit_count
            scan_cn_ne_map_diag_enabled = if ($scanCounters.cn_ne_map_diag_enabled) { 1 } else { 0 }
            scan_cn_ne_map_candidate_parent_lookup_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_parent_lookup_count
            scan_cn_ne_map_candidate_unique_parent_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_unique_parent_count
            scan_cn_ne_map_candidate_duplicate_parent_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_duplicate_parent_count
            scan_cn_ne_map_candidate_direct_source_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_direct_source_count
            scan_cn_ne_map_candidate_plus8_source_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_plus8_source_count
            scan_cn_ne_map_candidate_wrapper_source_count = As-Int64OrZero $scanCounters.cn_ne_map_candidate_wrapper_source_count
            scan_component_only_validation_attempt_count = As-Int64OrZero $scanCounters.component_only_validation_attempt_count
            scan_component_only_validation_success_count = As-Int64OrZero $scanCounters.component_only_validation_success_count
            scan_detail_records = As-Int64OrZero $scanDetail.records
            scan_detail_total_pairs = As-Int64OrZero $scanDetail.total_pairs
            scan_light_scan_enabled = if ($scanDetail.light_scan_enabled) { 1 } else { 0 }
            scan_light_unvalidated_pairs = As-Int64OrZero $scanDetail.light_scan_unvalidated_pairs
            scan_light_cap_hits = As-Int64OrZero $scanDetail.light_scan_cap_hits
            entity_cycle_ms = As-DoubleOrNull $pipeProcess.entity_cycle_ms
            entity_cycle_dma_reads_delta = As-Int64OrZero $pipeProcess.entity_cycle_dma_reads_delta
            entity_cycle_dma_fail_delta = As-Int64OrZero $pipeProcess.entity_cycle_dma_fail_delta
            phase_begin_frame_ms = As-DoubleOrNull $pipePhase.phase_begin_frame_ms
            phase_consume_scan_ms = As-DoubleOrNull $pipePhase.phase_consume_scan_ms
            phase_previous_snapshot_copy_ms = As-DoubleOrNull $pipePhase.phase_previous_snapshot_copy_ms
            phase_prefetch_ms = As-DoubleOrNull $pipePhase.phase_prefetch_ms
            phase_previous_index_ms = As-DoubleOrNull $pipePhase.phase_previous_index_ms
            phase_hot_scatter_prepare_ms = As-DoubleOrNull $pipePhase.phase_hot_scatter_prepare_ms
            phase_hot_scatter_execute_ms = As-DoubleOrNull $pipePhase.phase_hot_scatter_execute_ms
            phase_base_cache_ms = As-DoubleOrNull $pipePhase.phase_base_cache_ms
            phase_base_decrypt_ms = As-DoubleOrNull $pipePhase.phase_base_decrypt_ms
            phase_health_ms = As-DoubleOrNull $pipePhase.phase_health_ms
            phase_hero_ms = As-DoubleOrNull $pipePhase.phase_hero_ms
            phase_visibility_ms = As-DoubleOrNull $pipePhase.phase_visibility_ms
            phase_skeleton_ms = As-DoubleOrNull $pipePhase.phase_skeleton_ms
            phase_skeleton_velocity_read_ms = As-DoubleOrNull $pipePhase.phase_skeleton_velocity_read_ms
            phase_skeleton_cache_call_ms = As-DoubleOrNull $pipePhase.phase_skeleton_cache_call_ms
            phase_skill_ms = As-DoubleOrNull $pipePhase.phase_skill_ms
            phase_team_name_ms = As-DoubleOrNull $pipePhase.phase_team_name_ms
            phase_team_name_hero_lookup_ms = As-DoubleOrNull $pipePhase.phase_team_name_hero_lookup_ms
            phase_team_name_bot_adjust_ms = As-DoubleOrNull $pipePhase.phase_team_name_bot_adjust_ms
            phase_team_name_battle_tag_ms = As-DoubleOrNull $pipePhase.phase_team_name_battle_tag_ms
            phase_team_name_team_read_ms = As-DoubleOrNull $pipePhase.phase_team_name_team_read_ms
            phase_local_select_ms = As-DoubleOrNull $pipePhase.phase_local_select_ms
            phase_publish_ms = As-DoubleOrNull $pipePhase.phase_publish_ms
            phase_record_sync_ms = As-DoubleOrNull $pipePhase.phase_record_sync_ms
            phase_entity_loop_wall_ms = As-DoubleOrNull $pipePhase.phase_entity_loop_wall_ms
            phase_entity_loop_setup_ms = As-DoubleOrNull $pipePhase.phase_entity_loop_setup_ms
            phase_entity_header_special_ms = As-DoubleOrNull $pipePhase.phase_entity_header_special_ms
            phase_entity_header_component_ms = As-DoubleOrNull $pipePhase.phase_entity_header_component_ms
            phase_entity_header_link_ms = As-DoubleOrNull $pipePhase.phase_entity_header_link_ms
            phase_entity_special_probe_ms = As-DoubleOrNull $pipePhase.phase_entity_special_probe_ms
            phase_entity_cache_apply_ms = As-DoubleOrNull $pipePhase.phase_entity_cache_apply_ms
            phase_entity_cache_match_id_ms = As-DoubleOrNull $pipePhase.phase_entity_cache_match_id_ms
            phase_entity_cache_record_update_ms = As-DoubleOrNull $pipePhase.phase_entity_cache_record_update_ms
            phase_entity_hot_fields_ms = As-DoubleOrNull $pipePhase.phase_entity_hot_fields_ms
            phase_entity_rotation_position_ms = As-DoubleOrNull $pipePhase.phase_entity_rotation_position_ms
            phase_entity_loop_gap_ms = As-DoubleOrNull $pipePhase.phase_entity_loop_gap_ms
            phase_cycle_gap_ms = As-DoubleOrNull $pipePhase.phase_cycle_gap_ms
            hot_scatter_requested_count = As-Int64OrZero $pipeHot.hot_scatter_requested_count
            hot_scatter_prepare_requested_count = As-Int64OrZero $pipeHot.hot_scatter_prepare_requested_count
            hot_scatter_prepare_success_count = As-Int64OrZero $pipeHot.hot_scatter_prepare_success_count
            hot_scatter_prepare_fail_count = As-Int64OrZero $pipeHot.hot_scatter_prepare_fail_count
            hot_scatter_execute_count = As-Int64OrZero $pipeHot.hot_scatter_execute_count
            hot_scatter_execute_fail_count = As-Int64OrZero $pipeHot.hot_scatter_execute_fail_count
            hot_scatter_bytes_requested = As-Int64OrZero $pipeHot.hot_scatter_bytes_requested
            hot_scatter_bytes_read = As-Int64OrZero $pipeHot.hot_scatter_bytes_read
            hot_scatter_short_read_count = As-Int64OrZero $pipeHot.hot_scatter_short_read_count
            hot_scatter_batch_items = As-Int64OrZero $pipeHot.hot_scatter_batch_items
            hot_scatter_batch_requests = As-Int64OrZero $pipeHot.hot_scatter_batch_requests
            hot_scatter_estimated_unique_pages = As-Int64OrZero $pipeHot.hot_scatter_estimated_unique_pages
            hot_scatter_success_count = As-Int64OrZero $pipeHot.hot_scatter_success_count
            hot_scatter_partial_count = As-Int64OrZero $pipeHot.hot_scatter_partial_count
            hot_scatter_fallback_read_count = As-Int64OrZero $pipeFallback.hot_scatter_fallback_read_count
            visibility_scatter_hit_count = As-Int64OrZero $pipeHot.visibility_scatter_hit_count
            base_cache_hit_count = As-Int64OrZero $pipeCold.base_cache_hit_count
            base_cache_miss_count = As-Int64OrZero $pipeCold.base_cache_miss_count
            base_decrypt_attempt_count = As-Int64OrZero $pipeCold.base_decrypt_attempt_count
            base_decrypt_success_count = As-Int64OrZero $pipeCold.base_decrypt_success_count
            base_decrypt_fail_count = As-Int64OrZero $pipeCold.base_decrypt_fail_count
            base_decrypt_slow_call_count = As-Int64OrZero $pipeCold.base_decrypt_slow_call_count
            base_decrypt_fallback_attempt_count = As-Int64OrZero $pipeCold.base_decrypt_fallback_attempt_count
            base_decrypt_fallback_success_count = As-Int64OrZero $pipeCold.base_decrypt_fallback_success_count
            base_decrypt_fallback_fail_count = As-Int64OrZero $pipeCold.base_decrypt_fallback_fail_count
            base_decrypt_unique_key_count = As-Int64OrZero $pipeCold.base_decrypt_unique_key_count
            base_decrypt_duplicate_key_count = As-Int64OrZero $pipeCold.base_decrypt_duplicate_key_count
            base_decrypt_max_duplicate_key_count = As-Int64OrZero $pipeCold.base_decrypt_max_duplicate_key_count
            base_decrypt_max_duplicate_key_type = As-Int64OrZero $pipeCold.base_decrypt_max_duplicate_key_type
            base_decrypt_max_duplicate_key_parent = if ($pipeCold.base_decrypt_max_duplicate_key_parent) { [string]$pipeCold.base_decrypt_max_duplicate_key_parent } else { '' }
            base_decrypt_max_call_ms = As-DoubleOrNull $pipeCold.base_decrypt_max_call_ms
            base_decrypt_max_call_type = As-Int64OrZero $pipeCold.base_decrypt_max_call_type
            base_decrypt_max_call_parent = if ($pipeCold.base_decrypt_max_call_parent) { [string]$pipeCold.base_decrypt_max_call_parent } else { '' }
            base_decrypt_max_call_success = if ($pipeCold.base_decrypt_max_call_success) { 1 } else { 0 }
            team_name_slow_call_count = As-Int64OrZero $pipeCold.team_name_slow_call_count
            team_name_max_call_ms = As-DoubleOrNull $pipeCold.team_name_max_call_ms
            team_name_max_call_op = As-Int64OrZero $pipeCold.team_name_max_call_op
            team_name_max_call_hero_id = if ($pipeCold.team_name_max_call_hero_id) { [string]$pipeCold.team_name_max_call_hero_id } else { '' }
            team_name_max_call_link_base = if ($pipeCold.team_name_max_call_link_base) { [string]$pipeCold.team_name_max_call_link_base } else { '' }
            team_name_max_call_team_base = if ($pipeCold.team_name_max_call_team_base) { [string]$pipeCold.team_name_max_call_team_base } else { '' }
            team_name_max_call_success = if ($pipeCold.team_name_max_call_success) { 1 } else { 0 }
            skeleton_cache_hit_count = As-Int64OrZero $pipeHot.skeleton_cache_hit_count
            skeleton_cache_miss_count = As-Int64OrZero $pipeHot.skeleton_cache_miss_count
            skeleton_block_read_bytes = As-Int64OrZero $pipeHot.skeleton_block_read_bytes
            skeleton_slow_call_count = As-Int64OrZero $pipeHot.skeleton_slow_call_count
            skeleton_max_call_ms = As-DoubleOrNull $pipeHot.skeleton_max_call_ms
            skeleton_max_call_op = As-Int64OrZero $pipeHot.skeleton_max_call_op
            skeleton_max_call_hero_id = if ($pipeHot.skeleton_max_call_hero_id) { [string]$pipeHot.skeleton_max_call_hero_id } else { '' }
            skeleton_max_call_entity = if ($pipeHot.skeleton_max_call_entity) { [string]$pipeHot.skeleton_max_call_entity } else { '' }
            skeleton_max_call_bone_base = if ($pipeHot.skeleton_max_call_bone_base) { [string]$pipeHot.skeleton_max_call_bone_base } else { '' }
            skeleton_max_call_velocity_base = if ($pipeHot.skeleton_max_call_velocity_base) { [string]$pipeHot.skeleton_max_call_velocity_base } else { '' }
            skeleton_max_call_velocity_bone_data = if ($pipeHot.skeleton_max_call_velocity_bone_data) { [string]$pipeHot.skeleton_max_call_velocity_bone_data } else { '' }
            skeleton_max_call_cache_hit = if ($pipeHot.skeleton_max_call_cache_hit) { 1 } else { 0 }
            skeleton_max_call_cache_valid = if ($pipeHot.skeleton_max_call_cache_valid) { 1 } else { 0 }
            skeleton_max_call_fallback = if ($pipeHot.skeleton_max_call_fallback) { 1 } else { 0 }
            skeleton_max_call_max_mapped_index = As-Int64OrZero $pipeHot.skeleton_max_call_max_mapped_index
            skeleton_max_call_success = if ($pipeHot.skeleton_max_call_success) { 1 } else { 0 }
            skeleton_fallback_get_bone_pos_count = As-Int64OrZero $pipeFallback.skeleton_fallback_get_bone_pos_count
            visibility_fallback_count = As-Int64OrZero $pipeFallback.visibility_fallback_count
            skill_due_count = As-Int64OrZero $pipeCold.skill_due_count
            skill_read_count = As-Int64OrZero $pipeCold.skill_read_count
            skill_skipped_not_due_count = As-Int64OrZero $pipeCold.skill_skipped_not_due_count
            entity_record_created_count = As-Int64OrZero $pipeLifecycle.entity_record_created_count
            entity_record_updated_actor_count = As-Int64OrZero $pipeLifecycle.entity_record_updated_actor_count
            entity_record_link_changed_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_count
            entity_record_link_changed_same_component_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_same_component_count
            entity_record_link_changed_component_changed_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_component_changed_count
            entity_record_link_changed_same_hero_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_same_hero_count
            entity_record_link_changed_hero_changed_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_hero_changed_count
            entity_record_link_changed_hero_unknown_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_hero_unknown_count
            entity_record_link_changed_match_key_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_match_key_count
            entity_record_link_changed_link_key_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_link_key_count
            entity_record_link_changed_component_key_count = As-Int64OrZero $pipeLifecycle.entity_record_link_changed_component_key_count
            entity_record_mark_missing_count = As-Int64OrZero $pipeLifecycle.entity_record_mark_missing_count
            entity_record_mark_dead_count = As-Int64OrZero $pipeLifecycle.entity_record_mark_dead_count
            entity_record_expired_count = As-Int64OrZero $pipeLifecycle.entity_record_expired_count
            entity_record_scan_miss_soft_gap_count = As-Int64OrZero $pipeLifecycle.entity_record_scan_miss_soft_gap_count
            entity_record_scan_miss_hard_gap_count = As-Int64OrZero $pipeLifecycle.entity_record_scan_miss_hard_gap_count
            entity_record_scan_miss_grace_append_count = As-Int64OrZero $pipeLifecycle.entity_record_scan_miss_grace_append_count
            entity_record_scan_miss_grace_drop_count = As-Int64OrZero $pipeLifecycle.entity_record_scan_miss_grace_drop_count
            entity_record_scan_miss_hot_read_success_count = As-Int64OrZero $pipeLifecycle.entity_record_scan_miss_hot_read_success_count
            entity_record_scan_miss_hot_read_fail_count = As-Int64OrZero $pipeLifecycle.entity_record_scan_miss_hot_read_fail_count
            component_cache_hit_count = As-Int64OrZero $pipeLifecycle.component_cache_hit_count
            component_cache_miss_count = As-Int64OrZero $pipeLifecycle.component_cache_miss_count
            component_cache_invalidate_interval_count = As-Int64OrZero $pipeLifecycle.component_cache_invalidate_interval_count
            component_cache_invalidate_interval_skipped_lifetime_count = As-Int64OrZero $pipeLifecycle.component_cache_invalidate_interval_skipped_lifetime_count
            component_cache_invalidate_link_change_count = As-Int64OrZero $pipeLifecycle.component_cache_invalidate_link_change_count
            component_cache_invalidate_health_resurrect_count = As-Int64OrZero $pipeLifecycle.component_cache_invalidate_health_resurrect_count
            component_cache_invalidate_hero_change_count = As-Int64OrZero $pipeLifecycle.component_cache_invalidate_hero_change_count
            component_cache_link_change_previous_match_id_known_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_previous_match_id_known_count
            component_cache_link_change_previous_match_id_zero_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_previous_match_id_zero_count
            component_cache_link_change_previous_match_id_unknown_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_previous_match_id_unknown_count
            component_cache_link_change_record_alias_hit_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_record_alias_hit_count
            component_cache_link_change_record_alias_miss_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_record_alias_miss_count
            component_cache_link_change_record_published_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_record_published_count
            component_cache_link_change_record_bases_valid_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_record_bases_valid_count
            component_cache_link_change_record_match_key_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_record_match_key_count
            component_cache_link_change_record_link_key_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_record_link_key_count
            component_cache_link_change_record_component_key_count = As-Int64OrZero $pipeLifecycle.component_cache_link_change_record_component_key_count
            component_cache_link_retain_attempt_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_attempt_count
            component_cache_link_retain_success_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_success_count
            component_cache_link_retain_rejected_disabled_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_disabled_count
            component_cache_link_retain_rejected_record_store_disabled_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_record_store_disabled_count
            component_cache_link_retain_rejected_missing_record_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_missing_record_count
            component_cache_link_retain_rejected_missing_match_id_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_missing_match_id_count
            component_cache_link_retain_rejected_component_changed_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_component_changed_count
            component_cache_link_retain_rejected_interval_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_interval_count
            component_cache_link_retain_interval_bypassed_lifetime_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_interval_bypassed_lifetime_count
            component_cache_link_retain_rejected_hero_changed_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_hero_changed_count
            component_cache_link_retain_rejected_hero_unknown_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_hero_unknown_count
            component_cache_link_retain_rejected_decrypt_fail_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_rejected_decrypt_fail_count
            component_cache_link_retain_cached_hero_validate_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_cached_hero_validate_count
            component_cache_link_retain_refresh_decrypt_attempt_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_decrypt_attempt_count
            component_cache_link_retain_refresh_decrypt_success_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_decrypt_success_count
            component_cache_link_retain_refresh_decrypt_fail_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_decrypt_fail_count
            component_cache_link_retain_refresh_link_attempt_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_link_attempt_count
            component_cache_link_retain_refresh_link_success_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_link_success_count
            component_cache_link_retain_refresh_link_fail_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_link_fail_count
            component_cache_link_retain_refresh_hero_attempt_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_hero_attempt_count
            component_cache_link_retain_refresh_hero_success_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_hero_success_count
            component_cache_link_retain_refresh_hero_fail_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_hero_fail_count
            component_cache_link_retain_refresh_visibility_attempt_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_visibility_attempt_count
            component_cache_link_retain_refresh_visibility_success_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_visibility_success_count
            component_cache_link_retain_refresh_visibility_fail_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_visibility_fail_count
            component_cache_link_retain_refresh_angle_attempt_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_angle_attempt_count
            component_cache_link_retain_refresh_angle_success_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_angle_success_count
            component_cache_link_retain_refresh_angle_fail_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_angle_fail_count
            component_cache_link_retain_refresh_angle_skipped_no_prior_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_angle_skipped_no_prior_count
            component_cache_link_retain_refresh_angle_prior_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_angle_prior_count
            component_cache_link_retain_refresh_angle_prior_fail_rejected_count = As-Int64OrZero $pipeLifecycle.component_cache_link_retain_refresh_angle_prior_fail_rejected_count
            dynamic_cache_created_count = As-Int64OrZero $pipeLifecycle.dynamic_cache_created_count
            dynamic_cache_reused_count = As-Int64OrZero $pipeLifecycle.dynamic_cache_reused_count
            dynamic_cache_replaced_count = As-Int64OrZero $pipeLifecycle.dynamic_cache_replaced_count
            dynamic_cache_expired_count = As-Int64OrZero $pipeLifecycle.dynamic_cache_expired_count
            record_store_enabled = if ($pipeLifecycle.record_store_enabled) { 1 } else { 0 }
            record_store_size = As-Int64OrZero $pipeLifecycle.record_store_size
            record_store_fresh_count = As-Int64OrZero $pipeLifecycle.record_store_fresh_count
            record_store_missing_count = As-Int64OrZero $pipeLifecycle.record_store_missing_count
            record_store_dead_count = As-Int64OrZero $pipeLifecycle.record_store_dead_count
            record_store_expired_count = As-Int64OrZero $pipeLifecycle.record_store_expired_count
            record_store_bases_valid_count = As-Int64OrZero $pipeLifecycle.record_store_bases_valid_count
            record_store_dynamic_valid_count = As-Int64OrZero $pipeLifecycle.record_store_dynamic_valid_count
            record_store_published_valid_count = As-Int64OrZero $pipeLifecycle.record_store_published_valid_count
            sdk_component_key_cache_hit_count = As-Int64OrZero $sdk.component_key_cache_hit_count
            sdk_component_key_cache_miss_count = As-Int64OrZero $sdk.component_key_cache_miss_count
            sdk_begin_frame_scan_count = As-Int64OrZero $sdk.begin_frame_scan_count
            sdk_begin_frame_process_count = As-Int64OrZero $sdk.begin_frame_process_count
            sdk_begin_frame_unknown_count = As-Int64OrZero $sdk.begin_frame_unknown_count
            viewmatrix_publish_hz = As-DoubleOrNull $d.viewmatrix_publish_hz
            viewmatrix_poll_sleep_ms = As-Int64OrZero $d.viewmatrix_poll_sleep_ms
            viewmatrix_scan_backoff_ms = As-Int64OrZero $d.viewmatrix_scan_backoff_ms
            viewmatrix_scan_due_guard_ms = As-Int64OrZero $d.viewmatrix_scan_due_guard_ms
            viewmatrix_scan_backoff_count = As-Int64OrZero $d.viewmatrix_scan_backoff_count
            viewmatrix_scan_due_guard_count = As-Int64OrZero $d.viewmatrix_scan_due_guard_count
            viewmatrix_publish_age_ms = As-Int64OrZero $d.viewmatrix_publish_age_ms
            render_viewmatrix_age_ms = As-Int64OrZero $d.render_viewmatrix_age_ms
            render_viewmatrix_max_age_ms = As-Int64OrZero $d.render_viewmatrix_max_age_ms
            render_viewmatrix_uses = As-Int64OrZero $d.render_viewmatrix_uses
            render_viewmatrix_missing_publish_uses = As-Int64OrZero $d.render_viewmatrix_missing_publish_uses
            render_viewmatrix_over_16ms = As-Int64OrZero $d.render_viewmatrix_over_16ms
            render_viewmatrix_over_33ms = As-Int64OrZero $d.render_viewmatrix_over_33ms
            render_viewmatrix_over_50ms = As-Int64OrZero $d.render_viewmatrix_over_50ms
            view_matrix_ok = [bool]$d.view_matrix_ok
            view_matrix_resolved = [bool]$d.view_matrix_resolved
            view_matrix_valid = [bool]$d.view_matrix_valid
            snapshot_entities_copy_ms = As-DoubleOrNull $d.snapshot_entities_copy_ms
            snapshot_entities_copy_max_ms = As-DoubleOrNull $d.snapshot_entities_copy_max_ms
            snapshot_dynamic_copy_ms = As-DoubleOrNull $d.snapshot_dynamic_copy_ms
            snapshot_dynamic_copy_max_ms = As-DoubleOrNull $d.snapshot_dynamic_copy_max_ms
            manual_width = As-Int64OrZero $screen.manual_width
            manual_height = As-Int64OrZero $screen.manual_height
            detected_width = As-Int64OrZero $screen.detected_width
            detected_height = As-Int64OrZero $screen.detected_height
            resolved_wx = As-DoubleOrNull $screen.resolved_wx
            resolved_wy = As-DoubleOrNull $screen.resolved_wy
            player_info_input = As-Int64OrZero $pi.input
            player_info_projected = As-Int64OrZero $pi.projected
            player_info_drawn = As-Int64OrZero $pi.drawn
            render_prediction_candidates = As-Int64OrZero $renderPrediction.candidates
            render_prediction_applied = As-Int64OrZero $renderPrediction.applied
            render_prediction_world_delta_fallback = As-Int64OrZero $renderPrediction.world_delta_fallback
            render_prediction_max_lead_ms = As-Int64OrZero $renderPrediction.max_lead_ms
            render_prediction_max_offset_cm = As-Int64OrZero $renderPrediction.max_offset_cm
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
            dma_window_ms = As-Int64OrZero $dma.window_ms
            dma_window_total = As-Int64OrZero $dma.window_total
            dma_window_failed = As-Int64OrZero $dma.window_failed
            dma_window_max_latency_us = As-DoubleOrNull $dma.window_max_latency_us
            dma_window_slow_threshold_us = As-Int64OrZero $dma.window_slow_threshold_us
            dma_window_slow_reads = As-Int64OrZero $dma.window_slow_reads
            dma_window_slow_failed = As-Int64OrZero $dma.window_slow_failed
            dma_slow_samples_count = As-Int64OrZero $dmaSlowSamples.Count
            dma_slowest_sample_callsite = if ($dmaSlowestSample) { [string]$dmaSlowestSample.callsite } else { '' }
            dma_slowest_sample_success = if ($dmaSlowestSample -and $dmaSlowestSample.success) { 1 } else { 0 }
            dma_slowest_sample_latency_us = if ($dmaSlowestSample) { As-Int64OrZero $dmaSlowestSample.latency_us } else { 0 }
            dma_slowest_sample_thread_id = if ($dmaSlowestSample) { As-Int64OrZero $dmaSlowestSample.thread_id } else { 0 }
            dma_slowest_sample_started_tick_ms = if ($dmaSlowestSample) { As-Int64OrZero $dmaSlowestSample.started_tick_ms } else { 0 }
            dma_slowest_sample_completed_tick_ms = if ($dmaSlowestSample) { As-Int64OrZero $dmaSlowestSample.completed_tick_ms } else { 0 }
            dma_slowest_sample_completed_age_ms = if ($dmaSlowestSample) { As-Int64OrZero $dmaSlowestSample.completed_age_ms } else { 0 }
            dma_slowest_sample_started_age_ms = if ($dmaSlowestSample) { As-Int64OrZero $dmaSlowestSample.started_age_ms } else { 0 }
            dma_window_max_callsite = $dma.window_max_callsite
            dma_window_max_callsite_latency_us = As-DoubleOrNull $dma.window_max_callsite_latency_us
            dma_window_entity_scan_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScan' 'reads')
            dma_window_entity_scan_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScan' 'max_latency_us')
            dma_window_entity_scan_root_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanRoot' 'reads')
            dma_window_entity_scan_root_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanRoot' 'max_latency_us')
            dma_window_entity_scan_list_read_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanListRead' 'reads')
            dma_window_entity_scan_list_read_failed = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanListRead' 'failed')
            dma_window_entity_scan_list_read_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanListRead' 'max_latency_us')
            dma_window_entity_scan_list_read_success_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanListRead' 'success_max_latency_us')
            dma_window_entity_scan_list_read_failed_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanListRead' 'failed_max_latency_us')
            dma_window_entity_scan_list_read_slow_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanListRead' 'slow_reads')
            dma_window_entity_scan_list_read_slow_failed = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanListRead' 'slow_failed')
            dma_window_entity_scan_record_build_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanRecordBuild' 'reads')
            dma_window_entity_scan_record_build_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanRecordBuild' 'max_latency_us')
            dma_window_entity_scan_record_match_id_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanRecordMatchId' 'reads')
            dma_window_entity_scan_record_match_id_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanRecordMatchId' 'max_latency_us')
            dma_window_entity_scan_record_header_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanRecordHeader' 'reads')
            dma_window_entity_scan_record_header_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanRecordHeader' 'max_latency_us')
            dma_window_entity_scan_record_pool_ptr_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanRecordPoolPtr' 'reads')
            dma_window_entity_scan_record_pool_ptr_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanRecordPoolPtr' 'max_latency_us')
            dma_window_entity_scan_record_pool_id_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanRecordPoolId' 'reads')
            dma_window_entity_scan_record_pool_id_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanRecordPoolId' 'max_latency_us')
            dma_window_entity_scan_match_link_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanMatchLink' 'reads')
            dma_window_entity_scan_match_link_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanMatchLink' 'max_latency_us')
            dma_window_entity_scan_target_map_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanTargetMap' 'reads')
            dma_window_entity_scan_target_map_failed = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanTargetMap' 'failed')
            dma_window_entity_scan_target_map_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanTargetMap' 'max_latency_us')
            dma_window_entity_scan_target_map_success_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanTargetMap' 'success_max_latency_us')
            dma_window_entity_scan_target_map_failed_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanTargetMap' 'failed_max_latency_us')
            dma_window_entity_scan_target_map_slow_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanTargetMap' 'slow_reads')
            dma_window_entity_scan_target_map_slow_failed = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanTargetMap' 'slow_failed')
            dma_window_entity_scan_map_candidate_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanMapCandidate' 'reads')
            dma_window_entity_scan_map_candidate_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanMapCandidate' 'max_latency_us')
            dma_window_entity_scan_map_candidate_slow_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanMapCandidate' 'slow_reads')
            dma_window_entity_scan_link_target_resolve_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanLinkTargetResolve' 'reads')
            dma_window_entity_scan_link_target_resolve_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanLinkTargetResolve' 'max_latency_us')
            dma_window_entity_scan_link_target_resolve_slow_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanLinkTargetResolve' 'slow_reads')
            dma_window_entity_scan_self_validation_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanSelfValidation' 'reads')
            dma_window_entity_scan_self_validation_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanSelfValidation' 'max_latency_us')
            dma_window_entity_scan_component_validation_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityScanComponentValidation' 'reads')
            dma_window_entity_scan_component_validation_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityScanComponentValidation' 'max_latency_us')
            dma_window_entity_decrypt_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityDecrypt' 'reads')
            dma_window_entity_decrypt_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityDecrypt' 'max_latency_us')
            dma_window_entity_base_decrypt_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityBaseDecrypt' 'reads')
            dma_window_entity_base_decrypt_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityBaseDecrypt' 'max_latency_us')
            dma_window_entity_header_special_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityHeaderSpecial' 'reads')
            dma_window_entity_header_special_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityHeaderSpecial' 'max_latency_us')
            dma_window_entity_hot_scatter_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityHotScatter' 'reads')
            dma_window_entity_hot_scatter_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityHotScatter' 'max_latency_us')
            dma_window_entity_hot_fields_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityHotFields' 'reads')
            dma_window_entity_hot_fields_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityHotFields' 'max_latency_us')
            dma_window_entity_rotation_position_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityRotationPosition' 'reads')
            dma_window_entity_rotation_position_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityRotationPosition' 'max_latency_us')
            dma_window_entity_prefetch_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'EntityPrefetch' 'reads')
            dma_window_entity_prefetch_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'EntityPrefetch' 'max_latency_us')
            dma_window_viewmatrix_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'ViewMatrix' 'reads')
            dma_window_viewmatrix_failed = As-Int64OrZero (Get-DmaCallsiteField $dma 'ViewMatrix' 'failed')
            dma_window_viewmatrix_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'ViewMatrix' 'max_latency_us')
            dma_window_viewmatrix_success_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'ViewMatrix' 'success_max_latency_us')
            dma_window_viewmatrix_failed_max_us = As-DoubleOrNull (Get-DmaCallsiteField $dma 'ViewMatrix' 'failed_max_latency_us')
            dma_window_viewmatrix_slow_reads = As-Int64OrZero (Get-DmaCallsiteField $dma 'ViewMatrix' 'slow_reads')
            dma_window_viewmatrix_slow_failed = As-Int64OrZero (Get-DmaCallsiteField $dma 'ViewMatrix' 'slow_failed')
            render_frame_ms = As-DoubleOrNull $r.frame_ms
            render_callback_ms = As-DoubleOrNull $r.render_callback_ms
            present_ms = As-DoubleOrNull $r.present_ms
            render_mode = $r.mode
            player_info_called = [bool]$r.player_info_called
            skill_info_called = [bool]$r.skill_info_called
            entity_list_empty = [bool]$r.entity_list_empty
        }
        $samples.Add($sample) | Out-Null

        Write-Host ("{0} fps={1:n1} vm_pub={2:n1}Hz/{3}ms vm_use={4}ms ent_pub={5:n1}Hz/{6}ms entities={7} pred={8}/{9} vm={10} screen={11:n0}x{12:n0} sample=({13},{14}) render={15:n2}ms copy={16:n3}ms rt_dmatotal={17}" -f `
            (Get-Date -Format 'HH:mm:ss'),
            $sample.render_fps,
            $sample.viewmatrix_publish_hz,
            $sample.viewmatrix_publish_age_ms,
            $sample.render_viewmatrix_age_ms,
            $sample.entity_publish_hz,
            $sample.entity_publish_age_ms,
            $sample.entity_count,
            $sample.render_prediction_applied,
            $sample.render_prediction_world_delta_fallback,
            $sample.view_matrix_ok,
            $sample.resolved_wx,
            $sample.resolved_wy,
            $sample.sample_projected_left,
            $sample.sample_projected_top,
            $sample.render_callback_ms,
            $sample.snapshot_entities_copy_ms,
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
$summarySlowestSample = $null
foreach ($sample in $samples) {
    if ((As-Int64OrZero $sample.dma_slowest_sample_latency_us) -le 0) {
        continue
    }
    if ($null -eq $summarySlowestSample -or
        (As-Int64OrZero $sample.dma_slowest_sample_latency_us) -gt
        (As-Int64OrZero $summarySlowestSample.dma_slowest_sample_latency_us)) {
        $summarySlowestSample = $sample
    }
}
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
    label = $Label
    preflight = $preflight
    started_at = $startedAt.ToString('o')
    ended_at = (Get-Date).ToString('o')
    sample_count = $samples.Count
    duration_seconds = [math]::Round($durationSeconds, 3)
    fps = Measure-SampleField $samples 'fps'
    render_fps = Measure-SampleField $samples 'render_fps'
    entity_process_hz = Measure-SampleField $samples 'entity_process_hz'
    entity_scan_hz = Measure-SampleField $samples 'entity_scan_hz'
    entity_publish_hz = Measure-SampleField $samples 'entity_publish_hz'
    entity_publish_age_ms = Measure-SampleField $samples 'entity_publish_age_ms'
    scan_loop_hz = Measure-SampleField $samples 'scan_loop_hz'
    scan_due_hz = Measure-SampleField $samples 'scan_due_hz'
    scan_started_hz = Measure-SampleField $samples 'scan_started_hz'
    scan_completed_hz = Measure-SampleField $samples 'scan_completed_hz'
    scan_failed_delta = [math]::Max(0, $last.scan_failed_count - $first.scan_failed_count)
    scan_publish_attempt_delta = [math]::Max(0, $last.scan_publish_attempt_count - $first.scan_publish_attempt_count)
    scan_publish_success_delta = [math]::Max(0, $last.scan_publish_success_count - $first.scan_publish_success_count)
    scan_skip_stable_topology_count = Measure-SampleField $samples 'scan_skip_stable_topology_count'
    scan_skip_stable_topology_delta = [math]::Max(0, $last.scan_skip_stable_topology_count - $first.scan_skip_stable_topology_count)
    scan_get_ow_entities_ms = Measure-SampleField $samples 'scan_get_ow_entities_ms'
    scan_max_get_ow_entities_ms = Measure-SampleField $samples 'scan_max_get_ow_entities_ms'
    scan_max_get_ow_entities_generation = Measure-SampleField $samples 'scan_max_get_ow_entities_generation'
    scan_max_get_ow_entities_records = Measure-SampleField $samples 'scan_max_get_ow_entities_records'
    scan_max_get_ow_entities_pairs = Measure-SampleField $samples 'scan_max_get_ow_entities_pairs'
    scan_max_record_build_ms = Measure-SampleField $samples 'scan_max_record_build_ms'
    scan_max_match_link_ms = Measure-SampleField $samples 'scan_max_match_link_ms'
    scan_max_cn_ne_target_map_ms = Measure-SampleField $samples 'scan_max_cn_ne_target_map_ms'
    scan_max_component_only_validation_ms = Measure-SampleField $samples 'scan_max_component_only_validation_ms'
    scan_max_dma_reads_delta = Measure-SampleField $samples 'scan_max_dma_reads_delta'
    scan_max_dma_fail_delta = Measure-SampleField $samples 'scan_max_dma_fail_delta'
    scan_max_dma_range_diag_enabled = [bool]($last.scan_max_dma_range_diag_enabled)
    scan_max_dma_range_reads = Measure-SampleField $samples 'scan_max_dma_range_reads'
    scan_max_dma_range_failed = Measure-SampleField $samples 'scan_max_dma_range_failed'
    scan_max_dma_range_max_latency_us = Measure-SampleField $samples 'scan_max_dma_range_max_latency_us'
    scan_max_dma_range_max_callsite = $last.scan_max_dma_range_max_callsite
    scan_max_dma_range_scanner_reads = Measure-SampleField $samples 'scan_max_dma_range_scanner_reads'
    scan_max_dma_range_scanner_max_latency_us = Measure-SampleField $samples 'scan_max_dma_range_scanner_max_latency_us'
    scan_max_dma_range_scanner_max_callsite = $last.scan_max_dma_range_scanner_max_callsite
    scan_max_dma_range_foreign_reads = Measure-SampleField $samples 'scan_max_dma_range_foreign_reads'
    scan_max_dma_range_foreign_max_latency_us = Measure-SampleField $samples 'scan_max_dma_range_foreign_max_latency_us'
    scan_max_dma_range_foreign_max_callsite = $last.scan_max_dma_range_foreign_max_callsite
    scan_max_dma_range_root_max_us = Measure-SampleField $samples 'scan_max_dma_range_root_max_us'
    scan_max_dma_range_list_read_max_us = Measure-SampleField $samples 'scan_max_dma_range_list_read_max_us'
    scan_max_dma_range_record_header_max_us = Measure-SampleField $samples 'scan_max_dma_range_record_header_max_us'
    scan_max_dma_range_record_pool_id_max_us = Measure-SampleField $samples 'scan_max_dma_range_record_pool_id_max_us'
    scan_max_dma_range_target_map_max_us = Measure-SampleField $samples 'scan_max_dma_range_target_map_max_us'
    scan_max_dma_range_component_validation_max_us = Measure-SampleField $samples 'scan_max_dma_range_component_validation_max_us'
    scan_max_dma_range_viewmatrix_max_us = Measure-SampleField $samples 'scan_max_dma_range_viewmatrix_max_us'
    scan_max_cn_ne_entity_list_root_cache_hit_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_root_cache_hit_count'
    scan_max_cn_ne_entity_list_root_cache_read_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_root_cache_read_count'
    scan_max_cn_ne_entity_list_root_cache_store_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_root_cache_store_count'
    scan_max_cn_ne_entity_list_root_cache_expired_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_root_cache_expired_count'
    scan_max_cn_ne_entity_list_root_cache_stale_hit_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_root_cache_stale_hit_count'
    scan_max_list_read_skipped_count = Measure-SampleField $samples 'scan_max_list_read_skipped_count'
    scan_max_cn_ne_entity_list_read_negative_cache_hit_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_negative_cache_hit_count'
    scan_max_cn_ne_entity_list_read_negative_cache_store_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_negative_cache_store_count'
    scan_max_cn_ne_entity_list_read_negative_cache_expired_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_negative_cache_expired_count'
    scan_max_cn_ne_entity_list_read_negative_cache_stale_hit_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_negative_cache_stale_hit_count'
    scan_max_cn_ne_entity_list_read_cache_hit_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_cache_hit_count'
    scan_max_cn_ne_entity_list_read_cache_store_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_cache_store_count'
    scan_max_cn_ne_entity_list_read_cache_expired_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_cache_expired_count'
    scan_max_cn_ne_entity_list_read_cache_stale_hit_count = Measure-SampleField $samples 'scan_max_cn_ne_entity_list_read_cache_stale_hit_count'
    scan_max_record_match_id_direct_read_count = Measure-SampleField $samples 'scan_max_record_match_id_direct_read_count'
    scan_max_record_match_id_direct_zero_count = Measure-SampleField $samples 'scan_max_record_match_id_direct_zero_count'
    scan_max_record_match_id_header_hit_count = Measure-SampleField $samples 'scan_max_record_match_id_header_hit_count'
    scan_max_record_match_id_header_miss_count = Measure-SampleField $samples 'scan_max_record_match_id_header_miss_count'
    scan_max_record_match_id_header_match_count = Measure-SampleField $samples 'scan_max_record_match_id_header_match_count'
    scan_max_record_match_id_header_mismatch_count = Measure-SampleField $samples 'scan_max_record_match_id_header_mismatch_count'
    scan_max_record_match_id_header_use_count = Measure-SampleField $samples 'scan_max_record_match_id_header_use_count'
    scan_max_persistent_refresh_count = Measure-SampleField $samples 'scan_max_persistent_refresh_count'
    scan_max_persistent_stale_hit_count = Measure-SampleField $samples 'scan_max_persistent_stale_hit_count'
    scan_pending_age_ms = Measure-SampleField $samples 'scan_pending_age_ms'
    scan_cold_topology_enabled = [bool]($last.scan_cold_topology_enabled)
    scan_topology_rescan_request_count = Measure-SampleField $samples 'scan_topology_rescan_request_count'
    scan_topology_rescan_request_delta = [math]::Max(0, $last.scan_topology_rescan_request_count - $first.scan_topology_rescan_request_count)
    scan_topology_count_probe_count = Measure-SampleField $samples 'scan_topology_count_probe_count'
    scan_topology_count_probe_delta = [math]::Max(0, $last.scan_topology_count_probe_count - $first.scan_topology_count_probe_count)
    scan_topology_count_probe_change_count = Measure-SampleField $samples 'scan_topology_count_probe_change_count'
    scan_topology_count_probe_change_delta = [math]::Max(0, $last.scan_topology_count_probe_change_count - $first.scan_topology_count_probe_change_count)
    scan_topology_candidate_count = Measure-SampleField $samples 'scan_topology_candidate_count'
    scan_root_ms = Measure-SampleField $samples 'scan_root_ms'
    scan_list_read_ms = Measure-SampleField $samples 'scan_list_read_ms'
    scan_slot_walk_ms = Measure-SampleField $samples 'scan_slot_walk_ms'
    scan_record_build_ms = Measure-SampleField $samples 'scan_record_build_ms'
    scan_match_link_ms = Measure-SampleField $samples 'scan_match_link_ms'
    scan_cn_ne_target_map_ms = Measure-SampleField $samples 'scan_cn_ne_target_map_ms'
    scan_cn_ne_self_validation_ms = Measure-SampleField $samples 'scan_cn_ne_self_validation_ms'
    scan_component_only_validation_ms = Measure-SampleField $samples 'scan_component_only_validation_ms'
    scan_dynamic_pair_ms = Measure-SampleField $samples 'scan_dynamic_pair_ms'
    scan_finalize_ms = Measure-SampleField $samples 'scan_finalize_ms'
    scan_dma_reads_delta = Measure-SampleField $samples 'scan_dma_reads_delta'
    scan_dma_fail_delta = Measure-SampleField $samples 'scan_dma_fail_delta'
    scan_dma_range_diag_enabled = [bool]($last.scan_dma_range_diag_enabled)
    scan_dma_range_reads = Measure-SampleField $samples 'scan_dma_range_reads'
    scan_dma_range_failed = Measure-SampleField $samples 'scan_dma_range_failed'
    scan_dma_range_max_latency_us = Measure-SampleField $samples 'scan_dma_range_max_latency_us'
    scan_dma_range_max_callsite = $last.scan_dma_range_max_callsite
    scan_dma_range_scanner_reads = Measure-SampleField $samples 'scan_dma_range_scanner_reads'
    scan_dma_range_scanner_max_latency_us = Measure-SampleField $samples 'scan_dma_range_scanner_max_latency_us'
    scan_dma_range_scanner_max_callsite = $last.scan_dma_range_scanner_max_callsite
    scan_dma_range_foreign_reads = Measure-SampleField $samples 'scan_dma_range_foreign_reads'
    scan_dma_range_foreign_max_latency_us = Measure-SampleField $samples 'scan_dma_range_foreign_max_latency_us'
    scan_dma_range_foreign_max_callsite = $last.scan_dma_range_foreign_max_callsite
    scan_dma_range_root_max_us = Measure-SampleField $samples 'scan_dma_range_root_max_us'
    scan_dma_range_list_read_max_us = Measure-SampleField $samples 'scan_dma_range_list_read_max_us'
    scan_dma_range_record_header_max_us = Measure-SampleField $samples 'scan_dma_range_record_header_max_us'
    scan_dma_range_record_pool_id_max_us = Measure-SampleField $samples 'scan_dma_range_record_pool_id_max_us'
    scan_dma_range_target_map_max_us = Measure-SampleField $samples 'scan_dma_range_target_map_max_us'
    scan_dma_range_component_validation_max_us = Measure-SampleField $samples 'scan_dma_range_component_validation_max_us'
    scan_dma_range_viewmatrix_max_us = Measure-SampleField $samples 'scan_dma_range_viewmatrix_max_us'
    scan_list_read_count = Measure-SampleField $samples 'scan_list_read_count'
    scan_list_read_fail_count = Measure-SampleField $samples 'scan_list_read_fail_count'
    scan_list_fallback_read_count = Measure-SampleField $samples 'scan_list_fallback_read_count'
    scan_list_read_skipped_count = Measure-SampleField $samples 'scan_list_read_skipped_count'
    scan_cn_ne_entity_list_chunk_size = Measure-SampleField $samples 'scan_cn_ne_entity_list_chunk_size'
    scan_record_add_attempt_count = Measure-SampleField $samples 'scan_record_add_attempt_count'
    scan_record_duplicate_count = Measure-SampleField $samples 'scan_record_duplicate_count'
    scan_record_header_read_count = Measure-SampleField $samples 'scan_record_header_read_count'
    scan_record_header_fail_count = Measure-SampleField $samples 'scan_record_header_fail_count'
    scan_record_remote_fallback_read_count = Measure-SampleField $samples 'scan_record_remote_fallback_read_count'
    scan_record_match_id_direct_read_count = Measure-SampleField $samples 'scan_record_match_id_direct_read_count'
    scan_record_match_id_direct_zero_count = Measure-SampleField $samples 'scan_record_match_id_direct_zero_count'
    scan_record_match_id_header_hit_count = Measure-SampleField $samples 'scan_record_match_id_header_hit_count'
    scan_record_match_id_header_miss_count = Measure-SampleField $samples 'scan_record_match_id_header_miss_count'
    scan_record_match_id_header_match_count = Measure-SampleField $samples 'scan_record_match_id_header_match_count'
    scan_record_match_id_header_mismatch_count = Measure-SampleField $samples 'scan_record_match_id_header_mismatch_count'
    scan_record_match_id_header_use_count = Measure-SampleField $samples 'scan_record_match_id_header_use_count'
    scan_cn_ne_record_match_id_from_header_enabled = [bool]($last.scan_cn_ne_record_match_id_from_header_enabled)
    scan_cn_ne_entity_list_root_cache_enabled = [bool]($last.scan_cn_ne_entity_list_root_cache_enabled)
    scan_cn_ne_entity_list_root_cache_ttl_ms = Measure-SampleField $samples 'scan_cn_ne_entity_list_root_cache_ttl_ms'
    scan_cn_ne_entity_list_root_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_root_cache_hit_count'
    scan_cn_ne_entity_list_root_cache_read_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_root_cache_read_count'
    scan_cn_ne_entity_list_root_cache_store_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_root_cache_store_count'
    scan_cn_ne_entity_list_root_cache_expired_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_root_cache_expired_count'
    scan_cn_ne_entity_list_root_cache_stale_hit_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_root_cache_stale_hit_count'
    scan_cn_ne_entity_list_read_negative_cache_enabled = [bool]($last.scan_cn_ne_entity_list_read_negative_cache_enabled)
    scan_cn_ne_entity_list_read_negative_cache_ttl_ms = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_negative_cache_ttl_ms'
    scan_cn_ne_entity_list_read_negative_cache_lookup_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_negative_cache_lookup_count'
    scan_cn_ne_entity_list_read_negative_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_negative_cache_hit_count'
    scan_cn_ne_entity_list_read_negative_cache_store_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_negative_cache_store_count'
    scan_cn_ne_entity_list_read_negative_cache_expired_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_negative_cache_expired_count'
    scan_cn_ne_entity_list_read_negative_cache_stale_hit_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_negative_cache_stale_hit_count'
    scan_cn_ne_entity_list_read_cache_enabled = [bool]($last.scan_cn_ne_entity_list_read_cache_enabled)
    scan_cn_ne_entity_list_read_cache_ttl_ms = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_cache_ttl_ms'
    scan_cn_ne_entity_list_read_cache_lookup_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_cache_lookup_count'
    scan_cn_ne_entity_list_read_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_cache_hit_count'
    scan_cn_ne_entity_list_read_cache_store_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_cache_store_count'
    scan_cn_ne_entity_list_read_cache_expired_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_cache_expired_count'
    scan_cn_ne_entity_list_read_cache_stale_hit_count = Measure-SampleField $samples 'scan_cn_ne_entity_list_read_cache_stale_hit_count'
    scan_cn_ne_scanner_stale_metadata_ms = Measure-SampleField $samples 'scan_cn_ne_scanner_stale_metadata_ms'
    scan_cn_ne_scanner_stale_metadata_only_enabled = [bool]($last.scan_cn_ne_scanner_stale_metadata_only_enabled)
    scan_cn_ne_record_snapshot_cache_enabled = [bool]($last.scan_cn_ne_record_snapshot_cache_enabled)
    scan_cn_ne_record_snapshot_cache_ttl_ms = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_ttl_ms'
    scan_cn_ne_record_snapshot_cache_lookup_count = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_lookup_count'
    scan_cn_ne_record_snapshot_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_hit_count'
    scan_cn_ne_record_snapshot_cache_store_count = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_store_count'
    scan_cn_ne_record_snapshot_cache_expired_count = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_expired_count'
    scan_cn_ne_record_snapshot_cache_refresh_budget = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_refresh_budget'
    scan_cn_ne_record_snapshot_cache_refresh_count = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_refresh_count'
    scan_cn_ne_record_snapshot_cache_stale_hit_count = Measure-SampleField $samples 'scan_cn_ne_record_snapshot_cache_stale_hit_count'
    scan_pool_id_read_count = Measure-SampleField $samples 'scan_pool_id_read_count'
    scan_match_lookup_count = Measure-SampleField $samples 'scan_match_lookup_count'
    scan_match_lookup_hit_count = Measure-SampleField $samples 'scan_match_lookup_hit_count'
    scan_add_pair_attempt_count = Measure-SampleField $samples 'scan_add_pair_attempt_count'
    scan_add_pair_duplicate_count = Measure-SampleField $samples 'scan_add_pair_duplicate_count'
    scan_link_decrypt_attempt_count = Measure-SampleField $samples 'scan_link_decrypt_attempt_count'
    scan_link_decrypt_success_count = Measure-SampleField $samples 'scan_link_decrypt_success_count'
    scan_cn_ne_link_decrypt_negative_cache_enabled = [bool]($last.scan_cn_ne_link_decrypt_negative_cache_enabled)
    scan_cn_ne_link_decrypt_negative_cache_ttl_ms = Measure-SampleField $samples 'scan_cn_ne_link_decrypt_negative_cache_ttl_ms'
    scan_cn_ne_link_decrypt_negative_cache_lookup_count = Measure-SampleField $samples 'scan_cn_ne_link_decrypt_negative_cache_lookup_count'
    scan_cn_ne_link_decrypt_negative_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_link_decrypt_negative_cache_hit_count'
    scan_cn_ne_link_decrypt_negative_cache_store_count = Measure-SampleField $samples 'scan_cn_ne_link_decrypt_negative_cache_store_count'
    scan_cn_ne_link_decrypt_negative_cache_expired_count = Measure-SampleField $samples 'scan_cn_ne_link_decrypt_negative_cache_expired_count'
    scan_cn_ne_link_decrypt_negative_cache_stale_hit_count = Measure-SampleField $samples 'scan_cn_ne_link_decrypt_negative_cache_stale_hit_count'
    scan_playable_validation_attempt_count = Measure-SampleField $samples 'scan_playable_validation_attempt_count'
    scan_playable_validation_success_count = Measure-SampleField $samples 'scan_playable_validation_success_count'
    scan_cn_ne_map_candidate_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_count'
    scan_cn_ne_target_map_attempt_count = Measure-SampleField $samples 'scan_cn_ne_target_map_attempt_count'
    scan_cn_ne_target_map_success_count = Measure-SampleField $samples 'scan_cn_ne_target_map_success_count'
    scan_cn_ne_bucket_entry_scan_count = Measure-SampleField $samples 'scan_cn_ne_bucket_entry_scan_count'
    scan_cn_ne_map_candidate_cache_enabled = [bool]($last.scan_cn_ne_map_candidate_cache_enabled)
    scan_cn_ne_map_candidate_cache_lookup_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_cache_lookup_count'
    scan_cn_ne_map_candidate_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_cache_hit_count'
    scan_cn_ne_map_candidate_cache_miss_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_cache_miss_count'
    scan_cn_ne_map_candidate_persistent_cache_enabled = [bool]($last.scan_cn_ne_map_candidate_persistent_cache_enabled)
    scan_cn_ne_map_candidate_persistent_cache_ttl_ms = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_ttl_ms'
    scan_cn_ne_map_candidate_persistent_cache_lookup_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_lookup_count'
    scan_cn_ne_map_candidate_persistent_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_hit_count'
    scan_cn_ne_map_candidate_persistent_cache_miss_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_miss_count'
    scan_cn_ne_map_candidate_persistent_cache_store_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_store_count'
    scan_cn_ne_map_candidate_persistent_cache_expired_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_expired_count'
    scan_cn_ne_map_candidate_persistent_cache_refresh_budget = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_refresh_budget'
    scan_cn_ne_map_candidate_persistent_cache_refresh_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_refresh_count'
    scan_cn_ne_map_candidate_persistent_cache_stale_hit_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_persistent_cache_stale_hit_count'
    scan_cn_ne_component_negative_cache_enabled = [bool]($last.scan_cn_ne_component_negative_cache_enabled)
    scan_cn_ne_component_negative_cache_ttl_ms = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_ttl_ms'
    scan_cn_ne_component_negative_cache_lookup_count = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_lookup_count'
    scan_cn_ne_component_negative_cache_hit_count = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_hit_count'
    scan_cn_ne_component_negative_cache_store_count = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_store_count'
    scan_cn_ne_component_negative_cache_expired_count = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_expired_count'
    scan_cn_ne_component_negative_cache_refresh_budget = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_refresh_budget'
    scan_cn_ne_component_negative_cache_refresh_count = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_refresh_count'
    scan_cn_ne_component_negative_cache_stale_hit_count = Measure-SampleField $samples 'scan_cn_ne_component_negative_cache_stale_hit_count'
    scan_cn_ne_map_diag_enabled = [bool]($last.scan_cn_ne_map_diag_enabled)
    scan_cn_ne_map_candidate_parent_lookup_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_parent_lookup_count'
    scan_cn_ne_map_candidate_unique_parent_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_unique_parent_count'
    scan_cn_ne_map_candidate_duplicate_parent_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_duplicate_parent_count'
    scan_cn_ne_map_candidate_direct_source_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_direct_source_count'
    scan_cn_ne_map_candidate_plus8_source_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_plus8_source_count'
    scan_cn_ne_map_candidate_wrapper_source_count = Measure-SampleField $samples 'scan_cn_ne_map_candidate_wrapper_source_count'
    scan_component_only_validation_attempt_count = Measure-SampleField $samples 'scan_component_only_validation_attempt_count'
    scan_component_only_validation_success_count = Measure-SampleField $samples 'scan_component_only_validation_success_count'
    scan_skip_pending_delta = [math]::Max(0, $last.scan_skip_pending_count - $first.scan_skip_pending_count)
    scan_skip_not_due_delta = [math]::Max(0, $last.scan_skip_not_due_count - $first.scan_skip_not_due_count)
    scan_overwritten_delta = [math]::Max(0, $last.scan_overwritten_count - $first.scan_overwritten_count)
    scan_detail_records = Measure-SampleField $samples 'scan_detail_records'
    scan_detail_total_pairs = Measure-SampleField $samples 'scan_detail_total_pairs'
    scan_light_scan_enabled = [bool]($last.scan_light_scan_enabled)
    scan_light_unvalidated_pairs = Measure-SampleField $samples 'scan_light_unvalidated_pairs'
    scan_light_cap_hits_delta = [math]::Max(0, $last.scan_light_cap_hits - $first.scan_light_cap_hits)
    entity_cycle_ms = Measure-SampleField $samples 'entity_cycle_ms'
    phase_begin_frame_ms = Measure-SampleField $samples 'phase_begin_frame_ms'
    phase_consume_scan_ms = Measure-SampleField $samples 'phase_consume_scan_ms'
    phase_previous_snapshot_copy_ms = Measure-SampleField $samples 'phase_previous_snapshot_copy_ms'
    phase_prefetch_ms = Measure-SampleField $samples 'phase_prefetch_ms'
    phase_previous_index_ms = Measure-SampleField $samples 'phase_previous_index_ms'
    phase_hot_scatter_prepare_ms = Measure-SampleField $samples 'phase_hot_scatter_prepare_ms'
    phase_hot_scatter_execute_ms = Measure-SampleField $samples 'phase_hot_scatter_execute_ms'
    phase_base_cache_ms = Measure-SampleField $samples 'phase_base_cache_ms'
    phase_base_decrypt_ms = Measure-SampleField $samples 'phase_base_decrypt_ms'
    phase_health_ms = Measure-SampleField $samples 'phase_health_ms'
    phase_hero_ms = Measure-SampleField $samples 'phase_hero_ms'
    phase_visibility_ms = Measure-SampleField $samples 'phase_visibility_ms'
    phase_skeleton_ms = Measure-SampleField $samples 'phase_skeleton_ms'
    phase_skeleton_velocity_read_ms = Measure-SampleField $samples 'phase_skeleton_velocity_read_ms'
    phase_skeleton_cache_call_ms = Measure-SampleField $samples 'phase_skeleton_cache_call_ms'
    phase_skill_ms = Measure-SampleField $samples 'phase_skill_ms'
    phase_team_name_ms = Measure-SampleField $samples 'phase_team_name_ms'
    phase_team_name_hero_lookup_ms = Measure-SampleField $samples 'phase_team_name_hero_lookup_ms'
    phase_team_name_bot_adjust_ms = Measure-SampleField $samples 'phase_team_name_bot_adjust_ms'
    phase_team_name_battle_tag_ms = Measure-SampleField $samples 'phase_team_name_battle_tag_ms'
    phase_team_name_team_read_ms = Measure-SampleField $samples 'phase_team_name_team_read_ms'
    phase_local_select_ms = Measure-SampleField $samples 'phase_local_select_ms'
    phase_publish_ms = Measure-SampleField $samples 'phase_publish_ms'
    phase_record_sync_ms = Measure-SampleField $samples 'phase_record_sync_ms'
    phase_entity_loop_wall_ms = Measure-SampleField $samples 'phase_entity_loop_wall_ms'
    phase_entity_loop_setup_ms = Measure-SampleField $samples 'phase_entity_loop_setup_ms'
    phase_entity_header_special_ms = Measure-SampleField $samples 'phase_entity_header_special_ms'
    phase_entity_header_component_ms = Measure-SampleField $samples 'phase_entity_header_component_ms'
    phase_entity_header_link_ms = Measure-SampleField $samples 'phase_entity_header_link_ms'
    phase_entity_special_probe_ms = Measure-SampleField $samples 'phase_entity_special_probe_ms'
    phase_entity_cache_apply_ms = Measure-SampleField $samples 'phase_entity_cache_apply_ms'
    phase_entity_cache_match_id_ms = Measure-SampleField $samples 'phase_entity_cache_match_id_ms'
    phase_entity_cache_record_update_ms = Measure-SampleField $samples 'phase_entity_cache_record_update_ms'
    phase_entity_hot_fields_ms = Measure-SampleField $samples 'phase_entity_hot_fields_ms'
    phase_entity_rotation_position_ms = Measure-SampleField $samples 'phase_entity_rotation_position_ms'
    phase_entity_loop_gap_ms = Measure-SampleField $samples 'phase_entity_loop_gap_ms'
    phase_cycle_gap_ms = Measure-SampleField $samples 'phase_cycle_gap_ms'
    hot_scatter_requested_count = Measure-SampleField $samples 'hot_scatter_requested_count'
    hot_scatter_prepare_requested_count = Measure-SampleField $samples 'hot_scatter_prepare_requested_count'
    hot_scatter_prepare_success_count = Measure-SampleField $samples 'hot_scatter_prepare_success_count'
    hot_scatter_prepare_fail_count = Measure-SampleField $samples 'hot_scatter_prepare_fail_count'
    hot_scatter_execute_count = Measure-SampleField $samples 'hot_scatter_execute_count'
    hot_scatter_execute_fail_count = Measure-SampleField $samples 'hot_scatter_execute_fail_count'
    hot_scatter_execute_fail_delta = [math]::Max(0, $last.hot_scatter_execute_fail_count - $first.hot_scatter_execute_fail_count)
    hot_scatter_bytes_requested = Measure-SampleField $samples 'hot_scatter_bytes_requested'
    hot_scatter_bytes_read = Measure-SampleField $samples 'hot_scatter_bytes_read'
    hot_scatter_short_read_count = Measure-SampleField $samples 'hot_scatter_short_read_count'
    hot_scatter_short_read_delta = [math]::Max(0, $last.hot_scatter_short_read_count - $first.hot_scatter_short_read_count)
    hot_scatter_batch_items = Measure-SampleField $samples 'hot_scatter_batch_items'
    hot_scatter_batch_requests = Measure-SampleField $samples 'hot_scatter_batch_requests'
    hot_scatter_estimated_unique_pages = Measure-SampleField $samples 'hot_scatter_estimated_unique_pages'
    hot_scatter_success_count = Measure-SampleField $samples 'hot_scatter_success_count'
    hot_scatter_partial_count = Measure-SampleField $samples 'hot_scatter_partial_count'
    hot_scatter_partial_delta = [math]::Max(0, $last.hot_scatter_partial_count - $first.hot_scatter_partial_count)
    hot_scatter_fallback_read_count = Measure-SampleField $samples 'hot_scatter_fallback_read_count'
    hot_scatter_fallback_read_delta = [math]::Max(0, $last.hot_scatter_fallback_read_count - $first.hot_scatter_fallback_read_count)
    visibility_scatter_hit_count = Measure-SampleField $samples 'visibility_scatter_hit_count'
    visibility_scatter_hit_delta = [math]::Max(0, $last.visibility_scatter_hit_count - $first.visibility_scatter_hit_count)
    base_cache_hit_count = Measure-SampleField $samples 'base_cache_hit_count'
    base_cache_hit_delta = [math]::Max(0, $last.base_cache_hit_count - $first.base_cache_hit_count)
    base_cache_miss_count = Measure-SampleField $samples 'base_cache_miss_count'
    base_cache_miss_delta = [math]::Max(0, $last.base_cache_miss_count - $first.base_cache_miss_count)
    base_decrypt_attempt_count = Measure-SampleField $samples 'base_decrypt_attempt_count'
    base_decrypt_attempt_delta = [math]::Max(0, $last.base_decrypt_attempt_count - $first.base_decrypt_attempt_count)
    base_decrypt_success_count = Measure-SampleField $samples 'base_decrypt_success_count'
    base_decrypt_success_delta = [math]::Max(0, $last.base_decrypt_success_count - $first.base_decrypt_success_count)
    base_decrypt_fail_count = Measure-SampleField $samples 'base_decrypt_fail_count'
    base_decrypt_fail_delta = [math]::Max(0, $last.base_decrypt_fail_count - $first.base_decrypt_fail_count)
    base_decrypt_slow_call_count = Measure-SampleField $samples 'base_decrypt_slow_call_count'
    base_decrypt_fallback_attempt_count = Measure-SampleField $samples 'base_decrypt_fallback_attempt_count'
    base_decrypt_fallback_attempt_delta = [math]::Max(0, $last.base_decrypt_fallback_attempt_count - $first.base_decrypt_fallback_attempt_count)
    base_decrypt_fallback_success_count = Measure-SampleField $samples 'base_decrypt_fallback_success_count'
    base_decrypt_fallback_success_delta = [math]::Max(0, $last.base_decrypt_fallback_success_count - $first.base_decrypt_fallback_success_count)
    base_decrypt_fallback_fail_count = Measure-SampleField $samples 'base_decrypt_fallback_fail_count'
    base_decrypt_fallback_fail_delta = [math]::Max(0, $last.base_decrypt_fallback_fail_count - $first.base_decrypt_fallback_fail_count)
    base_decrypt_unique_key_count = Measure-SampleField $samples 'base_decrypt_unique_key_count'
    base_decrypt_duplicate_key_count = Measure-SampleField $samples 'base_decrypt_duplicate_key_count'
    base_decrypt_max_duplicate_key_count = Measure-SampleField $samples 'base_decrypt_max_duplicate_key_count'
    base_decrypt_max_call_ms = Measure-SampleField $samples 'base_decrypt_max_call_ms'
    team_name_slow_call_count = Measure-SampleField $samples 'team_name_slow_call_count'
    team_name_max_call_ms = Measure-SampleField $samples 'team_name_max_call_ms'
    skeleton_cache_hit_count = Measure-SampleField $samples 'skeleton_cache_hit_count'
    skeleton_cache_hit_delta = [math]::Max(0, $last.skeleton_cache_hit_count - $first.skeleton_cache_hit_count)
    skeleton_cache_miss_count = Measure-SampleField $samples 'skeleton_cache_miss_count'
    skeleton_cache_miss_delta = [math]::Max(0, $last.skeleton_cache_miss_count - $first.skeleton_cache_miss_count)
    skeleton_block_read_bytes = Measure-SampleField $samples 'skeleton_block_read_bytes'
    skeleton_slow_call_count = Measure-SampleField $samples 'skeleton_slow_call_count'
    skeleton_max_call_ms = Measure-SampleField $samples 'skeleton_max_call_ms'
    skeleton_fallback_get_bone_pos_count = Measure-SampleField $samples 'skeleton_fallback_get_bone_pos_count'
    skeleton_fallback_get_bone_pos_delta = [math]::Max(0, $last.skeleton_fallback_get_bone_pos_count - $first.skeleton_fallback_get_bone_pos_count)
    visibility_fallback_count = Measure-SampleField $samples 'visibility_fallback_count'
    visibility_fallback_delta = [math]::Max(0, $last.visibility_fallback_count - $first.visibility_fallback_count)
    skill_due_count = Measure-SampleField $samples 'skill_due_count'
    skill_read_count = Measure-SampleField $samples 'skill_read_count'
    skill_read_delta = [math]::Max(0, $last.skill_read_count - $first.skill_read_count)
    skill_skipped_not_due_count = Measure-SampleField $samples 'skill_skipped_not_due_count'
    skill_skipped_not_due_delta = [math]::Max(0, $last.skill_skipped_not_due_count - $first.skill_skipped_not_due_count)
    entity_record_created_count = Measure-SampleField $samples 'entity_record_created_count'
    entity_record_updated_actor_count = Measure-SampleField $samples 'entity_record_updated_actor_count'
    entity_record_link_changed_count = Measure-SampleField $samples 'entity_record_link_changed_count'
    entity_record_link_changed_same_component_count = Measure-SampleField $samples 'entity_record_link_changed_same_component_count'
    entity_record_link_changed_component_changed_count = Measure-SampleField $samples 'entity_record_link_changed_component_changed_count'
    entity_record_link_changed_same_hero_count = Measure-SampleField $samples 'entity_record_link_changed_same_hero_count'
    entity_record_link_changed_hero_changed_count = Measure-SampleField $samples 'entity_record_link_changed_hero_changed_count'
    entity_record_link_changed_hero_unknown_count = Measure-SampleField $samples 'entity_record_link_changed_hero_unknown_count'
    entity_record_link_changed_match_key_count = Measure-SampleField $samples 'entity_record_link_changed_match_key_count'
    entity_record_link_changed_link_key_count = Measure-SampleField $samples 'entity_record_link_changed_link_key_count'
    entity_record_link_changed_component_key_count = Measure-SampleField $samples 'entity_record_link_changed_component_key_count'
    entity_record_mark_missing_count = Measure-SampleField $samples 'entity_record_mark_missing_count'
    entity_record_mark_dead_count = Measure-SampleField $samples 'entity_record_mark_dead_count'
    entity_record_expired_count = Measure-SampleField $samples 'entity_record_expired_count'
    entity_record_scan_miss_soft_gap_count = Measure-SampleField $samples 'entity_record_scan_miss_soft_gap_count'
    entity_record_scan_miss_hard_gap_count = Measure-SampleField $samples 'entity_record_scan_miss_hard_gap_count'
    entity_record_scan_miss_grace_append_count = Measure-SampleField $samples 'entity_record_scan_miss_grace_append_count'
    entity_record_scan_miss_grace_drop_count = Measure-SampleField $samples 'entity_record_scan_miss_grace_drop_count'
    entity_record_scan_miss_hot_read_success_count = Measure-SampleField $samples 'entity_record_scan_miss_hot_read_success_count'
    entity_record_scan_miss_hot_read_fail_count = Measure-SampleField $samples 'entity_record_scan_miss_hot_read_fail_count'
    component_cache_hit_count = Measure-SampleField $samples 'component_cache_hit_count'
    component_cache_miss_count = Measure-SampleField $samples 'component_cache_miss_count'
    component_cache_invalidate_interval_count = Measure-SampleField $samples 'component_cache_invalidate_interval_count'
    component_cache_invalidate_interval_skipped_lifetime_count = Measure-SampleField $samples 'component_cache_invalidate_interval_skipped_lifetime_count'
    component_cache_invalidate_link_change_count = Measure-SampleField $samples 'component_cache_invalidate_link_change_count'
    component_cache_invalidate_health_resurrect_count = Measure-SampleField $samples 'component_cache_invalidate_health_resurrect_count'
    component_cache_invalidate_hero_change_count = Measure-SampleField $samples 'component_cache_invalidate_hero_change_count'
    component_cache_link_change_previous_match_id_known_count = Measure-SampleField $samples 'component_cache_link_change_previous_match_id_known_count'
    component_cache_link_change_previous_match_id_zero_count = Measure-SampleField $samples 'component_cache_link_change_previous_match_id_zero_count'
    component_cache_link_change_previous_match_id_unknown_count = Measure-SampleField $samples 'component_cache_link_change_previous_match_id_unknown_count'
    component_cache_link_change_record_alias_hit_count = Measure-SampleField $samples 'component_cache_link_change_record_alias_hit_count'
    component_cache_link_change_record_alias_miss_count = Measure-SampleField $samples 'component_cache_link_change_record_alias_miss_count'
    component_cache_link_change_record_published_count = Measure-SampleField $samples 'component_cache_link_change_record_published_count'
    component_cache_link_change_record_bases_valid_count = Measure-SampleField $samples 'component_cache_link_change_record_bases_valid_count'
    component_cache_link_change_record_match_key_count = Measure-SampleField $samples 'component_cache_link_change_record_match_key_count'
    component_cache_link_change_record_link_key_count = Measure-SampleField $samples 'component_cache_link_change_record_link_key_count'
    component_cache_link_change_record_component_key_count = Measure-SampleField $samples 'component_cache_link_change_record_component_key_count'
    component_cache_link_retain_attempt_count = Measure-SampleField $samples 'component_cache_link_retain_attempt_count'
    component_cache_link_retain_success_count = Measure-SampleField $samples 'component_cache_link_retain_success_count'
    component_cache_link_retain_rejected_disabled_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_disabled_count'
    component_cache_link_retain_rejected_record_store_disabled_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_record_store_disabled_count'
    component_cache_link_retain_rejected_missing_record_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_missing_record_count'
    component_cache_link_retain_rejected_missing_match_id_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_missing_match_id_count'
    component_cache_link_retain_rejected_component_changed_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_component_changed_count'
    component_cache_link_retain_rejected_interval_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_interval_count'
    component_cache_link_retain_interval_bypassed_lifetime_count = Measure-SampleField $samples 'component_cache_link_retain_interval_bypassed_lifetime_count'
    component_cache_link_retain_rejected_hero_changed_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_hero_changed_count'
    component_cache_link_retain_rejected_hero_unknown_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_hero_unknown_count'
    component_cache_link_retain_rejected_decrypt_fail_count = Measure-SampleField $samples 'component_cache_link_retain_rejected_decrypt_fail_count'
    component_cache_link_retain_cached_hero_validate_count = Measure-SampleField $samples 'component_cache_link_retain_cached_hero_validate_count'
    component_cache_link_retain_refresh_decrypt_attempt_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_decrypt_attempt_count'
    component_cache_link_retain_refresh_decrypt_success_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_decrypt_success_count'
    component_cache_link_retain_refresh_decrypt_fail_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_decrypt_fail_count'
    component_cache_link_retain_refresh_link_attempt_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_link_attempt_count'
    component_cache_link_retain_refresh_link_success_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_link_success_count'
    component_cache_link_retain_refresh_link_fail_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_link_fail_count'
    component_cache_link_retain_refresh_hero_attempt_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_hero_attempt_count'
    component_cache_link_retain_refresh_hero_success_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_hero_success_count'
    component_cache_link_retain_refresh_hero_fail_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_hero_fail_count'
    component_cache_link_retain_refresh_visibility_attempt_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_visibility_attempt_count'
    component_cache_link_retain_refresh_visibility_success_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_visibility_success_count'
    component_cache_link_retain_refresh_visibility_fail_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_visibility_fail_count'
    component_cache_link_retain_refresh_angle_attempt_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_angle_attempt_count'
    component_cache_link_retain_refresh_angle_success_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_angle_success_count'
    component_cache_link_retain_refresh_angle_fail_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_angle_fail_count'
    component_cache_link_retain_refresh_angle_skipped_no_prior_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_angle_skipped_no_prior_count'
    component_cache_link_retain_refresh_angle_prior_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_angle_prior_count'
    component_cache_link_retain_refresh_angle_prior_fail_rejected_count = Measure-SampleField $samples 'component_cache_link_retain_refresh_angle_prior_fail_rejected_count'
    dynamic_cache_created_count = Measure-SampleField $samples 'dynamic_cache_created_count'
    dynamic_cache_reused_count = Measure-SampleField $samples 'dynamic_cache_reused_count'
    dynamic_cache_replaced_count = Measure-SampleField $samples 'dynamic_cache_replaced_count'
    dynamic_cache_expired_count = Measure-SampleField $samples 'dynamic_cache_expired_count'
    record_store_enabled = [bool]($last.record_store_enabled)
    record_store_size = Measure-SampleField $samples 'record_store_size'
    record_store_fresh_count = Measure-SampleField $samples 'record_store_fresh_count'
    record_store_missing_count = Measure-SampleField $samples 'record_store_missing_count'
    record_store_dead_count = Measure-SampleField $samples 'record_store_dead_count'
    record_store_expired_count = Measure-SampleField $samples 'record_store_expired_count'
    record_store_bases_valid_count = Measure-SampleField $samples 'record_store_bases_valid_count'
    record_store_dynamic_valid_count = Measure-SampleField $samples 'record_store_dynamic_valid_count'
    record_store_published_valid_count = Measure-SampleField $samples 'record_store_published_valid_count'
    sdk_component_key_cache_hit_delta = [math]::Max(0, $last.sdk_component_key_cache_hit_count - $first.sdk_component_key_cache_hit_count)
    sdk_component_key_cache_miss_delta = [math]::Max(0, $last.sdk_component_key_cache_miss_count - $first.sdk_component_key_cache_miss_count)
    sdk_begin_frame_scan_delta = [math]::Max(0, $last.sdk_begin_frame_scan_count - $first.sdk_begin_frame_scan_count)
    sdk_begin_frame_process_delta = [math]::Max(0, $last.sdk_begin_frame_process_count - $first.sdk_begin_frame_process_count)
    sdk_begin_frame_unknown_delta = [math]::Max(0, $last.sdk_begin_frame_unknown_count - $first.sdk_begin_frame_unknown_count)
    viewmatrix_poll_sleep_ms = Measure-SampleField $samples 'viewmatrix_poll_sleep_ms'
    viewmatrix_scan_backoff_ms = Measure-SampleField $samples 'viewmatrix_scan_backoff_ms'
    viewmatrix_scan_due_guard_ms = Measure-SampleField $samples 'viewmatrix_scan_due_guard_ms'
    viewmatrix_scan_backoff_delta = [math]::Max(0, $last.viewmatrix_scan_backoff_count - $first.viewmatrix_scan_backoff_count)
    viewmatrix_scan_due_guard_delta = [math]::Max(0, $last.viewmatrix_scan_due_guard_count - $first.viewmatrix_scan_due_guard_count)
    viewmatrix_publish_hz = Measure-SampleField $samples 'viewmatrix_publish_hz'
    viewmatrix_publish_age_ms = Measure-SampleField $samples 'viewmatrix_publish_age_ms'
    render_viewmatrix_age_ms = Measure-SampleField $samples 'render_viewmatrix_age_ms'
    render_viewmatrix_max_age_ms = Measure-SampleField $samples 'render_viewmatrix_max_age_ms'
    render_viewmatrix_uses_delta = [math]::Max(0, $last.render_viewmatrix_uses - $first.render_viewmatrix_uses)
    render_viewmatrix_over_16ms_delta = [math]::Max(0, $last.render_viewmatrix_over_16ms - $first.render_viewmatrix_over_16ms)
    render_viewmatrix_over_33ms_delta = [math]::Max(0, $last.render_viewmatrix_over_33ms - $first.render_viewmatrix_over_33ms)
    render_viewmatrix_over_50ms_delta = [math]::Max(0, $last.render_viewmatrix_over_50ms - $first.render_viewmatrix_over_50ms)
    render_viewmatrix_missing_publish_delta = [math]::Max(0, $last.render_viewmatrix_missing_publish_uses - $first.render_viewmatrix_missing_publish_uses)
    snapshot_entities_copy_ms = Measure-SampleField $samples 'snapshot_entities_copy_ms'
    snapshot_dynamic_copy_ms = Measure-SampleField $samples 'snapshot_dynamic_copy_ms'
    entity_count = Measure-SampleField $samples 'entity_count'
    view_matrix_bad_samples = $viewMatrixBadSamples
    resolved_screen_sizes = $resolvedSizeKeys
    projection_jump_samples = $projectionJumpSamples
    render_prediction_candidates = Measure-SampleField $samples 'render_prediction_candidates'
    render_prediction_applied = Measure-SampleField $samples 'render_prediction_applied'
    render_prediction_world_delta_fallback = Measure-SampleField $samples 'render_prediction_world_delta_fallback'
    render_prediction_max_lead_ms = Measure-SampleField $samples 'render_prediction_max_lead_ms'
    render_prediction_max_offset_cm = Measure-SampleField $samples 'render_prediction_max_offset_cm'
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
    dma_window_total = Measure-SampleField $samples 'dma_window_total'
    dma_window_failed = Measure-SampleField $samples 'dma_window_failed'
    dma_window_max_latency_us = Measure-SampleField $samples 'dma_window_max_latency_us'
    dma_window_slow_threshold_us = Measure-SampleField $samples 'dma_window_slow_threshold_us'
    dma_window_slow_reads = Measure-SampleField $samples 'dma_window_slow_reads'
    dma_window_slow_failed = Measure-SampleField $samples 'dma_window_slow_failed'
    dma_slow_samples_count = Measure-SampleField $samples 'dma_slow_samples_count'
    dma_slowest_sample_latency_us = Measure-SampleField $samples 'dma_slowest_sample_latency_us'
    dma_slowest_sample_callsite = if ($summarySlowestSample) { $summarySlowestSample.dma_slowest_sample_callsite } else { '' }
    dma_slowest_sample_success = if ($summarySlowestSample) { [bool]($summarySlowestSample.dma_slowest_sample_success) } else { $false }
    dma_slowest_sample_thread_id = if ($summarySlowestSample) { $summarySlowestSample.dma_slowest_sample_thread_id } else { 0 }
    dma_slowest_sample_started_tick_ms = if ($summarySlowestSample) { $summarySlowestSample.dma_slowest_sample_started_tick_ms } else { 0 }
    dma_slowest_sample_completed_tick_ms = if ($summarySlowestSample) { $summarySlowestSample.dma_slowest_sample_completed_tick_ms } else { 0 }
    dma_slowest_sample_completed_age_ms = Measure-SampleField $samples 'dma_slowest_sample_completed_age_ms'
    dma_slowest_sample_started_age_ms = Measure-SampleField $samples 'dma_slowest_sample_started_age_ms'
    dma_window_entity_scan_reads = Measure-SampleField $samples 'dma_window_entity_scan_reads'
    dma_window_entity_scan_max_us = Measure-SampleField $samples 'dma_window_entity_scan_max_us'
    dma_window_entity_scan_root_reads = Measure-SampleField $samples 'dma_window_entity_scan_root_reads'
    dma_window_entity_scan_root_max_us = Measure-SampleField $samples 'dma_window_entity_scan_root_max_us'
    dma_window_entity_scan_list_read_reads = Measure-SampleField $samples 'dma_window_entity_scan_list_read_reads'
    dma_window_entity_scan_list_read_failed = Measure-SampleField $samples 'dma_window_entity_scan_list_read_failed'
    dma_window_entity_scan_list_read_max_us = Measure-SampleField $samples 'dma_window_entity_scan_list_read_max_us'
    dma_window_entity_scan_list_read_success_max_us = Measure-SampleField $samples 'dma_window_entity_scan_list_read_success_max_us'
    dma_window_entity_scan_list_read_failed_max_us = Measure-SampleField $samples 'dma_window_entity_scan_list_read_failed_max_us'
    dma_window_entity_scan_list_read_slow_reads = Measure-SampleField $samples 'dma_window_entity_scan_list_read_slow_reads'
    dma_window_entity_scan_list_read_slow_failed = Measure-SampleField $samples 'dma_window_entity_scan_list_read_slow_failed'
    dma_window_entity_scan_record_build_reads = Measure-SampleField $samples 'dma_window_entity_scan_record_build_reads'
    dma_window_entity_scan_record_build_max_us = Measure-SampleField $samples 'dma_window_entity_scan_record_build_max_us'
    dma_window_entity_scan_record_match_id_reads = Measure-SampleField $samples 'dma_window_entity_scan_record_match_id_reads'
    dma_window_entity_scan_record_match_id_max_us = Measure-SampleField $samples 'dma_window_entity_scan_record_match_id_max_us'
    dma_window_entity_scan_record_header_reads = Measure-SampleField $samples 'dma_window_entity_scan_record_header_reads'
    dma_window_entity_scan_record_header_max_us = Measure-SampleField $samples 'dma_window_entity_scan_record_header_max_us'
    dma_window_entity_scan_record_pool_ptr_reads = Measure-SampleField $samples 'dma_window_entity_scan_record_pool_ptr_reads'
    dma_window_entity_scan_record_pool_ptr_max_us = Measure-SampleField $samples 'dma_window_entity_scan_record_pool_ptr_max_us'
    dma_window_entity_scan_record_pool_id_reads = Measure-SampleField $samples 'dma_window_entity_scan_record_pool_id_reads'
    dma_window_entity_scan_record_pool_id_max_us = Measure-SampleField $samples 'dma_window_entity_scan_record_pool_id_max_us'
    dma_window_entity_scan_match_link_reads = Measure-SampleField $samples 'dma_window_entity_scan_match_link_reads'
    dma_window_entity_scan_match_link_max_us = Measure-SampleField $samples 'dma_window_entity_scan_match_link_max_us'
    dma_window_entity_scan_target_map_reads = Measure-SampleField $samples 'dma_window_entity_scan_target_map_reads'
    dma_window_entity_scan_target_map_failed = Measure-SampleField $samples 'dma_window_entity_scan_target_map_failed'
    dma_window_entity_scan_target_map_max_us = Measure-SampleField $samples 'dma_window_entity_scan_target_map_max_us'
    dma_window_entity_scan_target_map_success_max_us = Measure-SampleField $samples 'dma_window_entity_scan_target_map_success_max_us'
    dma_window_entity_scan_target_map_failed_max_us = Measure-SampleField $samples 'dma_window_entity_scan_target_map_failed_max_us'
    dma_window_entity_scan_target_map_slow_reads = Measure-SampleField $samples 'dma_window_entity_scan_target_map_slow_reads'
    dma_window_entity_scan_target_map_slow_failed = Measure-SampleField $samples 'dma_window_entity_scan_target_map_slow_failed'
    dma_window_entity_scan_map_candidate_reads = Measure-SampleField $samples 'dma_window_entity_scan_map_candidate_reads'
    dma_window_entity_scan_map_candidate_max_us = Measure-SampleField $samples 'dma_window_entity_scan_map_candidate_max_us'
    dma_window_entity_scan_map_candidate_slow_reads = Measure-SampleField $samples 'dma_window_entity_scan_map_candidate_slow_reads'
    dma_window_entity_scan_link_target_resolve_reads = Measure-SampleField $samples 'dma_window_entity_scan_link_target_resolve_reads'
    dma_window_entity_scan_link_target_resolve_max_us = Measure-SampleField $samples 'dma_window_entity_scan_link_target_resolve_max_us'
    dma_window_entity_scan_link_target_resolve_slow_reads = Measure-SampleField $samples 'dma_window_entity_scan_link_target_resolve_slow_reads'
    dma_window_entity_scan_self_validation_reads = Measure-SampleField $samples 'dma_window_entity_scan_self_validation_reads'
    dma_window_entity_scan_self_validation_max_us = Measure-SampleField $samples 'dma_window_entity_scan_self_validation_max_us'
    dma_window_entity_scan_component_validation_reads = Measure-SampleField $samples 'dma_window_entity_scan_component_validation_reads'
    dma_window_entity_scan_component_validation_max_us = Measure-SampleField $samples 'dma_window_entity_scan_component_validation_max_us'
    dma_window_entity_decrypt_reads = Measure-SampleField $samples 'dma_window_entity_decrypt_reads'
    dma_window_entity_decrypt_max_us = Measure-SampleField $samples 'dma_window_entity_decrypt_max_us'
    dma_window_entity_base_decrypt_reads = Measure-SampleField $samples 'dma_window_entity_base_decrypt_reads'
    dma_window_entity_base_decrypt_max_us = Measure-SampleField $samples 'dma_window_entity_base_decrypt_max_us'
    dma_window_entity_header_special_reads = Measure-SampleField $samples 'dma_window_entity_header_special_reads'
    dma_window_entity_header_special_max_us = Measure-SampleField $samples 'dma_window_entity_header_special_max_us'
    dma_window_entity_hot_scatter_reads = Measure-SampleField $samples 'dma_window_entity_hot_scatter_reads'
    dma_window_entity_hot_scatter_max_us = Measure-SampleField $samples 'dma_window_entity_hot_scatter_max_us'
    dma_window_entity_hot_fields_reads = Measure-SampleField $samples 'dma_window_entity_hot_fields_reads'
    dma_window_entity_hot_fields_max_us = Measure-SampleField $samples 'dma_window_entity_hot_fields_max_us'
    dma_window_entity_rotation_position_reads = Measure-SampleField $samples 'dma_window_entity_rotation_position_reads'
    dma_window_entity_rotation_position_max_us = Measure-SampleField $samples 'dma_window_entity_rotation_position_max_us'
    dma_window_entity_prefetch_reads = Measure-SampleField $samples 'dma_window_entity_prefetch_reads'
    dma_window_entity_prefetch_max_us = Measure-SampleField $samples 'dma_window_entity_prefetch_max_us'
    dma_window_viewmatrix_reads = Measure-SampleField $samples 'dma_window_viewmatrix_reads'
    dma_window_viewmatrix_failed = Measure-SampleField $samples 'dma_window_viewmatrix_failed'
    dma_window_viewmatrix_max_us = Measure-SampleField $samples 'dma_window_viewmatrix_max_us'
    dma_window_viewmatrix_success_max_us = Measure-SampleField $samples 'dma_window_viewmatrix_success_max_us'
    dma_window_viewmatrix_failed_max_us = Measure-SampleField $samples 'dma_window_viewmatrix_failed_max_us'
    dma_window_viewmatrix_slow_reads = Measure-SampleField $samples 'dma_window_viewmatrix_slow_reads'
    dma_window_viewmatrix_slow_failed = Measure-SampleField $samples 'dma_window_viewmatrix_slow_failed'
    slow_frame_log_lines = $slowLines.Count
    slow_frame_effective_count = $slowFrameEffectiveCount
    slow_frame_max_render_ms = $slowRenderMaxMs
    slow_frame_rt_dma_nonzero_lines = $rtDmaNonZero
    slow_frame_render_canvas_nonzero_lines = $renderCanvasNonZero
    log_path = (Resolve-Path -LiteralPath $DiagLog -ErrorAction SilentlyContinue).Path
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$safeLabel = ($Label -replace '[^A-Za-z0-9_.-]', '_').Trim('_')
$fileStem = if ([string]::IsNullOrWhiteSpace($safeLabel)) {
    "perf-monitor-$stamp"
} else {
    "perf-monitor-$safeLabel-$stamp"
}
$samplePath = Join-Path $OutDir "$fileStem.samples.csv"
$summaryPath = Join-Path $OutDir "$fileStem.summary.json"

$samples | Export-Csv -NoTypeInformation -LiteralPath $samplePath
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host ""
Write-Host "Summary:"
($summary | ConvertTo-Json -Depth 8)
Write-Host ""
Write-Host "Wrote samples: $samplePath"
Write-Host "Wrote summary: $summaryPath"
