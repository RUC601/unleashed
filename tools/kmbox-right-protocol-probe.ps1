param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot "..\dist\Unleashed-test\config.ini"),
    [int]$HoldMs = 1500,
    [int]$GapMs = 1000,
    [int]$CountdownMs = 5000,
    [int]$LeftTapDelayMs = 300,
    [int]$LeftTapMs = 60,
    [switch]$ScopedShot,
    [switch]$CombinedMaskTap,
    [ValidateRange(0, 8)]
    [int]$OnlyVariant = 0,
    [switch]$IncludeLeftSanity
)

$ErrorActionPreference = "Stop"

$CmdConnect = [Convert]::ToUInt32("af3c2828", 16)
$CmdMouseMove = [Convert]::ToUInt32("aede7345", 16)
$CmdMouseLeft = [Convert]::ToUInt32("9823AE8D", 16)
$CmdMouseRight = [Convert]::ToUInt32("238d8212", 16)
$CmdMouseMiddle = [Convert]::ToUInt32("97a3AE8D", 16)
$CmdUnmaskAll = [Convert]::ToUInt32("23344343", 16)

function Get-IniValue {
    param(
        [string]$Path,
        [string]$Section,
        [string]$Key,
        [string]$DefaultValue = ""
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $DefaultValue
    }

    $inSection = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith(";") -or $trimmed.StartsWith("#")) {
            continue
        }
        if ($trimmed.StartsWith("[") -and $trimmed.EndsWith("]")) {
            $inSection = $trimmed.Substring(1, $trimmed.Length - 2) -eq $Section
            continue
        }
        if ($inSection) {
            $parts = $trimmed.Split("=", 2)
            if ($parts.Count -eq 2 -and $parts[0].Trim() -eq $Key) {
                return $parts[1].Trim()
            }
        }
    }

    return $DefaultValue
}

function Set-UInt32LE {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [uint32]$Value
    )

    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Buffer, $Offset, 4)
}

function New-IntPayload {
    param([int]$Value)
    return [BitConverter]::GetBytes([int32]$Value)
}

function New-MousePayload16 {
    param(
        [int]$Button,
        [int]$X = 0,
        [int]$Y = 0,
        [int]$Wheel = 0
    )

    $payload = New-Object byte[] 16
    [Array]::Copy([BitConverter]::GetBytes([int32]$Button), 0, $payload, 0, 4)
    [Array]::Copy([BitConverter]::GetBytes([int32]$X), 0, $payload, 4, 4)
    [Array]::Copy([BitConverter]::GetBytes([int32]$Y), 0, $payload, 8, 4)
    [Array]::Copy([BitConverter]::GetBytes([int32]$Wheel), 0, $payload, 12, 4)
    return $payload
}

function New-SoftMousePayload {
    param(
        [int]$Button,
        [int]$X = 0,
        [int]$Y = 0,
        [int]$Wheel = 0
    )

    $payload = New-Object byte[] 56
    [Array]::Copy([BitConverter]::GetBytes([int32]$Button), 0, $payload, 0, 4)
    [Array]::Copy([BitConverter]::GetBytes([int32]$X), 0, $payload, 4, 4)
    [Array]::Copy([BitConverter]::GetBytes([int32]$Y), 0, $payload, 8, 4)
    [Array]::Copy([BitConverter]::GetBytes([int32]$Wheel), 0, $payload, 12, 4)
    return $payload
}

function New-KmPacket {
    param(
        [uint32]$Mac,
        [uint32]$Rand,
        [uint32]$Index,
        [uint32]$Cmd,
        [byte[]]$Payload = @()
    )

    $packet = New-Object byte[] (16 + $Payload.Length)
    Set-UInt32LE $packet 0 $Mac
    Set-UInt32LE $packet 4 $Rand
    Set-UInt32LE $packet 8 $Index
    Set-UInt32LE $packet 12 $Cmd
    if ($Payload.Length -gt 0) {
        [Array]::Copy($Payload, 0, $packet, 16, $Payload.Length)
    }
    return $packet
}

