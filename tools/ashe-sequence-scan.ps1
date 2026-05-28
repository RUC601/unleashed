param(
    [string]$DistDir = ".\dist\Unleashed-test",
    [double[]]$Scales = @(1.0, 0.9, 0.8, 0.7, 0.6, 0.5),
    [int]$DelayMs = 8000,
    [int]$DurationMs = 10000,
    [int]$RecoverySeconds = 10,
    [int]$ActivationKey = 2,
    [int]$ReloadDelayMs = 250,
    [int]$ReloadTapMs = 80,
    [int]$PostTestMs = 5000,
    [ValidateSet("Measured", "BruteLeft", "SpacedLeft", "ScopedProbe", "Semantic4Safe", "Semantic4R300", "Semantic4R280", "Semantic4R240", "Semantic4R200", "Semantic4R190", "Semantic4R180", "Semantic4R160")]
    [string]$Pattern = "BruteLeft",
    [switch]$KeepLastScale
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$script:AsheHeroId = "0x2E0000000000200"
$script:SkillId = "fire-pattern"
$script:MeasuredSteps = @(
    @{ buttonMask = 1; durationMs = 73 },
    @{ buttonMask = 3; durationMs = 11 },
    @{ buttonMask = 2; durationMs = 134 },
    @{ buttonMask = 3; durationMs = 27 },
    @{ buttonMask = 2; durationMs = 68 },
    @{ buttonMask = 0; durationMs = 20 },
    @{ buttonMask = 1; durationMs = 57 },
    @{ buttonMask = 0; durationMs = 128 }
)
$script:BruteLeftSteps = @(
    @{ buttonMask = 1; durationMs = 73 },
    @{ buttonMask = 3; durationMs = 11 },
    @{ buttonMask = 2; durationMs = 95 },
    @{ buttonMask = 3; durationMs = 18 },
    @{ buttonMask = 2; durationMs = 28 },
    @{ buttonMask = 3; durationMs = 18 },
    @{ buttonMask = 2; durationMs = 28 },
    @{ buttonMask = 3; durationMs = 18 },
    @{ buttonMask = 2; durationMs = 28 },
    @{ buttonMask = 3; durationMs = 18 },
    @{ buttonMask = 2; durationMs = 28 },
    @{ buttonMask = 3; durationMs = 18 },
    @{ buttonMask = 2; durationMs = 28 },
    @{ buttonMask = 0; durationMs = 16 },
    @{ buttonMask = 1; durationMs = 34 },
    @{ buttonMask = 0; durationMs = 87 }
)
$script:SpacedLeftSteps = @(
    @{ buttonMask = 1; durationMs = 73 },
    @{ buttonMask = 3; durationMs = 11 },
    @{ buttonMask = 2; durationMs = 125 },
    @{ buttonMask = 3; durationMs = 24 },
    @{ buttonMask = 2; durationMs = 105 },
    @{ buttonMask = 3; durationMs = 24 },
    @{ buttonMask = 2; durationMs = 15 },
    @{ buttonMask = 0; durationMs = 16 },
    @{ buttonMask = 1; durationMs = 42 },
    @{ buttonMask = 0; durationMs = 83 }
)
$script:ScopedProbeSteps = @(
    @{ buttonMask = 2; durationMs = 350 },
    @{ buttonMask = 3; durationMs = 100 },
    @{ buttonMask = 2; durationMs = 350 },
    @{ buttonMask = 0; durationMs = 200 }
)
$script:Semantic4SafeSteps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 300 }
)
$script:Semantic4R240Steps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 240 }
)
$script:Semantic4R300Steps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 300 }
)
$script:Semantic4R280Steps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 280 }
)
$script:Semantic4R200Steps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 200 }
)
$script:Semantic4R160Steps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 160 }
)
$script:Semantic4R190Steps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 190 }
)
$script:Semantic4R180Steps = @(
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 2; durationMs = 245 },
    @{ buttonMask = 3; durationMs = 70 },
    @{ buttonMask = 2; durationMs = 210 },
    @{ buttonMask = 0; durationMs = 154 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 182 },
    @{ buttonMask = 1; durationMs = 49 },
    @{ buttonMask = 0; durationMs = 180 }
)
$script:BaselineSteps = if ($Pattern -eq "BruteLeft") {
    $script:BruteLeftSteps
} elseif ($Pattern -eq "SpacedLeft") {
    $script:SpacedLeftSteps
} elseif ($Pattern -eq "ScopedProbe") {
    $script:ScopedProbeSteps
} elseif ($Pattern -eq "Semantic4Safe") {
    $script:Semantic4SafeSteps
} elseif ($Pattern -eq "Semantic4R300") {
    $script:Semantic4R300Steps
} elseif ($Pattern -eq "Semantic4R280") {
    $script:Semantic4R280Steps
} elseif ($Pattern -eq "Semantic4R240") {
    $script:Semantic4R240Steps
} elseif ($Pattern -eq "Semantic4R200") {
    $script:Semantic4R200Steps
} elseif ($Pattern -eq "Semantic4R190") {
    $script:Semantic4R190Steps
} elseif ($Pattern -eq "Semantic4R180") {
    $script:Semantic4R180Steps
} elseif ($Pattern -eq "Semantic4R160") {
    $script:Semantic4R160Steps
} else {
    $script:MeasuredSteps
}

function Resolve-FullPath([string]$Path) {
    return (Resolve-Path -LiteralPath $Path).Path
}

function Set-AsheScale([string]$Path, [double]$Scale) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $json = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    $heroProp = $json.heroSkillPresets.PSObject.Properties[$script:AsheHeroId]
    if ($null -eq $heroProp) {
        return
    }
    $hero = $heroProp.Value

    $skillProp = $hero.PSObject.Properties[$script:SkillId]
    if ($null -eq $skillProp) {
        return
    }
    $skill = $skillProp.Value

    $skill.enabled = $true
    $skill.key = $ActivationKey
    $steps = @()
    foreach ($step in $script:BaselineSteps) {
        $steps += [pscustomobject]@{
            buttonMask = [int]$step["buttonMask"]
            durationMs = [int]$step["durationMs"]
            speedScale = [double]$Scale
            jitterMs = 0
        }
    }
    $skill.sequenceSteps = $steps
    if ($null -eq $skill.PSObject.Properties["trackingFov"]) {
        $skill | Add-Member -NotePropertyName trackingFov -NotePropertyValue 0
    } else {
        $skill.PSObject.Properties["trackingFov"].Value = 0
    }
    if ($null -eq $skill.PSObject.Properties["trackingBone"]) {
        $skill | Add-Member -NotePropertyName trackingBone -NotePropertyValue 0
    } else {
        $skill.PSObject.Properties["trackingBone"].Value = 0
    }

    $json | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Disable-AimTriggerKeys([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $content = Get-Content -Raw -LiteralPath $Path
    $content = [regex]::Replace($content, "(?m)^(aim_key|aim_key2|triggerbotKey|triggerbotKey2)\s*=.*$", '$1=14')
    Set-Content -LiteralPath $Path -Value $content -Encoding UTF8
}

function Expected-CycleMs([double]$Scale) {
    $total = 0
    foreach ($step in $script:BaselineSteps) {
        $duration = [int][Math]::Round([double]$step["durationMs"] * $Scale)
        if ($duration -lt 5) { $duration = 5 }
        if ($duration -gt 1000) { $duration = 1000 }
        $total += $duration
    }
    return $total
}

function Parse-LogTime([string]$Line) {
    if ($Line -match '^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]') {
        return [DateTime]::ParseExact(
            $Matches[1],
            "yyyy-MM-dd HH:mm:ss.fff",
            [Globalization.CultureInfo]::InvariantCulture)
    }
    return $null
}

function Classify-AsheDamage([double]$Damage) {
    if ($Damage -ge 130) { return "ADS/head" }
    if ($Damage -ge 65 -and $Damage -lt 90) { return "ADS/body-or-hip/head" }
    if ($Damage -ge 45 -and $Damage -lt 65) { return "mid/unknown" }
    if ($Damage -ge 25 -and $Damage -lt 45) { return "hip/body" }
    if ($Damage -gt 0) { return "chip/unknown" }
    return "none"
}

function Parse-AimLog([string]$LogPath, [double]$Scale) {
    $result = [ordered]@{
        pattern = $Pattern
        scale = $Scale
        expectedCycleMs = Expected-CycleMs $Scale
        worker = $false
        cycles = 0
        cycleAvgMs = 0
        cycleMinMs = 0
        cycleMaxMs = 0
        cycleErrorAvgMs = 0
        hitEvents = 0
        firstHitMs = $null
        hitAvgDtMs = 0
        hitMaxDtMs = 0
        damage = 0
        damageProfile = ""
        hitDamages = ""
        hitClasses = ""
        cycleProfiles = ""
        stableCycles = 0
        lastHealth = $null
        queueDrops = 0
        logPath = $LogPath
    }

    if (-not (Test-Path -LiteralPath $LogPath)) {
        return [pscustomobject]$result
    }

    $lines = Get-Content -LiteralPath $LogPath
    $stepOneTimes = New-Object System.Collections.Generic.List[DateTime]
    $hitTimes = New-Object System.Collections.Generic.List[int]
    $hits = New-Object System.Collections.Generic.List[object]
    $hitRecords = New-Object System.Collections.Generic.List[object]
    $firstHealth = $null
    $lastHealth = $null

    foreach ($line in $lines) {
        if ($line -match 'sequence\.worker_mode enabled=1') {
            $result["worker"] = $true
        }
        if ($line -match 'queue_full|drop_oldest|send_failed|not_connected') {
            $result["queueDrops"] = [int]$result["queueDrops"] + 1
        }
        if ($line -match 'sequence\.step .* step=1 ') {
            $time = Parse-LogTime $line
            if ($null -ne $time) {
                $stepOneTimes.Add($time)
            }
        }
        if ($line -match 'sequence\.hit_timing .* tMs=(\d+) dtMs=(\d+) health=([0-9.]+) delta=([-0-9.]+) damageEvents=(\d+)') {
            $hitWallTime = Parse-LogTime $line
            $tMs = [int]$Matches[1]
            $dtMs = [int]$Matches[2]
            $hitTimes.Add($tMs)
            $health = [double]$Matches[3]
            $delta = [double]$Matches[4]
            $damage = [Math]::Round(-$delta, 1)
            if ($null -eq $firstHealth) {
                $firstHealth = $health - $delta
            }
            $lastHealth = $health
            $result["hitEvents"] = [int]$Matches[5]
            $hits.Add([pscustomobject]@{
                index = [int]$Matches[5]
                tMs = $tMs
                dtMs = $dtMs
                damage = $damage
                class = Classify-AsheDamage $damage
                health = $health
            })
            if ($null -ne $hitWallTime) {
                $hitRecords.Add([pscustomobject]@{
                    time = $hitWallTime
                    tMs = $tMs
                    damage = $damage
                })
            }
        }
        if ($line -match 'sequence\.hit_summary .* damageEvents=(\d+) .* lastHealth=([0-9.]+)') {
            $result["hitEvents"] = [int]$Matches[1]
            $lastHealth = [double]$Matches[2]
        }
    }

    if ($stepOneTimes.Count -gt 1) {
        $intervals = New-Object System.Collections.Generic.List[int]
        for ($i = 1; $i -lt $stepOneTimes.Count; ++$i) {
            $intervals.Add([int][Math]::Round(($stepOneTimes[$i] - $stepOneTimes[$i - 1]).TotalMilliseconds))
        }
        $result["cycles"] = $intervals.Count
        $result["cycleAvgMs"] = [Math]::Round(($intervals | Measure-Object -Average).Average, 1)
        $result["cycleMinMs"] = ($intervals | Measure-Object -Minimum).Minimum
        $result["cycleMaxMs"] = ($intervals | Measure-Object -Maximum).Maximum
        $result["cycleErrorAvgMs"] = [Math]::Round([double]$result["cycleAvgMs"] - [double]$result["expectedCycleMs"], 1)
    }

    if ($hitTimes.Count -gt 0) {
        $result["firstHitMs"] = $hitTimes[0]
    }
    if ($hitTimes.Count -gt 1) {
        $deltas = New-Object System.Collections.Generic.List[int]
        for ($i = 1; $i -lt $hitTimes.Count; ++$i) {
            $deltas.Add($hitTimes[$i] - $hitTimes[$i - 1])
        }
        $result["hitAvgDtMs"] = [Math]::Round(($deltas | Measure-Object -Average).Average, 1)
        $result["hitMaxDtMs"] = ($deltas | Measure-Object -Maximum).Maximum
    }
    if ($null -ne $firstHealth -and $null -ne $lastHealth) {
        $result["damage"] = [Math]::Round($firstHealth - $lastHealth, 1)
    }
    if ($hits.Count -gt 0) {
        $result["hitDamages"] = (($hits | ForEach-Object { [string]$_.damage }) -join ",")
        $result["hitClasses"] = (($hits | ForEach-Object { $_.class }) -join ",")
        $result["damageProfile"] = (($hits | ForEach-Object { "{0}:{1}({2})" -f $_.tMs, $_.damage, $_.class }) -join ", ")
    }
    if ($stepOneTimes.Count -gt 0 -and $hitRecords.Count -gt 0) {
        $cycleProfiles = New-Object System.Collections.Generic.List[string]
        $stableCycles = 0
        for ($cycleIndex = 0; $cycleIndex -lt $stepOneTimes.Count; ++$cycleIndex) {
            $cycleStart = $stepOneTimes[$cycleIndex]
            $cycleEnd = if ($cycleIndex + 1 -lt $stepOneTimes.Count) {
                $stepOneTimes[$cycleIndex + 1]
            } else {
                $cycleStart.AddMilliseconds((Expected-CycleMs $Scale))
            }

            $cycleDamages = @(
                $hitRecords |
                    Where-Object { $_.time -ge $cycleStart -and $_.time -lt $cycleEnd } |
                    ForEach-Object { [int]$_.damage }
            )
            if ($cycleDamages.Count -gt 0) {
                $profile = ($cycleDamages -join "-")
                $cycleProfiles.Add(("{0}:{1}" -f ($cycleIndex + 1), $profile))
                if ($profile -eq "35-75-35-35") {
                    ++$stableCycles
                }
            }
        }
        $result["cycleProfiles"] = ($cycleProfiles -join ", ")
        $result["stableCycles"] = $stableCycles
    }
    if ($null -ne $lastHealth) {
        $result["lastHealth"] = $lastHealth
    }

    return [pscustomobject]$result
}

$dist = Resolve-FullPath $DistDir
$exe = Join-Path $dist "Unleashed.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Unleashed.exe not found: $exe"
}