function Receive-KmAck {
    param(
        [System.Net.Sockets.UdpClient]$Client,
        [uint32]$ExpectedCmd,
        [uint32]$ExpectedIndex
    )

    $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
    try {
        $reply = $Client.Receive([ref]$remote)
    } catch {
        return "timeout"
    }

    if ($reply.Length -lt 16) {
        return "short:$($reply.Length)"
    }

    $cmd = [BitConverter]::ToUInt32($reply, 12)
    $index = [BitConverter]::ToUInt32($reply, 8)
    if ($cmd -ne $ExpectedCmd) {
        return ("cmd-mismatch:0x{0:X8}" -f $cmd)
    }
    if ($index -ne $ExpectedIndex) {
        return "idx-mismatch:$index"
    }
    return "ack:$($reply.Length)"
}

function Send-KmPacket {
    param(
        [System.Net.Sockets.UdpClient]$Client,
        [System.Net.IPEndPoint]$Endpoint,
        [uint32]$Mac,
        [uint32]$Cmd,
        [byte[]]$Payload,
        [uint32]$Rand,
        [uint32]$Index,
        [string]$Label
    )

    $packet = New-KmPacket -Mac $Mac -Rand $Rand -Index $Index -Cmd $Cmd -Payload $Payload
    [void]$Client.Send($packet, $packet.Length, $Endpoint)
    $ack = Receive-KmAck -Client $Client -ExpectedCmd $Cmd -ExpectedIndex $Index
    $line = "{0:HH:mm:ss.fff} {1,-38} cmd=0x{2:X8} idx={3} rand=0x{4:X8} len={5} {6}" -f `
        (Get-Date), $Label, $Cmd, $Index, $Rand, $packet.Length, $ack
    Write-Host $line
    Add-Content -LiteralPath $script:LogPath -Value $line
}

$ip = Get-IniValue -Path $ConfigPath -Section "KMBox" -Key "kmboxIp" -DefaultValue "192.168.2.188"
$port = [int](Get-IniValue -Path $ConfigPath -Section "KMBox" -Key "kmboxPort" -DefaultValue "8808")
$macText = Get-IniValue -Path $ConfigPath -Section "KMBox" -Key "kmboxMac" -DefaultValue "12525C53"
$mac = [Convert]::ToUInt32($macText, 16)

$logDir = Split-Path -Parent $ConfigPath
if (-not (Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}
$script:LogPath = Join-Path $logDir ("kmbox_right_probe_{0}.log" -f (Get-Date -Format "yyyyMMdd_HHmmss"))

$client = [System.Net.Sockets.UdpClient]::new()
$client.Client.ReceiveTimeout = 500
$endpoint = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Parse($ip), $port)
$rng = [System.Random]::new()
$script:Index = [uint32]0

try {
    "KMBox right protocol probe ip=$ip port=$port mac=$macText holdMs=$HoldMs gapMs=$GapMs scopedShot=$($ScopedShot.IsPresent) combinedMaskTap=$($CombinedMaskTap.IsPresent) onlyVariant=$OnlyVariant config=$ConfigPath" |
        Tee-Object -FilePath $script:LogPath

    $connectRand = [uint32]$rng.Next(1, [int]::MaxValue)
    $connectPacket = New-KmPacket -Mac $mac -Rand $connectRand -Index 0 -Cmd $CmdConnect
    [void]$client.Send($connectPacket, $connectPacket.Length, $endpoint)
    $connectAck = Receive-KmAck -Client $client -ExpectedCmd $CmdConnect -ExpectedIndex 0
    $line = "{0:HH:mm:ss.fff} connect                              cmd=0x{1:X8} idx=0 rand=0x{2:X8} len=16 {3}" -f `
        (Get-Date), $CmdConnect, $connectRand, $connectAck
    Write-Host $line
    Add-Content -LiteralPath $script:LogPath -Value $line

    Write-Host ""
    Write-Host "Watch the controlled PC now. First variant starts after $CountdownMs ms."
    if ($ScopedShot) {
        if ($CombinedMaskTap) {
            Write-Host "Each variant holds RIGHT mask=2, switches to mask=3 after $LeftTapDelayMs ms for $LeftTapMs ms, returns to mask=2, releases RIGHT, then waits $GapMs ms."
        } else {
            Write-Host "Each variant holds RIGHT, taps LEFT after $LeftTapDelayMs ms for $LeftTapMs ms, releases RIGHT, then waits $GapMs ms."
        }
    } else {
        Write-Host "Each variant holds RIGHT for $HoldMs ms, releases it, then waits $GapMs ms."
    }
    Start-Sleep -Milliseconds $CountdownMs

    $variants = @(
        @{
            Name = "right bool len20"
            Cmd = $CmdMouseRight
            Down = New-IntPayload 1
            Up = New-IntPayload 0
            TapDown = New-IntPayload 3
            TapUp = New-IntPayload 2
        },
        @{
            Name = "right mask len20"
            Cmd = $CmdMouseRight
            Down = New-IntPayload 2
            Up = New-IntPayload 0
            TapDown = New-IntPayload 3
            TapUp = New-IntPayload 2
        },
        @{
            Name = "right soft bool len72"
            Cmd = $CmdMouseRight
            Down = New-SoftMousePayload 1
            Up = New-SoftMousePayload 0
            TapDown = New-SoftMousePayload 3
            TapUp = New-SoftMousePayload 2
        },
        @{
            Name = "right soft mask len72"
            Cmd = $CmdMouseRight
            Down = New-SoftMousePayload 2
            Up = New-SoftMousePayload 0
            TapDown = New-SoftMousePayload 3
            TapUp = New-SoftMousePayload 2
        },
        @{
            Name = "mouse mask=2 len32"
            Cmd = $CmdMouseMove
            Down = New-MousePayload16 2
            Up = New-MousePayload16 0
            TapDown = New-MousePayload16 3
            TapUp = New-MousePayload16 2
        },
        @{
            Name = "mouse mask=3 len32"
            Cmd = $CmdMouseMove
            Down = New-MousePayload16 3
            Up = New-MousePayload16 0
            TapDown = New-MousePayload16 3
            TapUp = New-MousePayload16 2
        },
        @{
            Name = "mouse soft mask=2 len72"
            Cmd = $CmdMouseMove
            Down = New-SoftMousePayload 2
            Up = New-SoftMousePayload 0
            TapDown = New-SoftMousePayload 3
            TapUp = New-SoftMousePayload 2
        },
        @{
            Name = "mouse soft mask=3 len72"
            Cmd = $CmdMouseMove
            Down = New-SoftMousePayload 3
            Up = New-SoftMousePayload 0
            TapDown = New-SoftMousePayload 3
            TapUp = New-SoftMousePayload 2
        }
    )

    if ($IncludeLeftSanity) {
        $variants += @{
            Name = "left sanity bool len20"
            Cmd = $CmdMouseLeft
            Down = New-IntPayload 1
            Up = New-IntPayload 0
        }
    }

    $script:Index = [uint32]($script:Index + 1)
    $rand = [uint32]$rng.Next(1, [int]::MaxValue)
    Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $CmdUnmaskAll `
        -Payload @() -Rand $rand -Index $script:Index -Label "unmask all"

    foreach ($release in @(
        @{ Cmd = $CmdMouseLeft; Payload = New-IntPayload 0; Name = "cleanup left" },
        @{ Cmd = $CmdMouseRight; Payload = New-IntPayload 0; Name = "cleanup right" },
        @{ Cmd = $CmdMouseMiddle; Payload = New-IntPayload 0; Name = "cleanup middle" },
        @{ Cmd = $CmdMouseMove; Payload = New-MousePayload16 0; Name = "cleanup mouse" }
    )) {
        $script:Index = [uint32]($script:Index + 1)
        $rand = [uint32]$rng.Next(1, [int]::MaxValue)
        Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $release.Cmd `
            -Payload $release.Payload -Rand $rand -Index $script:Index -Label $release.Name
    }

    $variantNumber = 0
    foreach ($variant in $variants) {
        $variantNumber += 1
        if ($OnlyVariant -ne 0 -and $OnlyVariant -ne $variantNumber) {
            continue
        }
        Write-Host ""
        Write-Host ("Variant #{0}: {1}" -f $variantNumber, $variant.Name)
        Add-Content -LiteralPath $script:LogPath -Value ("Variant #{0}: {1}" -f $variantNumber, $variant.Name)

        $script:Index = [uint32]($script:Index + 1)
        $randDown = [uint32]$rng.Next(1, [int]::MaxValue)
        Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $variant.Cmd `
            -Payload $variant.Down -Rand $randDown -Index $script:Index -Label ("v{0} down" -f $variantNumber)

        if ($ScopedShot) {
            $leadMs = [Math]::Max(0, [Math]::Min($LeftTapDelayMs, $HoldMs))
            Start-Sleep -Milliseconds $leadMs

            if ($CombinedMaskTap) {
                $script:Index = [uint32]($script:Index + 1)
                $randTapDown = [uint32]$rng.Next(1, [int]::MaxValue)
                Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $variant.Cmd `
                    -Payload $variant.TapDown -Rand $randTapDown -Index $script:Index -Label ("v{0} mask 3" -f $variantNumber)

                Start-Sleep -Milliseconds $LeftTapMs

                $script:Index = [uint32]($script:Index + 1)
                $randTapUp = [uint32]$rng.Next(1, [int]::MaxValue)
                Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $variant.Cmd `
                    -Payload $variant.TapUp -Rand $randTapUp -Index $script:Index -Label ("v{0} mask 2" -f $variantNumber)
            } else {
                $script:Index = [uint32]($script:Index + 1)
                $randLeftDown = [uint32]$rng.Next(1, [int]::MaxValue)
                Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $CmdMouseLeft `
                    -Payload (New-IntPayload 1) -Rand $randLeftDown -Index $script:Index -Label ("v{0} left down" -f $variantNumber)

                Start-Sleep -Milliseconds $LeftTapMs

                $script:Index = [uint32]($script:Index + 1)
                $randLeftUp = [uint32]$rng.Next(1, [int]::MaxValue)
                Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $CmdMouseLeft `
                    -Payload (New-IntPayload 0) -Rand $randLeftUp -Index $script:Index -Label ("v{0} left up" -f $variantNumber)
            }

            $remainingMs = [Math]::Max(0, $HoldMs - $leadMs - $LeftTapMs)
            Start-Sleep -Milliseconds $remainingMs
        } else {
            Start-Sleep -Milliseconds $HoldMs
        }

        $script:Index = [uint32]($script:Index + 1)
        $randUp = [uint32]$rng.Next(1, [int]::MaxValue)
        Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $variant.Cmd `
            -Payload $variant.Up -Rand $randUp -Index $script:Index -Label ("v{0} up" -f $variantNumber)

        Start-Sleep -Milliseconds $GapMs
    }

    foreach ($release in @(
        @{ Cmd = $CmdMouseLeft; Payload = New-IntPayload 0; Name = "final left" },
        @{ Cmd = $CmdMouseRight; Payload = New-IntPayload 0; Name = "final right" },
        @{ Cmd = $CmdMouseMiddle; Payload = New-IntPayload 0; Name = "final middle" },
        @{ Cmd = $CmdMouseMove; Payload = New-MousePayload16 0; Name = "final mouse" }
    )) {
        $script:Index = [uint32]($script:Index + 1)
        $rand = [uint32]$rng.Next(1, [int]::MaxValue)
        Send-KmPacket -Client $client -Endpoint $endpoint -Mac $mac -Cmd $release.Cmd `
            -Payload $release.Payload -Rand $rand -Index $script:Index -Label $release.Name
    }

    Write-Host ""
    Write-Host "Probe log: $script:LogPath"
} finally {
    $client.Close()
}