$jsonProfiles = @(
    (Join-Path $dist "config.heroes.json"),
    (Join-Path $dist "rage_xy0headonly.heroes.json")
)
$iniProfiles = @(
    (Join-Path $dist "config.ini"),
    (Join-Path $dist "rage_xy0headonly.ini")
)

$backupDir = Join-Path $dist ("ashe_scan_backup_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $backupDir | Out-Null
foreach ($path in $jsonProfiles + $iniProfiles) {
    if (Test-Path -LiteralPath $path) {
        Copy-Item -LiteralPath $path -Destination (Join-Path $backupDir (Split-Path -Leaf $path))
    }
}

$oldSequenceTest = $env:UNLEASHED_SEQUENCE_TEST
$oldTestKey = $env:UNLEASHED_SEQUENCE_TEST_KEY
$oldDelay = $env:UNLEASHED_SEQUENCE_TEST_DELAY_MS
$oldDuration = $env:UNLEASHED_SEQUENCE_TEST_DURATION_MS
$oldWorker = $env:UNLEASHED_SEQUENCE_WORKER
$oldHero = $env:UNLEASHED_SEQUENCE_TEST_HERO
$oldSkill = $env:UNLEASHED_SEQUENCE_TEST_SKILL
$oldReload = $env:UNLEASHED_SEQUENCE_TEST_RELOAD
$oldReloadDelay = $env:UNLEASHED_SEQUENCE_TEST_RELOAD_DELAY_MS
$oldReloadTap = $env:UNLEASHED_SEQUENCE_TEST_RELOAD_TAP_MS
$oldReloadVk = $env:UNLEASHED_SEQUENCE_TEST_RELOAD_VK

$results = @()
try {
    foreach ($ini in $iniProfiles) {
        Disable-AimTriggerKeys $ini
    }

    foreach ($scale in $Scales) {
        foreach ($jsonPath in $jsonProfiles) {
            Set-AsheScale $jsonPath $scale
        }

        $aimLog = Join-Path $dist "unleashed_aim_diag.log"
        $diagLog = Join-Path $dist "unleashed_diag.log"
        Remove-Item -LiteralPath $aimLog -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $diagLog -ErrorAction SilentlyContinue

        $stdout = Join-Path $dist ("ashe_scan_stdout_{0:N2}.log" -f $scale)
        $stderr = Join-Path $dist ("ashe_scan_stderr_{0:N2}.log" -f $scale)
        Remove-Item -LiteralPath $stdout -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $stderr -ErrorAction SilentlyContinue

        $env:UNLEASHED_SEQUENCE_TEST = "1"
        $env:UNLEASHED_SEQUENCE_TEST_KEY = [string]$ActivationKey
        $env:UNLEASHED_SEQUENCE_TEST_DELAY_MS = [string]$DelayMs
        $env:UNLEASHED_SEQUENCE_TEST_DURATION_MS = [string]$DurationMs
        $env:UNLEASHED_SEQUENCE_WORKER = "1"
        $env:UNLEASHED_SEQUENCE_TEST_HERO = $script:AsheHeroId
        $env:UNLEASHED_SEQUENCE_TEST_SKILL = $script:SkillId
        $env:UNLEASHED_SEQUENCE_TEST_RELOAD = "1"
        $env:UNLEASHED_SEQUENCE_TEST_RELOAD_DELAY_MS = [string]$ReloadDelayMs
        $env:UNLEASHED_SEQUENCE_TEST_RELOAD_TAP_MS = [string]$ReloadTapMs
        $env:UNLEASHED_SEQUENCE_TEST_RELOAD_VK = "0x52"

        Write-Host ("scale={0:N2} start expectedCycleMs={1}" -f $scale, (Expected-CycleMs $scale))
        $process = Start-Process -FilePath $exe `
            -WorkingDirectory $dist `
            -PassThru `
            -WindowStyle Hidden `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr

        Start-Sleep -Milliseconds ($DelayMs + $DurationMs + $ReloadDelayMs + $ReloadTapMs + $PostTestMs)
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }

        $scaleLabel = "{0:N2}" -f $scale
        $scaleAimLog = Join-Path $dist ("ashe_scan_aim_{0}.log" -f $scaleLabel)
        $scaleDiagLog = Join-Path $dist ("ashe_scan_diag_{0}.log" -f $scaleLabel)
        if (Test-Path -LiteralPath $aimLog) {
            Copy-Item -LiteralPath $aimLog -Destination $scaleAimLog -Force
        }
        if (Test-Path -LiteralPath $diagLog) {
            Copy-Item -LiteralPath $diagLog -Destination $scaleDiagLog -Force
        }

        $result = Parse-AimLog $scaleAimLog $scale
        $results += $result
        $result | Format-Table scale,expectedCycleMs,worker,cycles,cycleAvgMs,cycleErrorAvgMs,hitEvents,stableCycles,cycleProfiles,damage,lastHealth,queueDrops -AutoSize

        if ($scale -ne $Scales[$Scales.Count - 1] -and $RecoverySeconds -gt 0) {
            Start-Sleep -Seconds $RecoverySeconds
        }
    }
}
finally {
    $env:UNLEASHED_SEQUENCE_TEST = $oldSequenceTest
    $env:UNLEASHED_SEQUENCE_TEST_KEY = $oldTestKey
    $env:UNLEASHED_SEQUENCE_TEST_DELAY_MS = $oldDelay
    $env:UNLEASHED_SEQUENCE_TEST_DURATION_MS = $oldDuration
    $env:UNLEASHED_SEQUENCE_WORKER = $oldWorker
    $env:UNLEASHED_SEQUENCE_TEST_HERO = $oldHero
    $env:UNLEASHED_SEQUENCE_TEST_SKILL = $oldSkill
    $env:UNLEASHED_SEQUENCE_TEST_RELOAD = $oldReload
    $env:UNLEASHED_SEQUENCE_TEST_RELOAD_DELAY_MS = $oldReloadDelay
    $env:UNLEASHED_SEQUENCE_TEST_RELOAD_TAP_MS = $oldReloadTap
    $env:UNLEASHED_SEQUENCE_TEST_RELOAD_VK = $oldReloadVk

    if (-not $KeepLastScale) {
        foreach ($path in $jsonProfiles + $iniProfiles) {
            $backup = Join-Path $backupDir (Split-Path -Leaf $path)
            if (Test-Path -LiteralPath $backup) {
                Copy-Item -LiteralPath $backup -Destination $path -Force
            }
        }
    }
}

$resultPath = Join-Path $dist "ashe_timing_scan_results.json"
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding UTF8
Write-Host "wrote $resultPath"
